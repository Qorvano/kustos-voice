# Hinweise zu Drittanbieter-Bestandteilen / Third-party notices

**English summary**: kustos-voice is licensed under the GPLv3 (see LICENSE).
The vendored components in `components/` are based on ESPHome 2026.6.5 (C++
GPLv3, Python MIT - MIT text in [LICENSES/ESPHOME-MIT.txt](LICENSES/ESPHOME-MIT.txt));
`CODEOWNERS` entries in these files name the maintainer of this fork, not the
original authors. Wake word models are Apache 2.0 (kahrendt/microWakeWord and
OHF-Voice), sounds are MIT (esphome/home-assistant-voice-pe). **Espressif's
esp-sr** (fetched at build time when the `afe:` block is configured) is
licensed under an MIT-like license with a hardware clause that is NOT
GPLv3-compatible: building and using the firmware yourself is fine, but
**compiled binaries containing esp-sr must not be redistributed**. The
detailed notices below are in German.

kustos-voice steht unter der GPLv3 (siehe LICENSE). Folgende Bestandteile
stammen von Dritten oder bauen auf fremder Arbeit auf.
`CODEOWNERS`-Eintraege in uebernommenen Dateien benennen den Maintainer
dieses Forks, nicht die urspruenglichen Autoren; die Urheberschaft der
Upstream-Anteile bleibt bei den ESPHome-Autoren. Der MIT-Text fuer die
Python-Anteile liegt unter [LICENSES/ESPHOME-MIT.txt](LICENSES/ESPHOME-MIT.txt).

## ESPHome (Komponenten-Basis)

`components/i2s_audio/`, `components/es8311/` und `components/es7210/` basieren
auf den gleichnamigen Komponenten von [ESPHome](https://esphome.io)
(Stand 2026.6.5). ESPHome-Lizenz: C++/Runtime-Code GPLv3, Python-Code MIT,
Copyright (c) 2019 ESPHome.

Eigene Aenderungen gegenueber Upstream:
- `i2s_audio`: neuer `duplex:`-Modus - der Bus besitzt beide Kanaele eines
  I2S-Controllers (Vollduplex), RX laeuft dauerhaft als Clock-Master im
  4-Slot-TDM, TX im Standard-I2S-Format (16-in-32, MSB an der WS-Flanke fuer
  den ES8311-Slave); der Speaker leiht sich den TX-Kanal vom Bus. Zusaetzlich
  schreibt der Speaker-Task jeden DMA-Puffer in genau einem
  `i2s_channel_write` (Deskriptor-Splice-Fix, haelt auch die
  Wiedergabe-Zeitstempel fuer die AEC-Referenz exakt).
- `es8311`: das Lautstaerke-Mapping des DAC ist geaendert -
  Regler linear in dB ueber 45 dB Spanne mit Maximum +2 dB (Upstream mappt
  linear auf den vollen Registerbereich inklusive +32 dB Digital-Gain und
  uebersteuert damit oben hart). Herleitung aus dem ES8311-Datenblatt und
  Messung am Geraet. Ausserdem ist `mclk_multiple` konfigurierbar und der
  Referenztakt wird beim Start geloggt.
- `es7210`: neue Optionen `tdm:` (4 ADC-Kanaele in einem TDM-Frame) und
  `reference_channel:` (der Echo-Referenz-Kanal bleibt fest auf 0 dB,
  unabhaengig vom Mikrofon-Gain). Dazu ein Fix der REG01-Takt-Gates: der
  Upstream-Treiber gibt nur die ADC1/2-Takte frei (Maske 0x0b), ADC3/4
  brauchen zusaetzlich Maske 0x15 (Quelle: Espressif esp-adf,
  es7210_mic_select).

## microWakeWord-Modelle

Die Wake-Word-Modelle (hey_jarvis, okay_nabu, stop) stammen aus
[kahrendt/microWakeWord](https://github.com/kahrendt/microWakeWord) bzw.
[OHF-Voice/micro-wake-word](https://github.com/OHF-Voice/micro-wake-word),
Lizenz Apache 2.0. Sie werden beim Kompilieren geladen und in die Firmware
eingebettet.

## Sounds

`timer_finished.flac` und `wake_word_triggered.flac` stammen aus
[esphome/home-assistant-voice-pe](https://github.com/esphome/home-assistant-voice-pe)
(MIT-Teil der ESPHome-Lizenz).

## Espressif esp-sr (Audio-Front-End)

`components/afe_mic/` bindet bei konfiguriertem `afe:`-Block zur Buildzeit
[espressif/esp-sr](https://github.com/espressif/esp-sr) (^2.4.6, ueber den
IDF Component Manager) samt esp-dsp (^1.8.0) ein. esp-sr steht unter einer
MIT-aehnlichen Espressif-Lizenz mit Hardware-Klausel (Nutzung nur auf
Espressif-Chips). Diese Klausel ist mit der GPLv3 dieses Projekts nicht
kompatibel; die Verteilung des QUELLCODES ist unkritisch (esp-sr wird erst
beim Kompilieren durch den Nutzer bezogen), aber **vorkompilierte Binaries
mit einkompiliertem esp-sr duerfen nicht verbreitet werden** (fuer einen
Web-Installer ist eine AFE-freie Variante zu bauen).

Die AFE-Anbindung (API-Vertrag afe_config_init/feed/fetch_with_delay)
orientiert sich an der Komponente `esp_afe` aus
[n-IA-hane/esphome-audio-stack](https://github.com/n-IA-hane/esphome-audio-stack)
(MIT); der Code in `components/afe_mic/` ist eine Eigenentwicklung.

## Marken / Trademarks

"Waveshare" wird ausschliesslich beschreibend zur Kompatibilitaetsangabe
genannt; dieses Projekt steht in keiner Verbindung zu Waveshare.
"Waveshare" is used descriptively for compatibility only; this project is
not affiliated with Waveshare.
