# re:Lain Android

Native Android port of the *Serial Experiments Lain* PlayStation
decompilation, based on the original work by Y0oshi.

## Version 0.2.0 — Wired Workstation

### Requirements

- Android 6.0 or newer
- ARM64 (`arm64-v8a`)
- OpenGL ES 3.0
- Original PlayStation disc dumps supplied by the player

### Installation

Download the APK from
[GitHub Releases](https://github.com/VandalsEmpire/lain-psx-decompiled-android/releases).

Import Disc 1 in the launcher using a raw `.bin` or `.img`
data track with 2352-byte sectors. Import Disc 2 when needed.

CHD and 2048-byte ISO imports are not supported.

### Wired Workstation launcher

The Android launcher provides access to game discs, mods,
configuration, subtitles and the Archive.

The launcher has English and Portuguese UI text.
Some interface elements may still appear in English.

### Gameplay and stability

- Virtual PlayStation controls with adjustable layout
- Physical gamepad support
- Return to launcher without closing the application
- Restart a game session from the launcher
- Game audio is stopped when returning to the launcher

### Subtitles

The launcher can download, install and select community
subtitle packs.

Supported translated subtitle languages:

- Portuguese (Brazil) — `pt-BR`
- English — `en`
- Spanish — `es-ES`
- French — `fr`
- German — `de`

An alternative English ASS subtitle pack is also available.

Original Japanese game audio and content are unchanged.
Support for non-Latin subtitle scripts is deferred.

Subtitle data comes from the community repositories
[laingame-net/lainvtt](https://github.com/laingame-net/lainvtt)
and [laingame-net/lainass](https://github.com/laingame-net/lainass).

Subtitle files are downloaded on request, not bundled
with the APK.

### Archive

The Archive reads unused and rare content from the user's
own imported disc images.

Features include:

- Image and animation previews
- Audio previews
- Individual PNG image export
- Batch ZIP export, including animation frames

No original game assets are embedded in the application.

### Mods

The launcher supports importing and managing mods,
including ZIP-based installation.

Mods can be enabled, disabled and removed.
Platform-specific native plugins must target Android ARM64.

See [MODDING.md](../port/MODDING.md).

### Building

Required toolchain:

- JDK 17
- Android SDK Platform 36
- Android NDK 28.2.13676358
- CMake 3.31.6
- Gradle 9.6.0
- Android Gradle Plugin 9.5.0

Build a debug APK from the repository root:

```sh
gradle -p android assembleDebug
```

For a distributable release APK, configure the original
release signing key in `android/keystore.properties`,
then run:

```sh
gradle -p android assembleRelease
```

Never commit release signing keys or passwords.

### Known limitations

- ARM64 devices only
- Landscape orientation
- Non-Latin subtitle fonts are not supported in this release
- Android device compatibility has not been exhaustively tested
- 60 fps interpolation is disabled due to rendering artifacts

### Legal

No PlayStation disc images, movies, sounds, textures or other
copyrighted game assets are distributed.

This project is not affiliated with the rights holders of
*Serial Experiments Lain*.

See the repository [MIT License](../LICENSE) and
[third-party notices](../thirdparty/README.md).
