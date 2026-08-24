#include "i2s_audio.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cinttypes>
#include <cmath>
#include <vector>

namespace esphome::i2s_audio {

static const char *const TAG = "i2s_audio";

// DMA-Dimensionierung des Duplex-Ports: 10-ms-Deskriptoren bei 48 kHz.
// RX-Frame = 4 Slots x 2 B = 8 B -> 480 Frames = 3840 B, unter dem
// 4092-B-Deskriptor-Limit des IDF-Treibers.
static const uint32_t DUPLEX_DMA_DESC_NUM = 5;
static const uint32_t DUPLEX_DMA_FRAME_NUM = 480;

uint8_t I2SAudioComponent::get_duplex_rx_slot_count() const {
  uint8_t count = 0;
  for (uint32_t mask = this->duplex_rx_slot_mask_; mask != 0; mask >>= 1) {
    count += mask & 1;
  }
  return count;
}

float I2SAudioComponent::get_setup_priority() const {
  // Vor den Codec-Komponenten (DATA): einige Codecs resetten ohne anliegenden
  // MCLK, daher muessen die Takte stehen, bevor deren I2C-Setup laeuft
  return setup_priority::HARDWARE;
}

void I2SAudioComponent::setup() {
  if (!this->duplex_enabled_) {
    return;  // klassischer Betrieb: Kanaele gehoeren den Kind-Plattformen
  }

  i2s_chan_config_t chan_cfg = {};
  chan_cfg.id = this->get_port();
  chan_cfg.role = I2S_ROLE_MASTER;
  chan_cfg.dma_desc_num = DUPLEX_DMA_DESC_NUM;
  chan_cfg.dma_frame_num = DUPLEX_DMA_FRAME_NUM;
  // TX-Underrun soll Stille senden statt alte DMA-Puffer zu wiederholen
  chan_cfg.auto_clear = true;

  // Ein einziger Aufruf mit beiden Handles: nur so setzt der Treiber
  // full_duplex und koppelt TX/RX hardwareseitig an dieselben Takte
  esp_err_t err = i2s_new_channel(&chan_cfg, &this->duplex_tx_handle_, &this->duplex_rx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Duplex: i2s_new_channel fehlgeschlagen: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  const i2s_data_bit_width_t data_bits =
      (this->duplex_slot_bits_ == 32) ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT;

  i2s_tdm_clk_config_t clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(this->duplex_sample_rate_);
#ifdef I2S_CLK_SRC_APLL
  // Nur Chips mit APLL (klassischer ESP32); der S3 nutzt immer die PLL-Quelle
  if (this->duplex_use_apll_) {
    clk_cfg.clk_src = I2S_CLK_SRC_APLL;
  }
#endif
  clk_cfg.mclk_multiple = this->duplex_mclk_multiple_;

  // RX ZUERST: der zuerst initialisierte Kanal bleibt Master und bindet MCLK
  // an seinen Teiler; MCLK/BCLK/WS laufen ab hier dauerhaft
  i2s_tdm_config_t rx_cfg = {};
  rx_cfg.clk_cfg = clk_cfg;
  rx_cfg.slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(data_bits, I2S_SLOT_MODE_STEREO,
                                                        (i2s_tdm_slot_mask_t) this->duplex_rx_slot_mask_);
  rx_cfg.slot_cfg.total_slot = this->duplex_total_slots_;
  rx_cfg.gpio_cfg.mclk = (gpio_num_t) this->mclk_pin_;
  rx_cfg.gpio_cfg.bclk = (gpio_num_t) this->bclk_pin_;
  rx_cfg.gpio_cfg.ws = (gpio_num_t) this->lrclk_pin_;
  rx_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
  rx_cfg.gpio_cfg.din = (gpio_num_t) this->duplex_din_pin_;

  err = i2s_channel_init_tdm_mode(this->duplex_rx_handle_, &rx_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Duplex: RX-TDM-Init fehlgeschlagen: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  // TX als Zweiter wird full_duplex_slave. Der DAC ist ein Standard-I2S-
  // Geraet, deshalb laeuft TX im STD-Modus: 2 Slots ueber die halbe Frame-
  // Breite, 16-bit-Daten MSB-buendig, Rest des Slots fuellt die Hardware mit
  // Nullen. Auf dem Draht ist das identisch zu TDM-Daten in den Slots 0 und
  // total/2 (gleiche BCLK-Anzahl, WS 50 % Duty) - aber es nutzt den seit
  // Jahren bewaehrten 16-in-32-Datenpfad statt des TDM-Sonderfalls
  // "16-bit-Slots mit Luecken", der am ES8311 pegelverfaelschte Ausgabe
  // erzeugte (gemessen: +12 dB Eingangsstufe -> -2,6 dB am Verstaerker).
  const uint32_t tx_slot_width_bits = ((uint32_t) this->duplex_total_slots_ * this->duplex_slot_bits_) / 2;
  if (tx_slot_width_bits > 32) {
    ESP_LOGE(TAG, "Duplex: TX-Slotbreite %" PRIu32 " bit nicht darstellbar (max 32)", tx_slot_width_bits);
    this->mark_failed();
    return;
  }
  i2s_std_config_t tx_cfg = {};
  tx_cfg.clk_cfg.sample_rate_hz = clk_cfg.sample_rate_hz;
  tx_cfg.clk_cfg.clk_src = clk_cfg.clk_src;
  tx_cfg.clk_cfg.mclk_multiple = clk_cfg.mclk_multiple;
  tx_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(data_bits, I2S_SLOT_MODE_STEREO);
  tx_cfg.slot_cfg.slot_bit_width = (i2s_slot_bit_width_t) tx_slot_width_bits;
  // Empirisch ermittelt (Treppenton-Messung + Hoertest): der ES8311 latcht im
  // Slave-Betrieb an diesem Bus das MSB direkt an der WS-Flanke. Mit dem
  // Philips-1-Bit-Versatz liest er stattdessen [Padding-0][Bits 15..1] und
  // spielt ein vorzeichendominiertes Rechteck konstanter Amplitude. Im alten
  // Master-Betrieb war das unsichtbar, weil der Codec sein eigenes Timing
  // definierte. MSB an die Flanke legen:
  tx_cfg.slot_cfg.bit_shift = false;
  tx_cfg.gpio_cfg.mclk = (gpio_num_t) this->mclk_pin_;
  tx_cfg.gpio_cfg.bclk = (gpio_num_t) this->bclk_pin_;
  tx_cfg.gpio_cfg.ws = (gpio_num_t) this->lrclk_pin_;
  tx_cfg.gpio_cfg.dout = (gpio_num_t) this->duplex_dout_pin_;
  tx_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;

  err = i2s_channel_init_std_mode(this->duplex_tx_handle_, &tx_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Duplex: TX-STD-Init fehlgeschlagen: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  // RX dauerhaft aktiv: Takt fuer den gesamten Bus (beide Codecs) steht ab jetzt
  err = i2s_channel_enable(this->duplex_rx_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Duplex: RX-Enable fehlgeschlagen: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  if (this->duplex_slot_monitor_) {
    xTaskCreate(I2SAudioComponent::duplex_slot_monitor_task, "slot_mon", 4096, this, 5, nullptr);
  }
}

void I2SAudioComponent::duplex_slot_monitor_task(void *param) {
  // Bring-up-Diagnose: liest den RX-Strom und loggt sekuendlich den Pegel je
  // TDM-Slot. Gemessene Zuordnung am Waveshare-Board: Slot 0/2 = Mikrofone,
  // Slot 1 = Echo-Referenz (korreliert mit Wiedergabe), Slot 3 unbenutzt.
  auto *bus = static_cast<I2SAudioComponent *>(param);
  const uint32_t slots = bus->duplex_total_slots_;
  const size_t frames_per_read = DUPLEX_DMA_FRAME_NUM;                       // 10 ms
  const size_t bytes_per_read = frames_per_read * slots * sizeof(int16_t);  // 16-bit-Slots
  std::vector<int16_t> buffer(frames_per_read * slots);
  std::vector<uint64_t> sum_squares(slots, 0);
  // Nulldurchgangszaehlung je Slot: bei einem Sinus-Testton ist
  // Frequenz = Durchgaenge / 2 / Fensterdauer - deckt Tonhoehenfehler
  // (falsche Abspielrate, Kanal-Versatz) auf, die im RMS unsichtbar sind
  std::vector<int16_t> prev_sample(slots, 0);
  std::vector<uint32_t> zero_crossings(slots, 0);
  // Spitzenwert je Slot: Crest-Faktor (Peak/RMS) unterscheidet Sinus (3 dB)
  // von Rechteck (0 dB) - RMS und Nulldurchgaenge koennen das nicht
  std::vector<int32_t> peak(slots, 0);
  uint32_t frames_accumulated = 0;

  while (true) {
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(bus->duplex_rx_handle_, buffer.data(), bytes_per_read, &bytes_read, portMAX_DELAY);
    if (err != ESP_OK || bytes_read == 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    const size_t frames = bytes_read / (slots * sizeof(int16_t));
    for (size_t f = 0; f < frames; f++) {
      for (uint32_t s = 0; s < slots; s++) {
        const int16_t v16 = buffer[f * slots + s];
        const int32_t v = v16;
        sum_squares[s] += (uint64_t) (v * v);
        const int32_t mag = (v < 0) ? -v : v;
        if (mag > peak[s]) {
          peak[s] = mag;
        }
        if ((v16 < 0) != (prev_sample[s] < 0)) {
          zero_crossings[s]++;
        }
        prev_sample[s] = v16;
      }
    }
    frames_accumulated += frames;
    if (frames_accumulated >= bus->duplex_sample_rate_) {  // ~1 s Fenster
      float rms_dbfs[8] = {0};
      float freq_hz[8] = {0};
      float crest_db[8] = {0};
      const float window_s = (float) frames_accumulated / (float) bus->duplex_sample_rate_;
      for (uint32_t s = 0; s < slots && s < 8; s++) {
        const float rms = sqrtf((float) sum_squares[s] / (float) frames_accumulated);
        rms_dbfs[s] = 20.0f * log10f((rms + 1.0f) / 32768.0f);
        freq_hz[s] = (float) zero_crossings[s] / 2.0f / window_s;
        crest_db[s] = 20.0f * log10f(((float) peak[s] + 1.0f) / (rms + 1.0f));
        sum_squares[s] = 0;
        zero_crossings[s] = 0;
        peak[s] = 0;
      }
      frames_accumulated = 0;
      ESP_LOGI(TAG,
               "Slot-Pegel [dBFS]: S0=%.1f S1=%.1f S2=%.1f S3=%.1f | f[Hz]: %.0f %.0f %.0f %.0f | Crest[dB]: %.1f "
               "%.1f %.1f %.1f",
               rms_dbfs[0], rms_dbfs[1], rms_dbfs[2], rms_dbfs[3], freq_hz[0], freq_hz[1], freq_hz[2], freq_hz[3],
               crest_db[0], crest_db[1], crest_db[2], crest_db[3]);
    }
  }
}

void I2SAudioComponent::dump_config() {
  if (!this->duplex_enabled_) {
    return;
  }
  ESP_LOGCONFIG(TAG,
                "I2S-Duplex-TDM-Bus:\n"
                "  Sample Rate: %" PRIu32 " Hz\n"
                "  Slots: %u x %u bit (%u BCLK/Frame)\n"
                "  TX-Slot-Maske: 0x%02X, RX-Slot-Maske: 0x%02X\n"
                "  MCLK: %" PRIu32 " Hz",
                this->duplex_sample_rate_, this->duplex_total_slots_, this->duplex_slot_bits_,
                (unsigned) (this->duplex_total_slots_ * this->duplex_slot_bits_),
                (unsigned) this->duplex_tx_slot_mask_, (unsigned) this->duplex_rx_slot_mask_,
                this->duplex_sample_rate_ * (uint32_t) this->duplex_mclk_multiple_);
}

}  // namespace esphome::i2s_audio

#endif  // USE_ESP32
