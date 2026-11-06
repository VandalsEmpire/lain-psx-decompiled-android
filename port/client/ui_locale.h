/* Launcher locale: Android system preference or user override (C++17). */
#pragma once
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

/* 0 = system, 1 = English, 2 = Portuguese (Brazil). */
inline int &reLain_ui_language_choice() {
    static int choice = 0;
    return choice;
}

inline bool reLain_ui_system_pt() {
#if defined(__ANDROID__)
    static const bool pt = [] {
        SDL_Locale *locales = SDL_GetPreferredLocales();
        bool found = false;
        if (locales) {
            if (locales[0].language && std::strcmp(locales[0].language, "pt") == 0)
                found = true;
            SDL_free(locales);
        }
        return found;
    }();
    return pt;
#else
    return false;
#endif
}

inline bool reLain_ui_pt() {
    const int choice = reLain_ui_language_choice();
    return choice == 2 || (choice == 0 && reLain_ui_system_pt());
}

inline const char *reLain_ui_text(const char *en, const char *pt) {
    return reLain_ui_pt() ? pt : en;
}

inline std::string reLain_ui_language_path(const char *data_dir) {
    return std::string(data_dir ? data_dir : "") + "launcher_language.cfg";
}

inline void reLain_ui_language_load(const char *data_dir) {
    reLain_ui_language_choice() = 0;
    FILE *f = std::fopen(reLain_ui_language_path(data_dir).c_str(), "rb");
    if (!f) return;
    int choice = 0;
    if (std::fscanf(f, "%d", &choice) == 1 && choice >= 0 && choice <= 2)
        reLain_ui_language_choice() = choice;
    std::fclose(f);
}

inline bool reLain_ui_language_save(const char *data_dir, int choice) {
    if (choice < 0 || choice > 2) return false;
    FILE *f = std::fopen(reLain_ui_language_path(data_dir).c_str(), "wb");
    if (!f) return false;
    const bool written = std::fprintf(f, "%d\n", choice) > 0;
    const bool closed = std::fclose(f) == 0;
    const bool ok = written && closed;
    if (ok) reLain_ui_language_choice() = choice;
    return ok;
}
