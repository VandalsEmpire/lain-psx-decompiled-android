/* The launcher's Mods tab. */
#pragma once

#include "settings.h"

/* Draws the tab's page; the mod order and on/off state go to s->mods. */
void mods_tab_draw(Settings *s);
#if defined(__ANDROID__)
/* Receives the asynchronous result of the Android Storage Access Framework importer. */
extern "C" void mods_android_import_result(const char *folder_path, const char *message);
/* True while the Android picker or background importer is active. */
extern "C" bool mods_android_import_busy(void);
#endif

/* Launcher-only summary. Enabled means configured to load at the next Play;
 * not proof of successful Lua/plugin initialization while the game runs. */
struct ModsUiCounts {
    int installed = 0;
    int enabled = 0;
};
ModsUiCounts mods_ui_counts(const Settings *s);
