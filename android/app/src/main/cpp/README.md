# Android native build

The app's external CMake build starts at android/CMakeLists.txt, which
includes the existing port/CMakeLists.txt. SDL2 is downloaded and SHA-256
verified by settings.gradle before CMake configuration. The SDL Android Java
classes and resources are compiled from that same pinned source archive.

The Android native library is named main because that is the library SDL's
Android Activity loads after SDL2.
