re:Lain Android update: touchscreen controls + Mods tab

Apply this archive on top of the Android-port working tree that already builds
and runs with the disc-image picker. The Java Activity and Android launcher
files in this archive include that existing disc-picker integration.

1. Back up the project folder.
2. Extract this ZIP into the repository root, preserving directory paths and
   allowing the files in the archive to replace the existing ones.
3. Build from Fedora with the same Android toolchain:

   cd "$HOME/lain-droid/lain-psx-decompiled-android"
   "$HOME/.local/opt/gradle-9.6.0/bin/gradle" --no-daemon --console=plain -p android assembleDebug

4. Install the resulting APK:

   "$HOME/Android/Sdk/platform-tools/adb" install -r \
     android/app/build/outputs/apk/debug/re-Lain-debug.apk

Touch controls:
- Enabled by default on Android. D-pad, four face buttons, L1/L2, R1/R2,
  Select and Start feed the first PlayStation controller directly.
- The MENU pill in the upper-right opens the in-game menu. It remains available
  even when the gameplay buttons are hidden.
- Toggle gameplay buttons in F1 > Controls > Show touch controls.

Mods:
- Open the launcher Mods tab > Import mod folder.
- Pick one extracted mod directory. mod.ini is optional; without it, the
  directory name is used. ZIP files must be extracted before import.
- The app copies the mod to its private mods directory and enables it by
  default. Refresh, enable/disable, inspect, and reorder mods in the tab.
- Native plugins need an Android arm64 .so build. Desktop DLL/dylib plugins
  cannot load on Android.

The update contains no game discs or copyrighted game data. It also does not
modify the user's original disc/mod folders. The Android APK still needs a
full Gradle/NDK build and device testing after applying this update.
