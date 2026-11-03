re:Lain Android — editable touch layout, larger mobile UI, official save icon

APPLY
1. Extract this archive into the ROOT of your existing lain-psx-decompiled-android repository.
2. Allow the files to overwrite the existing files.
3. Build with the same Fedora Android toolchain used for the previous working APK:

   "$HOME/.local/opt/gradle-9.6.0/bin/gradle" --no-daemon --console=plain -p android assembleDebug

4. Install over the current debug APK (do not uninstall; this preserves your imported discs
   and the current app-private data):

   adb install -r android/app/build/outputs/apk/debug/re-Lain-debug.apk

WHAT CHANGED
- F1 > Controls now has Edit touch layout, Reset touch layout, and a touch-size slider.
- In layout mode, drag D-pad, action-button cluster, L1/L2/R1/R2, and Select/Start groups.
  Tap DONE in the upper-right corner to save. Positions/size persist in settings.ini.
- The launcher uses Android display density to enlarge text, spacing, and hit targets.
  Its tab page is scrollable; the Android Mods list and details are stacked vertically.
- The app icon uses the static first pose of the source project's docs/assets/save-icon.svg,
  including an adaptive monochrome mask. The SVG itself remains in the source repository.

FILES
- port/client/overlay.cpp
- port/client/settings.c
- port/client/settings.h
- port/client/setup_ui_android.cpp
- port/client/mods_ui.cpp
- android/README.md
- android/app/src/main/res/drawable-nodpi/lain_launcher_{foreground,master,monochrome}.png
- android/app/src/main/res/mipmap-{mdpi,hdpi,xhdpi,xxhdpi,xxxhdpi}/lain_launcher.png

VALIDATION
- Android-specific C++ syntax checks passed for overlay.cpp, setup_ui_android.cpp and mods_ui.cpp.
- settings.c passed a save/load round-trip test for all touch positions and size.
- This environment does not have the Fedora Gradle/JDK/Android SDK toolchain, so a full
  Gradle/NDK APK build still needs to be run on the Fedora machine.
