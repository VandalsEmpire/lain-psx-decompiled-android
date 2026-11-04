/* Shared launcher locale choice: English by default, Portuguese on pt devices. */
#pragma once
#include <SDL.h>
#include <string.h>

inline bool reLain_ui_pt(void) {
#if defined(__ANDROID__)
    static const bool pt = [] {
        SDL_Locale *locales = SDL_GetPreferredLocales();
        bool found = false;
        if (locales) {
            for (SDL_Locale *p = locales; p->language; ++p) {
                if (strcmp(p->language, "pt") == 0) { found = true; break; }
            }
            SDL_free(locales);
        }
        return found;
    }();
    return pt;
#else
    return false;
#endif
}
inline const char *reLain_ui_text(const char *en, const char *pt) {
    return reLain_ui_pt() ? pt : en;
}
