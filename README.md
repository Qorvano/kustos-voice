# kustos-voice

*Deutsche Version: [README.de.md](README.de.md)*

ESPHome firmware that turns the **Waveshare ESP32-S3-AUDIO** board into a
full-featured voice assistant for Home Assistant: **voice satellite, Music
Assistant speaker, and announcements with automatic ducking on a single
device**, with 48 kHz playback and on-device wake word running at the same
time.

The audio path uses a full-duplex TDM bus and Espressif's **esp-sr audio
front-end**: acoustic echo cancellation against a hardware echo reference,
blind source separation across both microphone capsules, and automatic gain
control. Measured on the reference device: music barge-in that used to fail
beyond half a meter works at **around 5 meters** with the front-end enabled.

## Features

- microWakeWord on-device; models are freely configurable, including your own
  (see [docs/wake-word-training.md](docs/wake-word-training.md))
- esp-sr audio front-end (AEC + BSS + AGC) with a runtime on/off switch in
  Home Assistant for A/B comparison
- Live controls in Home Assistant: wake word threshold (applies to all
  selectable models), microphone gain, STT noise suppression, wake sound
- Media player with separate media and announcement pipelines (announcements
  duck music instead of stopping it)
- Voice timers with ring tone and "stop" wake word
- Subtle monochrome LED status ring (accent color selectable, idle is dark)
- Three hardware buttons (volume down / play-pause / volume up), microphone
  mute with a visible privacy indicator
- All user-facing entity names in English by default; a complete German
  translation ships as [kustos-voice-de.yaml](kustos-voice-de.yaml)

## Hardware

The recommended (and reference) board for this project is the
[Waveshare ESP32-S3-AUDIO](https://www.waveshare.com/wiki/ESP32-S3-AUDIO):
ESP32-S3R8, ES8311 DAC, ES7210 quad ADC with two mic capsules and a hardware
echo reference, 7x WS2812 ring, three buttons. Add a small speaker on the
amplifier output (4-8 ohms) and any enclosure you like.

## Installation (ESPHome Builder)

Prerequisites: Home Assistant with the
[ESPHome Builder](https://esphome.io/guides/getting_started_hassio.html)
add-on.

1. In the ESPHome Builder, create a new device (any ESP32-S3 board is fine,
   the package pins the correct board settings). Keep the wizard-generated
   YAML as it is - including its per-device API encryption key, `ota:` and
   fallback-hotspot blocks - and just add one block:

   ```yaml
   packages:
     kustos_voice: github://Qorvano/kustos-voice/kustos-voice.yaml@main
   ```

   For German entity names use
   `github://Qorvano/kustos-voice/kustos-voice-de.yaml@main` instead.

2. The only shared secrets the package needs are your Wi-Fi credentials in
   the builder's `secrets.yaml` (usually already there; see
   [secrets.yaml.example](secrets.yaml.example)):

   ```yaml
   wifi_ssid: "..."
   wifi_password: "..."
   ```

3. Install. For the first flash choose "Plug into this computer" and flash
   over USB from the browser; every update afterwards works over the air.

4. Add the discovered device in Home Assistant, then set the satellite's
   assistant to a pipeline that supports speech-to-text.

The build fetches this repository and Espressif's esp-sr component onto your
machine and compiles the firmware there. Nothing is downloaded as a prebuilt
binary; see the license section for why.

### Building without esp-sr

Remove the `microphone: ... afe:` block and the `${name_afe}` switch in your
own copy of the config to build a passthrough-only firmware without the
esp-sr dependency. You lose the long-range music barge-in; everything else
works identically.

## Controls in Home Assistant

| Entity | Purpose |
|---|---|
| Audio front-end | Runtime switch: esp-sr processing vs. raw microphone |
| Wake word threshold | Detection threshold, live, for all selectable models |
| Microphone gain | ES7210 analog gain (the echo reference stays fixed) |
| STT noise suppression | 0-4, affects only the speech-to-text path |
| Wake sound | Audible confirmation for each wake word detection |
| Mute microphone | Software mute with a dim privacy glow on the ring |
| Amplifier | Speaker amplifier enable |

## Structure

```
kustos-voice.yaml             main config (English entity names)
kustos-voice-de.yaml          German entity-name overlay
packages/hardware_duplex.yaml pins, I2C, full-duplex TDM bus, codecs, amplifier
packages/audio_duplex.yaml    afe_mic microphone, speaker chain, media player
packages/voice.yaml           voice assistant, wake word handling, timers
packages/leds.yaml            LED state logic
packages/controls.yaml        buttons, mute, sliders
components/                   patched/own components (base ESPHome, see NOTICE.md)
```

Code comments are written in German (the author's working language); the
configuration surface and docs are bilingual.

## Troubleshooting

- **Wake word is detected but replies are silent, satellite stuck in
  "responding"**: Home Assistant cached the device's audio formats from a
  previous firmware and serves TTS as MP3 (this firmware decodes FLAC).
  Reload the ESPHome integration entry in Home Assistant once.
- **A wake word is detected but the ring just flashes twice** (also when
  adding a second wake word): the assigned assistant pipeline, often the
  "Preferred" one, has no speech-to-text engine. Assign a pipeline with STT
  to that wake word slot, or make an STT-capable pipeline the preferred one.
- **Detection quality**: tune the wake word threshold slider; every
  detection logs its probability in the device log.

## License

The project is licensed under the **GPLv3** (see [LICENSE](LICENSE)).

The esp-sr audio front-end is fetched at build time and is licensed by
Espressif under an MIT-like license with a hardware restriction that is not
compatible with the GPLv3. Building and using the firmware yourself is fine;
**do not redistribute compiled binaries that contain esp-sr**. Details and
all third-party attributions: [NOTICE.md](NOTICE.md).

"Waveshare" is used descriptively for compatibility only; this project is
not affiliated with Waveshare.
