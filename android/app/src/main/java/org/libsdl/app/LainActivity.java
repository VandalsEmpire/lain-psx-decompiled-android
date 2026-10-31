package org.libsdl.app;

/**
 * Application entry point. SDLActivity owns the Android lifecycle and JNI
 * bridge; game-specific Android services (SAF disc picker, etc.) will be added
 * here without forking SDL's upstream Activity implementation.
 */
public final class LainActivity extends SDLActivity {
}
