# kustos-voice

*English version: [README.md](README.md)*

ESPHome-Firmware, die aus dem **Waveshare ESP32-S3-AUDIO**-Board einen
vollwertigen Sprachassistenten für Home Assistant macht: **Voice-Satellit,
Music-Assistant-Lautsprecher und Ansagen mit automatischem Ducking auf einem
Gerät**, bei 48-kHz-Wiedergabe und gleichzeitig aktivem On-Device-Wake-Word.

Der Audiopfad nutzt einen Vollduplex-TDM-Bus und Espressifs
**esp-sr-Audio-Front-End**: Echounterdrückung gegen eine Hardware-
Echo-Referenz, Quellentrennung über beide Mikrofonkapseln und automatische
Pegelregelung. Am Referenzgerät gemessen: Zuruf in laufende Musik, der vorher
ab einem halben Meter scheiterte, funktioniert mit Front-End auf **rund 5
Metern**.

## Features

- microWakeWord on-device; Modelle frei konfigurierbar, auch selbst
  trainierte (siehe [docs/wake-word-training.de.md](docs/wake-word-training.de.md))
- esp-sr-Audio-Front-End (AEC + BSS + AGC) mit Laufzeit-Schalter in Home
  Assistant für A/B-Vergleiche
- Live-Regler in Home Assistant: Wake-Word-Schwelle (wirkt auf alle
  wählbaren Modelle), Mikrofon-Verstärkung, STT-Rauschunterdrückung,
  Aktivierungston
- Media-Player mit getrennter Medien- und Ansage-Pipeline (Ansagen ducken
  Musik statt sie zu stoppen)
- Sprach-Timer mit Klingelton und "Stopp"-Wort
- Dezente monochrome LED-Statusanzeige (Akzentfarbe frei wählbar, Idle dunkel)
- Drei Hardware-Tasten (leiser / Play-Pause / lauter),
  Mikrofon-Stummschaltung mit sichtbarer Datenschutz-Anzeige
- Bedienelemente standardmäßig englisch; die vollständige deutsche
  Übersetzung liegt als [kustos-voice-de.yaml](kustos-voice-de.yaml) bei

## Hardware

Das empfohlene Board (und Referenzgerät) für dieses Projekt ist das
[Waveshare ESP32-S3-AUDIO](https://www.waveshare.com/wiki/ESP32-S3-AUDIO):
ESP32-S3R8, ES8311-DAC, ES7210-Vierfach-ADC mit zwei Mikrofonkapseln und
Hardware-Echo-Referenz, 7x-WS2812-Ring, drei Tasten. Dazu ein kleiner
Lautsprecher am Verstärkerausgang (4-8 Ohm) und ein Gehäuse nach Wahl.

## Installation (ESPHome Builder)

Voraussetzung: Home Assistant mit dem
[ESPHome-Builder](https://esphome.io/guides/getting_started_hassio.html)-Add-on.

1. Im ESPHome Builder ein neues Gerät anlegen (jedes ESP32-S3-Board im
   Wizard ist ok, das Package setzt die korrekten Board-Einstellungen). Die
   erzeugte YAML unverändert lassen - samt gerätespezifischem
   API-Verschlüsselungs-Key, `ota:`- und Fallback-Hotspot-Block - und nur
   einen Block ergänzen:

   ```yaml
   packages:
     kustos_voice: github://Qorvano/kustos-voice/kustos-voice-de.yaml@main
   ```

   Für englische Bedienelement-Namen stattdessen
   `github://Qorvano/kustos-voice/kustos-voice.yaml@main` verwenden.

2. Als geteilte Secrets braucht das Package nur die WLAN-Zugangsdaten in der
   `secrets.yaml` des Builders (meist ohnehin vorhanden; siehe
   [secrets.yaml.example](secrets.yaml.example)):

   ```yaml
   wifi_ssid: "..."
   wifi_password: "..."
   ```

3. Installieren. Beim Erstflash "Plug into this computer" wählen und per USB
   aus dem Browser flashen; danach läuft jedes Update über OTA.

4. Das gefundene Gerät in Home Assistant hinzufügen und dem Satelliten eine
   Pipeline mit Spracherkennung zuweisen.

Der Build lädt dieses Repository und Espressifs esp-sr-Komponente auf deinen
Rechner und kompiliert dort. Es wird nichts als fertiges Binary verteilt;
warum, steht im Lizenz-Abschnitt.

### Build ohne esp-sr

Wer den `microphone: ... afe:`-Block und den `${name_afe}`-Schalter aus
seiner Kopie der Config entfernt, baut eine reine Passthrough-Firmware ohne
esp-sr-Abhängigkeit. Der weiträumige Zuruf in Musik entfällt, alles andere
funktioniert identisch.

## Bedienelemente in Home Assistant

| Entität | Zweck |
|---|---|
| Audio-Front-End | Laufzeit-Schalter: esp-sr-Verarbeitung vs. rohes Mikrofon |
| Wake-Word-Schwelle | Erkennungsschwelle, live, für alle wählbaren Modelle |
| Mikrofon-Verstärkung | Analog-Gain des ES7210 (die Echo-Referenz bleibt fix) |
| Rauschunterdrückung STT | 0-4, wirkt nur auf den Spracherkennungs-Pfad |
| Aktivierungston | Hörbare Bestätigung jeder Wake-Word-Erkennung |
| Mikrofon stumm | Software-Stummschaltung mit dezenter Datenschutz-Anzeige |
| Verstärker | Freigabe des Lautsprecherverstärkers |

## Struktur

```
kustos-voice.yaml             Haupt-Config (englische Bedienelement-Namen)
kustos-voice-de.yaml          deutsches Namens-Overlay
packages/hardware_duplex.yaml Pins, I2C, Vollduplex-TDM-Bus, Codecs, Verstaerker
packages/audio_duplex.yaml    afe_mic-Mikrofon, Lautsprecherkette, Media-Player
packages/voice.yaml           Voice Assistant, Wake-Word-Behandlung, Timer
packages/leds.yaml            LED-Zustandslogik
packages/controls.yaml        Tasten, Mute, Regler
components/                   gepatchte/eigene Komponenten (Basis ESPHome, siehe NOTICE.md)
```

Code-Kommentare sind deutsch (Arbeitssprache des Autors); Konfigurations-
Oberflaeche und Doku sind zweisprachig.

## Fehlerbehebung

- **Wake Word wird erkannt, aber Antworten bleiben stumm, Satellit hängt in
  "Sprachausgabe"**: Home Assistant hat die Audio-Formate einer früheren
  Firmware zwischengespeichert und liefert TTS als MP3 (diese Firmware
  dekodiert FLAC). Einmal den ESPHome-Integrationseintrag in Home Assistant
  neu laden.
- **Erkennungsqualität**: über den Schwellen-Slider abstimmen; jede
  Erkennung protokolliert ihre Wahrscheinlichkeit im Geräte-Log.

## Lizenz

Das Projekt steht unter der **GPLv3** (siehe [LICENSE](LICENSE)).

Das esp-sr-Audio-Front-End wird zur Buildzeit bezogen und steht bei
Espressif unter einer MIT-ähnlichen Lizenz mit Hardware-Klausel, die nicht
GPLv3-kompatibel ist. Selbst bauen und nutzen ist unkritisch;
**vorkompilierte Binaries mit esp-sr dürfen nicht weiterverbreitet werden**.
Details und alle Drittanbieter-Hinweise: [NOTICE.md](NOTICE.md).

"Waveshare" wird ausschließlich beschreibend zur Kompatibilitätsangabe
genannt; dieses Projekt steht in keiner Verbindung zu Waveshare.
