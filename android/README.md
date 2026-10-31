# Android port - first native build

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

The APK output is `app/build/outputs/apk/debug/app-debug.apk`.

## Current scope

The app is landscape-only and builds the native client as `libmain.so` linked
to SDL2, OpenAL Soft and GLESv3. The Android setup screen is a temporary
launcher: it displays the app-private data directory and only enables Play
when `disc1.bin` is present. The Android Storage Access Framework picker and
copy/import workflow are not implemented yet. Do not expect a playable install
until that picker is added and the app is tested on a device with the user's
own disc images.

Touchscreen gameplay controls, pause/resume validation, PT-BR pack install
integration and widescreen rendering remain subsequent milestones. The
original game display remains 4:3 for now.
