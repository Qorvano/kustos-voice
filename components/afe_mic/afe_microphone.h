#pragma once

#ifdef USE_ESP32

#include "esphome/components/i2s_audio/i2s_audio.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/core/component.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <string>
#include <vector>

#ifdef USE_AFE_MIC_ESP_SR
#include <esp_afe_config.h>
#include <esp_afe_sr_iface.h>
#include <esp_afe_sr_models.h>
#endif

namespace esphome::afe_mic {

// Mikrofon am Vollduplex-TDM-Bus: liest den dauerhaft laufenden RX-Kanal des
// Busses (alle ADC-Slots), dezimiert auf die Zielrate der Sprachverarbeitung
// und liefert Mono an die Konsumenten. Zwei Betriebsarten:
//  - Passthrough: der gewaehlte Mikrofon-Slot wird direkt durchgereicht
//  - AFE (mit afe:-Block konfiguriert): alle Slots laufen durch Espressifs
//    Audio-Front-End (AEC gegen die Echo-Referenz, BSS ueber beide Kapseln,
//    AGC); der AFE-Ausgang ist der Mono-Strom.
// Der Modus ist zur Laufzeit umschaltbar (set_afe_enabled), damit A/B-Tests
// ohne Neuflash moeglich sind.
class AfeMicrophone : public microphone::Microphone, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void start() override;
  void stop() override;
  void loop() override;

  void set_i2s_bus(i2s_audio::I2SAudioComponent *bus) { this->bus_ = bus; }
  void set_input_format(const std::string &fmt) { this->input_format_ = fmt; }
  void set_passthrough_channel(uint8_t ch) { this->passthrough_channel_ = ch; }

#ifdef USE_AFE_MIC_ESP_SR
  void set_afe_configured(bool aec, bool bss, bool agc) {
    this->afe_configured_ = true;
    this->afe_aec_ = aec;
    this->afe_bss_ = bss;
    this->afe_agc_ = agc;
  }
  // Laufzeit-Umschaltung AFE <-> Passthrough (A/B-Tests ohne Neuflash)
  void set_afe_enabled(bool enabled) { this->afe_active_.store(enabled, std::memory_order_relaxed); }
  bool is_afe_enabled() const { return this->afe_active_.load(std::memory_order_relaxed); }
#endif

 protected:
  static void capture_task(void *params);

  i2s_audio::I2SAudioComponent *bus_{nullptr};
  std::string input_format_;
  uint8_t passthrough_channel_{0};

  SemaphoreHandle_t active_listeners_semaphore_{nullptr};
  EventGroupHandle_t event_group_{nullptr};
  TaskHandle_t task_handle_{nullptr};

#ifdef USE_AFE_MIC_ESP_SR
  static void fetch_task(void *params);
  bool setup_afe_();

  bool afe_configured_{false};
  bool afe_aec_{true};
  bool afe_bss_{true};
  bool afe_agc_{true};
  std::atomic<bool> afe_active_{false};

  const esp_afe_sr_iface_t *afe_iface_{nullptr};
  esp_afe_sr_data_t *afe_data_{nullptr};
  int afe_feed_chunk_{0};     // Samples je Kanal pro feed()
  int afe_feed_channels_{0};  // vom AFE erwartete Kanalzahl
  TaskHandle_t fetch_task_handle_{nullptr};
#endif
};

}  // namespace esphome::afe_mic

#endif  // USE_ESP32
