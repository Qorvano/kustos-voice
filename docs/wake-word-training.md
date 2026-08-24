# Training your own wake word

*Deutsche Version: [wake-word-training.de.md](wake-word-training.de.md)*

kustos-voice accepts any microWakeWord model. The loaded models are listed in
`kustos-voice.yaml` under `micro_wake_word: models:`; the entry with the id
`wake_word_primary` is the primary model, whose detection threshold the
"Wake word threshold" slider in Home Assistant controls live.

## Using prebuilt models

Change the entry in your device YAML, then compile and flash over the air:

```yaml
micro_wake_word:
  models:
    - model: okay_nabu          # built-in model names: okay_nabu, hey_jarvis, hey_mycroft
      id: wake_word_primary
    # or any model JSON URL:
    # - model: https://example.com/my_word.json
    #   id: wake_word_primary
```

## Training your own word (microWakeWord, Apache 2.0)

The official training path from [kahrendt/microWakeWord](https://github.com/kahrendt/microWakeWord):

1. **Open the training notebook**: the repository ships a Colab/Jupyter
   notebook (`notebooks/`) that wraps the whole flow. Google Colab with a GPU
   is sufficient.
2. **Pick a target phrase**: the notebook generates thousands of synthetic
   pronunciations of the target phrase (plus negatives) with a TTS sample
   generator (Piper). Rules of thumb: 3-4 syllables work much better than
   short words, and the phrase should not come up casually in daily speech.
3. **Run the training**: a few hours depending on the Colab GPU. The result
   is a `.tflite` file (quantized model) and a manifest `.json`.
4. **Host the model**: put both files into a GitHub repo or any HTTP server.
   The JSON references the tflite relatively; both files belong side by side.
5. **Wire it up**: enter the JSON URL as `wake_word_primary` as shown above,
   compile, flash over the air.
6. **Calibrate**: use the threshold slider in Home Assistant to balance
   sensitivity against false accepts. Start at 0.83; the device log
   (`esphome logs`) prints the achieved probability for every detection.

## Limits

Detection quality depends on the acoustic path (microphone openings in the
enclosure, distance, reverb). With the esp-sr audio front-end enabled (AEC
against the board's echo reference), barge-in into running music works at
around 5 meters on the reference device; without the front-end the range
drops below one meter.
