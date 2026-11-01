# re:Lain for Android

This milestone adds an Android Gradle/NDK application target around the
existing native client. It uses the existing PsyCross Android path (OpenGL ES
3) and the SDL2 Android Activity/JNI bridge. The SDL2 2.32.10 source archive
is downloaded on Gradle sync and SHA-256 verified; it is not copied into this
repository.

## Toolchain

- JDK 17
- Android SDK Platform 36 and Build Tools 36.0.0
- Android NDK 28.2.13676358
- CMake 3.31.6
- Gradle 9.6.0 / Android Gradle Plugin 9.5.0

Only arm64-v8a is enabled in this first device build.

## Build

Open this `android/` folder in Android Studio and allow Gradle sync to finish.
The first sync downloads the pinned SDL2 release. Then build the `debug`
variant, or run `gradle assembleDebug` from this folder with Gradle 9.6.0.

The debug APK output is `app/build/outputs/apk/debug/re-Lain-debug.apk`.
The installed app is displayed as **re:Lain**; the package ID remains
`com.vandalsempire.lain` (debug builds append `.debug`).

## Current scope

The app is landscape-only and builds the native client as `libmain.so` linked
to SDL2, OpenAL Soft and GLESv3. The Android launcher uses the system document
picker to import the player's own raw disc data tracks. It copies the selected
file into app-private storage, hashes and identifies Disc 1 or Disc 2, then
stores it as `disc1.bin` or `disc2.bin`. No broad storage permission is needed.

On Android, select the raw `.bin` or `.img` data track (2352 bytes per sector),
not the `.cue` sheet. Disc 1 is required to enable Play; Disc 2 is optional
until the game asks for it. CHD and 2048-byte ISO images are not supported by
this importer. Convert CHD to a raw BIN/CUE dump first, then select the BIN
data track.

Imported discs stay in the app-private data folder. Uninstalling the app
removes that private copy, so keep the original disc dumps elsewhere.

## Touch controls

Gameplay has an on-screen PlayStation control overlay enabled by default on
Android: D-pad, Triangle, Square, Circle, Cross, L1/L2, R1/R2, Select and
Start. Touch **MENU** in the upper-right corner to open the in-game F1 menu.
The gameplay buttons can be hidden or shown again in **F1 → Controls → Show
touch controls**; the small **MENU** control remains on-screen so it can always
be reopened. A physical gamepad remains supported and can be used alongside
the touch controls.

## Mods

The launcher has a **Mods** tab. Choose **Import mod folder** and select the
folder of one mod (the folder containing its files). `mod.ini` is optional;
when absent, the folder name is used. Extract `.zip` archives in the file
manager before importing. The app copies the folder into its private `mods/`
directory and does not modify the selected source folder.
Imported mods are enabled by default. The tab lets you refresh the list,
enable/disable mods, inspect validation results, and change load order before
pressing **Play**. The existing mod runtime also applies data mods, Lua scripts,
HD texture packs, and native plugins when the project supports their target
platform. A mod's native plugin must be compiled for Android/arm64 to load on
this APK; Windows `.dll` or macOS `.dylib` files will not work on Android.

Keep the original mod folders elsewhere as backups. The imported copy is
removed when the app is uninstalled. Consult [`../port/MODDING.md`](../port/MODDING.md)
for the supported mod folder layout and file formats.

Pause/resume validation and widescreen rendering remain subsequent milestones.
The game display remains 4:3 for now. The launcher interface is still in English;
the PT-BR download below provides translated game subtitles, not a translated UI.

## Android touch layout and frame interpolation

Gameplay has a translucent PlayStation-style touch controller. Its D-pad is a
single cross, the four face buttons use the original PlayStation symbols and
colours, and L1/L2/R1/R2 sit along the top edge. Select and Start are centred
below the play area. The **MENU** pill remains available even when gameplay
controls are hidden. Android disables 60 fps frame interpolation because it
causes rendering artifacts on some devices; the original frame cadence and
VSync remain available.

## English subtitles

The launcher has a **Subtitles** tab with a button to download the completed
English fan subtitle set from the public `laingame-net/lainass` repository.
The subtitle files are fetched from their authors and are not bundled in the
APK. After installation, start the game and choose **English (laingame.net fan
subtitles)** in **F1 → Settings → Text / subtitles**. Internet access is used
only when the player starts this download.

## Portuguese (Brazil) subtitles

The same **Subtitles** tab offers a PT-BR download from the community
[`lainvtt/pt-BR`](https://github.com/laingame-net/lainvtt/tree/main/pt-BR) set.
The Android client downloads the VTT tracks, converts valid cues to the native
SRT pack format, and installs them as `lang/pt-BR/`. After installation,
select **Português brasileiro** in **F1 → Settings → Text / subtitles**.
The translation files are fetched when requested and are not bundled in the APK.

## Unused-content gallery

The Android launcher includes the same **Unused** gallery as the desktop client.
It uses the shared `unused_gallery.cpp` decoder to read the player's imported
Disc 1 and optional Disc 2 images. It displays the unused Lain animations,
photos and sounds, as well as rare content that the game only shows under
special conditions. Animated pictures are decoded from the original disc data
and played in the gallery; sound samples can be previewed with **Play audio**.

The first opening decodes the disc data on a background thread, so the page may
show a loading message. Disc 1 is required. Import Disc 2 and choose **Reload
gallery** to include the additional Site B entry. No game pictures, animations
or audio are bundled in the APK; the gallery reads only the user's own imported
disc images.

## Android mod smoke test

The folder `port/mod_examples/android-touch-test` is a harmless Lua mod used
to verify the Android import and runtime path. Import that folder from the
launcher **Mods** tab; while playing, it draws a small HUD saying the mod is
active. It can be disabled or removed like any other mod.

## Phone layout and launcher icon update

- In-game Android controls can be repositioned from `F1 > Controls > Edit touch layout`.
  Drag the D-pad, action-button cluster, L1/L2/R1/R2 and Start/Select groups, then tap
  `DONE`. `Touch control size` scales the whole overlay, and `Reset touch layout` restores
  the defaults. Positions and scale are saved in `settings.ini`.
- The Android launcher UI scales its fonts and touch targets using the display density;
  tab pages scroll, and the Mods list/details are stacked vertically on Android to make
  them easier to use on a phone.
- The launcher icon now uses the pixel-art save icon already present at
  [`docs/assets/save-icon.svg`](https://github.com/Y0oshi/lain-psx-decompiled/blob/205c1977e2aa8d26128fcdbab19170c2c2bbde0e/docs/assets/save-icon.svg).
  Android launcher icons are static, so the app uses its first visible pose, with a safe
  margin for adaptive icon masks; the monochrome mask is generated from the same artwork.


### Touch layout editor

On Android, open F1 > Controls > Edit touch layout. The four D-pad directions
(Up, Down, Left, Right) and the four face buttons (Triangle, Square, Circle,
Cross) are independent draggable elements; L1/L2/R1/R2 and Select/Start are
also individually movable. Tap DONE to save. Older saved 8-group touch layouts
are migrated automatically to the new per-button coordinates.
