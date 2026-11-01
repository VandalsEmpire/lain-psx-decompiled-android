# Initial Android-port contribution

Files to add to the `android-port` branch of
`VandalsEmpire/lain-psx-decompiled-android`:

- `android/README.md` - verified starting points, milestones and known blockers.
- `tools/import_webvtt_pack.py` - converts the existing browser PT-BR WebVTT
  subtitle tracks into the native client's SRT pack format.
- `tools/tests/test_import_webvtt_pack.py` - tests timestamps, cue parsing,
  markup cleanup and pack naming.

No game data, BIOS, disc images or proprietary assets are included.

Test:

```sh
python3 -m unittest tools.tests.test_import_webvtt_pack -v
```
