/* The launcher's Mods tab. */
#pragma once

#include "settings.h"

/* Draws the tab's page; the mod order and on/off state go to s->mods. */
void mods_tab_draw(Settings *s);
#if defined(__ANDROID__)
/* Receives the asynchronous result of the Android Storage Access Framework importer. */
extern "C" void mods_android_import_result(const char *folder_path, const char *message);
#endif
