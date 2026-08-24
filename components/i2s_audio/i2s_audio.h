#pragma once

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include <esp_idf_version.h>
#include <driver/i2s_std.h>
#include <driver/i2s_tdm.h>

namespace esphome::i2s_audio {

class I2SAudioComponent;

class I2SAudioBase : public Parented<I2SAudioComponent> {
 public:
  void set_i2s_role(i2s_role_t role) { this->i2s_role_ = role; }
  void set_slot_mode(i2s_slot_mode_t slot_mode) { this->slot_mode_ = slot_mode; }
  void set_std_slot_mask(i2s_std_slot_mask_t std_slot_mask) { this->std_slot_mask_ = std_slot_mask; }
  void set_slot_bit_width(i2s_slot_bit_width_t slot_bit_width) { this->slot_bit_width_ = slot_bit_width; }
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }
  void set_use_apll(uint32_t use_apll) { this->use_apll_ = use_apll; }
  void set_mclk_multiple(i2s_mclk_multiple_t mclk_multiple) { this->mclk_multiple_ = mclk_multiple; }

 protected:
  i2s_role_t i2s_role_{};
  i2s_slot_mode_t slot_mode_;
  i2s_std_slot_mask_t std_slot_mask_;
  i2s_slot_bit_width_t slot_bit_width_;
  uint32_t sample_rate_;
  bool use_apll_;
  i2s_mclk_multiple_t mclk_multiple_;
};

class I2SAudioIn : public I2SAudioBase {};

class I2SAudioOut : public I2SAudioBase {};

class I2SAudioComponent : public Component {
 public:
  i2s_std_gpio_config_t get_pin_config() const {
    return {.mclk = (gpio_num_t) this->mclk_pin_,
            .bclk = (gpio_num_t) this->bclk_pin_,
            .ws = (gpio_num_t) this->lrclk_pin_,
            .dout = I2S_GPIO_UNUSED,  // add local ports
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            }};
  }

  void set_mclk_pin(int pin) { this->mclk_pin_ = pin; }
  void set_bclk_pin(int pin) { this->bclk_pin_ = pin; }
  void set_lrclk_pin(int pin) { this->lrclk_pin_ = pin; }
  void set_port(int port) { this->port_ = port; }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  int get_port() const { return this->port_; }
#else
  i2s_port_t get_port() const { return static_cast<i2s_port_t>(this->port_); }
#endif

  void lock() { this->lock_.lock(); }
  bool try_lock() { return this->lock_.try_lock(); }
  void unlock() { this->lock_.unlock(); }

  // --- Vollduplex-TDM-Modus -------------------------------------------------
  // Der Bus besitzt beide Kanaele eines Controllers (ein einziger
  // i2s_new_channel-Aufruf, nur so koppelt der IDF-Treiber TX und RX an
  // dieselben Takte). RX wird ZUERST initialisiert und dauerhaft enabled:
  // der zuerst initialisierte Kanal ist Clock-Master, damit besitzt das
  // permanent laufende Mikrofon den Takt und der Speaker-TX darf beliebig
  // starten/stoppen, ohne MCLK/BCLK/LRCK zu beruehren.
  void set_duplex_enabled(bool enabled) { this->duplex_enabled_ = enabled; }
  void set_duplex_dout_pin(int pin) { this->duplex_dout_pin_ = pin; }
  void set_duplex_din_pin(int pin) { this->duplex_din_pin_ = pin; }
  void set_duplex_sample_rate(uint32_t rate) { this->duplex_sample_rate_ = rate; }
  void set_duplex_slot_bits(uint8_t bits) { this->duplex_slot_bits_ = bits; }
  void set_duplex_total_slots(uint8_t slots) { this->duplex_total_slots_ = slots; }
  void set_duplex_tx_slot_mask(uint32_t mask) { this->duplex_tx_slot_mask_ = mask; }
  void set_duplex_rx_slot_mask(uint32_t mask) { this->duplex_rx_slot_mask_ = mask; }
  void set_duplex_use_apll(bool use_apll) { this->duplex_use_apll_ = use_apll; }
  void set_duplex_mclk_multiple(i2s_mclk_multiple_t multiple) { this->duplex_mclk_multiple_ = multiple; }
  // Bring-up-Diagnose: sekuendliches RMS-Log je TDM-Slot.
  // Exklusiv - nicht zusammen mit einem RX-Konsumenten (afe_mic) verwenden.
  void set_duplex_slot_monitor(bool enabled) { this->duplex_slot_monitor_ = enabled; }

  bool is_duplex() const { return this->duplex_enabled_; }
  i2s_chan_handle_t get_duplex_tx_handle() const { return this->duplex_tx_handle_; }
  i2s_chan_handle_t get_duplex_rx_handle() const { return this->duplex_rx_handle_; }
  uint32_t get_duplex_sample_rate() const { return this->duplex_sample_rate_; }
  uint8_t get_duplex_slot_bits() const { return this->duplex_slot_bits_; }
  uint8_t get_duplex_rx_slot_count() const;
  bool is_duplex_slot_monitor_enabled() const { return this->duplex_slot_monitor_; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  static void duplex_slot_monitor_task(void *param);

  Mutex lock_;

  bool duplex_enabled_{false};
  int duplex_dout_pin_{I2S_GPIO_UNUSED};
  int duplex_din_pin_{I2S_GPIO_UNUSED};
  uint32_t duplex_sample_rate_{48000};
  uint8_t duplex_slot_bits_{16};
  uint8_t duplex_total_slots_{4};
  uint32_t duplex_tx_slot_mask_{0};
  uint32_t duplex_rx_slot_mask_{0};
  bool duplex_use_apll_{true};
  i2s_mclk_multiple_t duplex_mclk_multiple_{I2S_MCLK_MULTIPLE_256};
  bool duplex_slot_monitor_{false};
  i2s_chan_handle_t duplex_tx_handle_{nullptr};
  i2s_chan_handle_t duplex_rx_handle_{nullptr};

  I2SAudioIn *audio_in_{nullptr};
  I2SAudioOut *audio_out_{nullptr};
  int mclk_pin_{I2S_GPIO_UNUSED};
  int bclk_pin_{I2S_GPIO_UNUSED};
  int lrclk_pin_;
  int port_{};
};

}  // namespace esphome::i2s_audio

#endif  // USE_ESP32
