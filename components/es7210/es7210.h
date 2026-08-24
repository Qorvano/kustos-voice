#pragma once

#include "esphome/components/audio_adc/audio_adc.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

#include "es7210_const.h"

namespace esphome::es7210 {

enum ES7210BitsPerSample : uint8_t {
  ES7210_BITS_PER_SAMPLE_16 = 16,
  ES7210_BITS_PER_SAMPLE_18 = 18,
  ES7210_BITS_PER_SAMPLE_20 = 20,
  ES7210_BITS_PER_SAMPLE_24 = 24,
  ES7210_BITS_PER_SAMPLE_32 = 32,
};

class ES7210 : public audio_adc::AudioAdc, public Component, public i2c::I2CDevice {
  /* Class for configuring an ES7210 ADC for microphone input.
   * Based on code from:
   * - https://github.com/espressif/esp-bsp/ (accessed 20241219)
   * - https://github.com/espressif/esp-adf/ (accessed 20241219)
   */
 public:
  void setup() override;
  void dump_config() override;

  void set_bits_per_sample(ES7210BitsPerSample bits_per_sample) { this->bits_per_sample_ = bits_per_sample; }
  bool set_mic_gain(float mic_gain) override;
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }
  // TDM-Modus: alle vier ADC-Kanaele als Slots 0-3 auf SDOUT1 (fuer AFE-Capture)
  void set_enable_tdm(bool enable_tdm) { this->enable_tdm_ = enable_tdm; }
  // 1-4 = dieser Kanal ist die Echo-Referenz (Line-Pegel) und bleibt fest auf 0 dB;
  // 0 = kein Referenzkanal, alle Kanaele bekommen den User-Gain
  void set_reference_channel(uint8_t channel) { this->reference_channel_ = channel; }

  float mic_gain() override { return this->mic_gain_; };

 protected:
  /// @brief Updates an I2C registry address by modifying the current state
  /// @param reg_addr I2C register address
  /// @param update_bits Mask of allowed bits to be modified
  /// @param data Bit values to be written
  /// @return True if successful, false otherwise
  bool es7210_update_reg_bit_(uint8_t reg_addr, uint8_t update_bits, uint8_t data);

  /// @brief Convert floating point mic gain value to register value
  /// @param mic_gain Gain value to convert
  /// @return Corresponding register value for specified gain
  uint8_t es7210_gain_reg_value_(float mic_gain);

  bool configure_i2s_format_();
  bool configure_mic_gain_();
  bool configure_sample_rate_();

  bool setup_complete_{false};
  bool enable_tdm_{false};
  uint8_t reference_channel_{0};
  float mic_gain_{0};
  ES7210BitsPerSample bits_per_sample_{ES7210_BITS_PER_SAMPLE_16};
  uint32_t sample_rate_{0};
};

}  // namespace esphome::es7210
