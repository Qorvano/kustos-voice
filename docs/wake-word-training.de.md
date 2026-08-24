# Eigenes Wake Word trainieren

*English version: [wake-word-training.md](wake-word-training.md)*

kustos-voice akzeptiert jedes microWakeWord-Modell. Welche Modelle geladen werden,
steht in `kustos-voice.yaml` unter `micro_wake_word: models:`; der Eintrag mit der
id `wake_word_primary` ist das Hauptmodell, dessen Erkennungsschwelle der Slider
"Wake-Word-Schwelle" in Home Assistant live steuert.

## Fertige Modelle einsetzen

Eintrag in der eigenen Geraete-YAML aendern (Override per `packages`/`micro_wake_word`), danach kompilieren und per OTA flashen:

```yaml
micro_wake_word:
  models:
    - model: okay_nabu          # eingebaute Modellnamen: okay_nabu, hey_jarvis, hey_mycroft
      id: wake_word_primary
    # oder jede Modell-JSON-URL:
    # - model: https://example.com/mein_wort.json
    #   id: wake_word_primary
```

## Eigenes Wort trainieren (microWakeWord, Apache 2.0)

Der offizielle Trainingsweg von [kahrendt/microWakeWord](https://github.com/kahrendt/microWakeWord):

1. **Trainings-Notebook oeffnen**: Im Repo liegt ein Colab-/Jupyter-Notebook
   (`notebooks/`), das den kompletten Ablauf kapselt. Google Colab mit GPU reicht.
2. **Zielwort festlegen**: Das Notebook erzeugt mit einem TTS-Sample-Generator
   (Piper) tausende synthetische Aussprachen des Zielworts plus Negativbeispiele.
   Faustregeln: 3-4 Silben funktionieren deutlich besser als kurze Woerter,
   und das Wort sollte im Alltag nicht beilaeufig fallen.
3. **Training laufen lassen**: je nach Colab-GPU einige Stunden. Ergebnis sind
   eine `.tflite`-Datei (quantisiertes Modell) und eine Manifest-`.json`.
4. **Modell hosten**: beide Dateien in ein eigenes GitHub-Repo oder einen
   HTTP-Server legen. Die JSON referenziert die tflite relativ, beide Dateien
   gehoeren nebeneinander.
5. **Einbinden**: JSON-URL wie oben als `wake_word_primary` eintragen,
   kompilieren, OTA flashen.
6. **Kalibrieren**: mit dem Schwellen-Slider in Home Assistant den Kompromiss
   aus Empfindlichkeit und Fehlausloesungen einstellen. Startwert 0.83; im Log
   (`esphome logs`) zeigt jede Erkennung die erreichte Wahrscheinlichkeit.

## Grenzen

Die Erkennungsqualitaet haengt vom akustischen Pfad ab (Mikrofonoeffnungen im
Gehaeuse, Abstand, Hall). Mit aktivem esp-sr-Audio-Front-End (AEC gegen die
Echo-Referenz des Boards) funktioniert Zuruf in laufende Musik am
Referenzgeraet auf rund 5 Metern; ohne Front-End bricht die Reichweite auf
unter einen Meter ein.
