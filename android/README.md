# Android port - implementation notes

This directory is the Android work area. The first goal is a debug APK that can
boot the existing native client with the player's own two disc images. The game
must not bundle copyrighted disc data.

## Confirmed starting points

- `port/psx/include/PsyX/PsyX_render.h` already selects the OpenGL ES 3 renderer
  when `__ANDROID__` is defined.
- `port/psx/src/render/PsyX_render.cpp` already has an OpenGL ES context path.
- The client currently builds a desktop executable (`add_executable(lain ...)`)
  and uses desktop OpenGL/ImGui setup code. This is not yet an Android app.
- The current launcher uses native desktop file dialogs. Android needs a
  Storage Access Framework (SAF) picker and persistent access to selected files.
- The native settings/data directory is based on `SDL_GetPrefPath`, which is a
  suitable starting point for app-private persistent data.
- The SDL GameController layer already handles physical controllers. Touch
  controls are a separate input overlay and are not implemented yet.

## Planned milestones

1. **Build target:** add an Android Gradle/NDK application target that packages
   SDL2 and loads the port as an Android shared library. Keep desktop targets
   unchanged.
2. **First boot:** create an Android launcher that requests the two disc images
   with SAF, imports them into app-private storage, and starts the game.
3. **Graphics/audio:** validate GLES3 shader paths, framebuffer formats, audio
   initialization and lifecycle handling (pause/resume/context recreation).
4. **Input:** add a landscape touch overlay for D-pad, face buttons, Start and
   Select. Keep SDL physical controllers working and allow the overlay to be
   hidden when a controller is connected.
5. **PT-BR:** import the separate `laingame-net/lainvtt` `pt-BR` WebVTT set as
   the native SRT subtitle pack. The conversion helper lives in
   `tools/import_webvtt_pack.py`.
6. **Display polish:** preserve the original 4:3 game image first. Consider
   true widescreen only after the Android build is stable.

## Current status

This is planning/scaffolding, not an installable APK. The Gradle project and
Android lifecycle/input bridge still need to be implemented and tested on a
physical device.

## PT-BR subtitle import

Clone the translation repository separately, then run from the root of this
repository:

```sh
python3 tools/import_webvtt_pack.py \
  --input /path/to/lainvtt/pt-BR \
  --output /path/to/lain-user-data/lang/pt-BR
```

The output is a native pack folder containing `pack.txt` and
`subtitles/index.txt` plus one `.srt` per source `.vtt`. Node basenames are
preserved so native track lookup can continue to use names such as `Cou001`
and `Lda001`.
