re:Lain Android update — touch layout, Android frame timing and English subtitles

APPLY
1. Extract this archive into the ROOT of your existing lain-psx-decompiled-android repository.
2. Allow the files to overwrite the previous Android implementation files.
3. Build with the same Gradle/SDK setup that produced the working APK:

   "$HOME/.local/opt/gradle-9.6.0/bin/gradle" --no-daemon --console=plain -p android assembleDebug

4. Install without uninstalling the previous debug APK, so your imported discs and app data stay in place:

   adb install -r android/app/build/outputs/apk/debug/re-Lain-debug.apk

WHAT CHANGED
- Redesigned Android touch overlay: cross-shaped D-pad, PlayStation symbols and classic button colours, L1/L2/R1/R2 at the top, Select/Start centered, persistent MENU pill.
- Disabled 60 fps in-between-frame interpolation for Android, including old settings.ini values. Desktop builds keep the existing option.
- Added a Subtitles launcher tab that downloads and safely extracts the completed English fan subtitle archive from laingame-net/lainass. Internet permission is used only for this download.
- Added port/mod_examples/android-touch-test, a small Lua HUD smoke-test mod.

The update preserves the existing re:Lain label/icon, disc importer and Android Mods tab. It contains no game disc data and does not bundle third-party subtitles. The full Gradle/NDK build and on-device behavior still need validation on the Fedora machine.
