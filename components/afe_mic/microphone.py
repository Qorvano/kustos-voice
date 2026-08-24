import esphome.codegen as cg
from esphome.components import audio, microphone
from esphome.components.esp32 import add_idf_component
from esphome.components.i2s_audio import I2SAudioComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@Qorvano"]
DEPENDENCIES = ["i2s_audio"]

CONF_I2S_AUDIO_ID = "i2s_audio_id"
CONF_INPUT_FORMAT = "input_format"
CONF_PASSTHROUGH_CHANNEL = "passthrough_channel"
CONF_AFE = "afe"
CONF_AEC = "aec"
CONF_BSS = "bss"
CONF_AGC = "agc"

# Muss zur gleichnamigen Konstante in afe_microphone.cpp passen (Zielrate der
# Sprachverarbeitung: micro_wake_word/voice_assistant sind 16-kHz-only)
TARGET_RATE_HZ = 16000
OUTPUT_BITS = 16
OUTPUT_CHANNELS = 1

afe_mic_ns = cg.esphome_ns.namespace("afe_mic")
AfeMicrophone = afe_mic_ns.class_("AfeMicrophone", microphone.Microphone, cg.Component)


def _validate_input_format(value):
    value = cv.string_strict(value).upper()
    if not value or any(c not in "MRN" for c in value):
        raise cv.Invalid(
            "input_format beschreibt die RX-Slots des Busses und darf nur "
            "M (Mikrofon), R (Echo-Referenz) und N (unbenutzt) enthalten"
        )
    if "M" not in value:
        raise cv.Invalid("input_format braucht mindestens einen M-Slot")
    return value


def _validate_passthrough(config):
    fmt = config[CONF_INPUT_FORMAT]
    if CONF_PASSTHROUGH_CHANNEL not in config:
        # Standard: der erste Mikrofon-Slot
        config[CONF_PASSTHROUGH_CHANNEL] = fmt.index("M")
    ch = config[CONF_PASSTHROUGH_CHANNEL]
    if ch >= len(fmt) or fmt[ch] != "M":
        raise cv.Invalid(
            f"passthrough_channel {ch} ist kein M-Slot in '{fmt}' (0-basiert)"
        )
    return config


def _set_stream_limits(config):
    audio.set_stream_limits(
        min_bits_per_sample=OUTPUT_BITS,
        max_bits_per_sample=OUTPUT_BITS,
        min_channels=OUTPUT_CHANNELS,
        max_channels=OUTPUT_CHANNELS,
        min_sample_rate=TARGET_RATE_HZ,
        max_sample_rate=TARGET_RATE_HZ,
    )(config)
    return config


# Espressifs Audio-Front-End (AEC gegen die Echo-Referenz, BSS ueber die
# Mikrofonkapseln, AGC). Nur wenn dieser Block konfiguriert ist, wird esp-sr
# in den Build gezogen; ohne ihn bleibt afe_mic ein reiner Passthrough.
AFE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_AEC, default=True): cv.boolean,
        cv.Optional(CONF_BSS, default=True): cv.boolean,
        cv.Optional(CONF_AGC, default=True): cv.boolean,
    }
)

CONFIG_SCHEMA = cv.All(
    microphone.MICROPHONE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(AfeMicrophone),
            cv.GenerateID(CONF_I2S_AUDIO_ID): cv.use_id(I2SAudioComponent),
            cv.Required(CONF_INPUT_FORMAT): _validate_input_format,
            cv.Optional(CONF_PASSTHROUGH_CHANNEL): cv.int_range(min=0, max=15),
            cv.Optional(CONF_AFE): AFE_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_passthrough,
    _set_stream_limits,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await microphone.register_microphone(var, config)

    bus = await cg.get_variable(config[CONF_I2S_AUDIO_ID])
    cg.add(var.set_i2s_bus(bus))
    cg.add(var.set_input_format(config[CONF_INPUT_FORMAT]))
    cg.add(var.set_passthrough_channel(config[CONF_PASSTHROUGH_CHANNEL]))

    if CONF_AFE in config:
        afe = config[CONF_AFE]
        cg.add_define("USE_AFE_MIC_ESP_SR")
        cg.add(var.set_afe_configured(afe[CONF_AEC], afe[CONF_BSS], afe[CONF_AGC]))
        # esp-sr braucht esp-dsp >= 1.8.0; beide Pins nach dem Muster der
        # MIT-lizenzierten Referenz n-IA-hane/esphome-audio-stack (NOTICE.md)
        add_idf_component(name="espressif/esp-dsp", ref="^1.8.0")
        add_idf_component(name="espressif/esp-sr", ref="^2.4.6")
