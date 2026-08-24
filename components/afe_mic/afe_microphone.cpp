#include "afe_microphone.h"

#ifdef USE_ESP32

#include "esphome/components/audio/audio.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <cstring>

#ifdef USE_AFE_MIC_ESP_SR
#include <esp_heap_caps.h>
#endif

namespace esphome::afe_mic {

static const char *const TAG = "afe_mic";

static const UBaseType_t MAX_LISTENERS = 16;

// Lesetakt des Capture-Tasks. Die tatsaechliche Lesegroesse wird unten auf
// READ_DURATION_MS / Dezimierungsfaktor Echtzeit bemessen (bewaehrtes Muster
// aus components/i2s_audio/microphone: kurze Reads mit grossem
// Timeout-Spielraum statt langer Reads am Timeout-Limit).
static const uint32_t READ_DURATION_MS = 16;

static const size_t TASK_STACK_SIZE = 4096;
static const ssize_t TASK_PRIORITY = 23;

// Zielrate der Sprachverarbeitung: micro_wake_word und voice_assistant
// verarbeiten ausschliesslich 16-kHz-Audio (Vorgabe der Feature-Frontends
// beider Komponenten, gleichzeitig die Eingaberate des esp-sr-AFE). Muss zur
// gleichnamigen Konstante in microphone.py passen.
static const uint32_t TARGET_RATE_HZ = 16000;

// Ausgabeformat zur Sprachverarbeitung: mono 16 bit (Erwartung von
// micro_wake_word/voice_assistant; MicrophoneSource konvertiert nur Bits,
// nie die Samplerate oder Kanalzahl)
static const uint8_t OUTPUT_BITS = 16;
static const uint8_t OUTPUT_CHANNELS = 1;

enum MicrophoneEventGroupBits : uint32_t {
  COMMAND_STOP = (1 << 0),

  TASK_RUNNING = (1 << 11),
  TASK_STOPPED = (1 << 13),
  FETCH_TASK_STOPPED = (1 << 14),

  ALL_BITS = 0x00FFFFFF,
};

void AfeMicrophone::setup() {
  if (this->bus_ == nullptr || !this->bus_->is_duplex()) {
    ESP_LOGE(TAG, "afe_mic braucht einen i2s_audio-Bus mit duplex:-Block");
    this->mark_failed();
    return;
  }
  if (this->bus_->is_duplex_slot_monitor_enabled()) {
    // Zwei Leser desselben RX-Kanals stehlen sich gegenseitig DMA-Bloecke
    ESP_LOGE(TAG, "slot_monitor: true am Bus kollidiert mit afe_mic - Monitor abschalten");
    this->mark_failed();
    return;
  }
  const uint32_t bus_rate = this->bus_->get_duplex_sample_rate();
  if (bus_rate < TARGET_RATE_HZ || bus_rate % TARGET_RATE_HZ != 0) {
    ESP_LOGE(TAG, "Bus-Rate %" PRIu32 " Hz ist kein ganzzahliges Vielfaches von %" PRIu32 " Hz", bus_rate,
             TARGET_RATE_HZ);
    this->mark_failed();
    return;
  }
  const uint8_t slots = this->bus_->get_duplex_rx_slot_count();
  if (this->input_format_.length() != slots) {
    ESP_LOGE(TAG, "input_format '%s' beschreibt %u Slots, der Bus liefert %u", this->input_format_.c_str(),
             (unsigned) this->input_format_.length(), slots);
    this->mark_failed();
    return;
  }
  if (this->passthrough_channel_ >= slots || this->input_format_[this->passthrough_channel_] != 'M') {
    ESP_LOGE(TAG, "passthrough_channel %u ist kein M-Slot in '%s'", this->passthrough_channel_,
             this->input_format_.c_str());
    this->mark_failed();
    return;
  }

  this->active_listeners_semaphore_ = xSemaphoreCreateCounting(MAX_LISTENERS, MAX_LISTENERS);
  this->event_group_ = xEventGroupCreate();
  if (this->active_listeners_semaphore_ == nullptr || this->event_group_ == nullptr) {
    ESP_LOGE(TAG, "Semaphore/Event-Group konnte nicht angelegt werden");
    this->mark_failed();
    return;
  }

#ifdef USE_AFE_MIC_ESP_SR
  if (this->afe_configured_) {
    if (!this->setup_afe_()) {
      this->mark_failed();
      return;
    }
    this->afe_active_.store(true, std::memory_order_relaxed);
  }
#endif

  this->audio_stream_info_ = audio::AudioStreamInfo(OUTPUT_BITS, OUTPUT_CHANNELS, TARGET_RATE_HZ);
}

#ifdef USE_AFE_MIC_ESP_SR
bool AfeMicrophone::setup_afe_() {
  // Modellfreier AFE-Betrieb: kein WakeNet (das Wake Word erkennt weiterhin
  // micro_wake_word auf dem gesaeuberten Ausgang), VAD strukturell aktiv
  // (dient dem AFE als Kanalselektor), Erkennungs-Gates bleiben bei mWW.
  afe_config_t *cfg = afe_config_init(this->input_format_.c_str(), nullptr, AFE_TYPE_SR, AFE_MODE_LOW_COST);
  if (cfg == nullptr) {
    ESP_LOGE(TAG, "afe_config_init('%s') fehlgeschlagen", this->input_format_.c_str());
    return false;
  }
  cfg->wakenet_init = false;
  cfg->vad_init = true;
  cfg->aec_init = this->afe_aec_;
  cfg->se_init = this->afe_bss_;
  cfg->agc_init = this->afe_agc_;
  if (this->afe_agc_) {
    cfg->agc_mode = AFE_AGC_MODE_WEBRTC;
  }
  // Sprach-Kern auf Core 1: dort laufen auch Capture/Fetch, Core 0 behaelt
  // WiFi/Mainloop/Wiedergabe
  cfg->afe_perferred_core = 1;
  // Grosse Puffer bevorzugt ins PSRAM (8 MB vorhanden, interner RAM ist knapp)
  cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

  this->afe_iface_ = esp_afe_handle_from_config(cfg);
  if (this->afe_iface_ == nullptr) {
    ESP_LOGE(TAG, "esp_afe_handle_from_config lieferte NULL");
    return false;
  }
  this->afe_data_ = this->afe_iface_->create_from_config(cfg);
  if (this->afe_data_ == nullptr) {
    ESP_LOGE(TAG, "AFE create_from_config fehlgeschlagen");
    return false;
  }
  this->afe_feed_chunk_ = this->afe_iface_->get_feed_chunksize(this->afe_data_);
  this->afe_feed_channels_ = this->afe_iface_->get_feed_channel_num(this->afe_data_);
  if (this->afe_feed_channels_ != (int) this->input_format_.length()) {
    ESP_LOGE(TAG, "AFE erwartet %d Kanaele, input_format '%s' liefert %u", this->afe_feed_channels_,
             this->input_format_.c_str(), (unsigned) this->input_format_.length());
    return false;
  }
  ESP_LOGI(TAG, "AFE bereit: feed %d Samples/Kanal x %d Kanaele, fetch %d Samples", this->afe_feed_chunk_,
           this->afe_feed_channels_, this->afe_iface_->get_fetch_chunksize(this->afe_data_));
  return true;
}
#endif

void AfeMicrophone::dump_config() {
  ESP_LOGCONFIG(TAG,
                "AFE-Mikrofon (Duplex-Bus):\n"
                "  Slot-Layout: %s\n"
                "  Passthrough-Slot: %u\n"
                "  Bus-Rate: %" PRIu32 " Hz -> Ausgabe: %" PRIu32 " Hz mono %u bit",
                this->input_format_.c_str(), this->passthrough_channel_, this->bus_->get_duplex_sample_rate(),
                (uint32_t) TARGET_RATE_HZ, OUTPUT_BITS);
#ifdef USE_AFE_MIC_ESP_SR
  ESP_LOGCONFIG(TAG, "  AFE: %s (AEC %s, BSS %s, AGC %s)", this->afe_configured_ ? "konfiguriert" : "nicht gebaut",
                YESNO(this->afe_aec_), YESNO(this->afe_bss_), YESNO(this->afe_agc_));
#endif
}

void AfeMicrophone::start() {
  if (this->is_failed())
    return;
  xSemaphoreTake(this->active_listeners_semaphore_, 0);
}

void AfeMicrophone::stop() {
  if (this->state_ == microphone::STATE_STOPPED || this->is_failed())
    return;
  xSemaphoreGive(this->active_listeners_semaphore_);
}

void AfeMicrophone::capture_task(void *params) {
  AfeMicrophone *this_mic = (AfeMicrophone *) params;

  {
    // Dezimierung Bus-Rate -> Zielrate per Boxcar-Mittelwert ueber ratio
    // aufeinanderfolgende Frames, fuer ALLE Slots (das AFE braucht Mikros und
    // Echo-Referenz identisch dezimiert und zeitsynchron). Der Boxcar hat
    // Nullstellen bei Vielfachen der Zielrate (48k->16k: exakt 16 und 32 kHz)
    // und daempft damit genau die Baender, die beim Dezimieren in den
    // Nutzbereich falten wuerden.
    const uint32_t bus_rate = this_mic->bus_->get_duplex_sample_rate();
    const size_t ratio = bus_rate / TARGET_RATE_HZ;
    const uint8_t slots = this_mic->bus_->get_duplex_rx_slot_count();
    const size_t in_sample_bytes = this_mic->bus_->get_duplex_slot_bits() / 8;
    const size_t in_frame_bytes = (size_t) slots * in_sample_bytes;
    const size_t in_block_bytes = ratio * in_frame_bytes;  // ergibt 1 dezimierten Frame
    const size_t out_sample_bytes = this_mic->audio_stream_info_.samples_to_bytes(1);
    const uint8_t pass_ch = this_mic->passthrough_channel_;

    // Lesegroesse: READ_DURATION_MS an der ZIELRATE bemessen, also
    // READ_DURATION_MS/ratio Echtzeit pro Read - kurze Reads mit grossem
    // Abstand zum 2*READ_DURATION_MS-Timeout (Lehre aus dem i2s-Mikrofon:
    // Requests nahe am Timeout kippen unter Last in ESP_ERR_TIMEOUT)
    size_t in_frames_per_read = (bus_rate * READ_DURATION_MS) / (1000 * ratio);
    in_frames_per_read -= in_frames_per_read % ratio;  // nur ganze Dezimierungs-Bloecke
    if (in_frames_per_read == 0) {
      in_frames_per_read = ratio;
    }
    const size_t in_bytes_per_read = in_frames_per_read * in_frame_bytes;
    const size_t out_frames_per_read = in_frames_per_read / ratio;

    std::vector<uint8_t> input(in_bytes_per_read);
    // dezimierte Frames, alle Slots interleaved (Reihenfolge = input_format)
    std::vector<int16_t> decimated(out_frames_per_read * slots);
    std::vector<uint8_t> mono(out_frames_per_read * out_sample_bytes);
#ifdef USE_AFE_MIC_ESP_SR
    // Sammelpuffer fuer feed(): das AFE will exakt feed_chunk Frames je Aufruf
    std::vector<int16_t> feed_accu;
    if (this_mic->afe_configured_) {
      feed_accu.reserve((size_t) this_mic->afe_feed_chunk_ * this_mic->afe_feed_channels_ * 2);
    }
#endif

    xEventGroupSetBits(this_mic->event_group_, MicrophoneEventGroupBits::TASK_RUNNING);

    while (!(xEventGroupGetBits(this_mic->event_group_) & MicrophoneEventGroupBits::COMMAND_STOP)) {
      if (this_mic->data_callbacks_.size() == 0) {
        vTaskDelay(pdMS_TO_TICKS(READ_DURATION_MS));
        continue;
      }

      size_t bytes_read = 0;
      esp_err_t err = i2s_channel_read(this_mic->bus_->get_duplex_rx_handle(), input.data(), in_bytes_per_read,
                                       &bytes_read, 2 * READ_DURATION_MS);
      if (err != ESP_OK || bytes_read == 0) {
        if (!this_mic->status_has_warning()) {
          ESP_LOGW(TAG, "RX-Lesefehler: %s", esp_err_to_name(err));
        }
        this_mic->status_set_warning();
        continue;
      }
      this_mic->status_clear_warning();

      const size_t blocks = bytes_read / in_block_bytes;
      decimated.resize(blocks * slots);
      for (size_t block = 0; block < blocks; block++) {
        const size_t block_start = block * in_block_bytes;
        for (uint8_t s = 0; s < slots; s++) {
          int64_t acc = 0;
          for (size_t frame = 0; frame < ratio; frame++) {
            acc += audio::unpack_audio_sample_to_q31(
                &input[block_start + frame * in_frame_bytes + (size_t) s * in_sample_bytes], in_sample_bytes);
          }
          const int32_t mean_q31 = static_cast<int32_t>(acc / static_cast<int64_t>(ratio));
          int16_t sample16;
          audio::pack_q31_as_audio_sample(mean_q31, reinterpret_cast<uint8_t *>(&sample16), sizeof(sample16));
          decimated[block * slots + s] = sample16;
        }
      }

#ifdef USE_AFE_MIC_ESP_SR
      if (this_mic->afe_configured_ && this_mic->afe_active_.load(std::memory_order_relaxed)) {
        // AFE-Pfad: alle Kanaele einspeisen; der Ausgang kommt im fetch_task
        feed_accu.insert(feed_accu.end(), decimated.begin(), decimated.begin() + blocks * slots);
        const size_t chunk_samples = (size_t) this_mic->afe_feed_chunk_ * this_mic->afe_feed_channels_;
        size_t offset = 0;
        while (feed_accu.size() - offset >= chunk_samples) {
          this_mic->afe_iface_->feed(this_mic->afe_data_, feed_accu.data() + offset);
          offset += chunk_samples;
        }
        if (offset > 0) {
          feed_accu.erase(feed_accu.begin(), feed_accu.begin() + offset);
        }
        continue;
      }
      // Umschaltung auf Passthrough: angefangenen Feed-Block verwerfen
      if (!feed_accu.empty()) {
        feed_accu.clear();
      }
#endif

      // Passthrough-Pfad: gewaehlten Mikrofon-Slot direkt ausliefern
      mono.resize(blocks * out_sample_bytes);
      for (size_t block = 0; block < blocks; block++) {
        const int16_t v = decimated[block * slots + pass_ch];
        memcpy(&mono[block * out_sample_bytes], &v, sizeof(v));
      }
      this_mic->data_callbacks_.call(mono);
    }
  }

  xEventGroupSetBits(this_mic->event_group_, MicrophoneEventGroupBits::TASK_STOPPED);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

#ifdef USE_AFE_MIC_ESP_SR
void AfeMicrophone::fetch_task(void *params) {
  AfeMicrophone *this_mic = (AfeMicrophone *) params;
  std::vector<uint8_t> output;

  while (!(xEventGroupGetBits(this_mic->event_group_) & MicrophoneEventGroupBits::COMMAND_STOP)) {
    if (!this_mic->afe_active_.load(std::memory_order_relaxed)) {
      vTaskDelay(pdMS_TO_TICKS(READ_DURATION_MS));
      continue;
    }
    // Timeout statt portMAX_DELAY, damit Stop/Umschaltung nie blockiert
    afe_fetch_result_t *res = this_mic->afe_iface_->fetch_with_delay(this_mic->afe_data_, pdMS_TO_TICKS(100));
    if (res == nullptr || res->ret_value != ESP_OK || res->data == nullptr || res->data_size <= 0) {
      continue;
    }
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(res->data);
    output.assign(bytes, bytes + res->data_size);
    this_mic->data_callbacks_.call(output);
  }

  xEventGroupSetBits(this_mic->event_group_, MicrophoneEventGroupBits::FETCH_TASK_STOPPED);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#endif

void AfeMicrophone::loop() {
  uint32_t event_group_bits = xEventGroupGetBits(this->event_group_);

  if (event_group_bits & MicrophoneEventGroupBits::TASK_RUNNING) {
    xEventGroupClearBits(this->event_group_, MicrophoneEventGroupBits::TASK_RUNNING);
    this->state_ = microphone::STATE_RUNNING;
  }

  if (event_group_bits & MicrophoneEventGroupBits::TASK_STOPPED) {
    bool fetch_done = true;
#ifdef USE_AFE_MIC_ESP_SR
    fetch_done = (this->fetch_task_handle_ == nullptr) ||
                 (event_group_bits & MicrophoneEventGroupBits::FETCH_TASK_STOPPED);
#endif
    if (fetch_done) {
      vTaskDelete(this->task_handle_);
      this->task_handle_ = nullptr;
#ifdef USE_AFE_MIC_ESP_SR
      if (this->fetch_task_handle_ != nullptr) {
        vTaskDelete(this->fetch_task_handle_);
        this->fetch_task_handle_ = nullptr;
      }
#endif
      xEventGroupClearBits(this->event_group_, ALL_BITS);
      this->state_ = microphone::STATE_STOPPED;
    }
  }

  if ((uxSemaphoreGetCount(this->active_listeners_semaphore_) < MAX_LISTENERS) &&
      (this->state_ == microphone::STATE_STOPPED)) {
    this->state_ = microphone::STATE_STARTING;
  }
  if ((uxSemaphoreGetCount(this->active_listeners_semaphore_) == MAX_LISTENERS) &&
      (this->state_ == microphone::STATE_RUNNING)) {
    this->state_ = microphone::STATE_STOPPING;
  }

  switch (this->state_) {
    case microphone::STATE_STARTING:
      // Kein Treiber-Start noetig: der Bus haelt den RX-Kanal dauerhaft aktiv
      if (this->task_handle_ == nullptr) {
#ifdef USE_AFE_MIC_ESP_SR
        if (this->afe_configured_) {
          // Sprachpfad komplett auf Core 1 (Core 0: WiFi/Mainloop/Wiedergabe)
          xTaskCreatePinnedToCore(AfeMicrophone::capture_task, "afe_capture", TASK_STACK_SIZE, (void *) this,
                                  TASK_PRIORITY, &this->task_handle_, 1);
          if (this->task_handle_ != nullptr && this->fetch_task_handle_ == nullptr) {
            xTaskCreatePinnedToCore(AfeMicrophone::fetch_task, "afe_fetch", TASK_STACK_SIZE, (void *) this,
                                    TASK_PRIORITY - 1, &this->fetch_task_handle_, 1);
            if (this->fetch_task_handle_ == nullptr) {
              ESP_LOGE(TAG, "Fetch-Task startet nicht");
              xEventGroupSetBits(this->event_group_, MicrophoneEventGroupBits::COMMAND_STOP);
            }
          }
        } else
#endif
        {
          xTaskCreate(AfeMicrophone::capture_task, "afe_capture", TASK_STACK_SIZE, (void *) this, TASK_PRIORITY,
                      &this->task_handle_);
        }
        if (this->task_handle_ == nullptr) {
          ESP_LOGE(TAG, "Capture-Task startet nicht, neuer Versuch in 1 s");
          this->status_momentary_error("task_fail", 1000);
        }
      }
      break;
    case microphone::STATE_RUNNING:
    case microphone::STATE_STOPPED:
      break;
    case microphone::STATE_STOPPING:
      xEventGroupSetBits(this->event_group_, MicrophoneEventGroupBits::COMMAND_STOP);
      break;
  }
}

}  // namespace esphome::afe_mic

#endif  // USE_ESP32
