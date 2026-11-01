#include "settings.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *settings_data_dir(void) {
    static char *dir;
    if (!dir && getenv("LAIN_DATA_DIR")) {
        /* tests: a separate data folder (settings, discs, memory card, packs) */
        const char *d = getenv("LAIN_DATA_DIR");
        size_t n = strlen(d);
        dir = SDL_malloc(n + 2);
        memcpy(dir, d, n + 1);
        if (n && d[n - 1] != '/' && d[n - 1] != '\\') {
            dir[n] = '/';
            dir[n + 1] = 0;
        }
    }
    if (!dir) {
        /* %APPDATA%\LainNative\lain-native\, ~/Library/Application Support/LainNative/lain-native/, ... */
        dir = SDL_GetPrefPath("LainNative", "lain-native");
        if (!dir) {
            dir = SDL_strdup("./");
        }
    }
    return dir;
}

const char *const SETTINGS_KEY_NAMES[SETTINGS_NUM_KEYS] = {
    "up", "down", "left", "right", "cross", "circle", "square", "triangle",
    "l1", "l2", "l3", "r1", "r2", "r3", "start", "select",
};

static const char *const DEFAULT_KEYS[SETTINGS_NUM_KEYS] = {
    "Up", "Down", "Left", "Right", "C", "V", "X", "Z",
    "Left Shift", "Left Ctrl", "[", "Right Shift", "Right Ctrl", "]", "Return", "Space",
};

const char *settings_default_key(int button) {
    return button >= 0 && button < SETTINGS_NUM_KEYS ? DEFAULT_KEYS[button] : "";
}

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof *s);
    s->fullscreen = 0;
    s->width = 1280;
    s->height = 960;
    s->render_scale = 0; /* auto: the window's resolution */
    s->vsync = 1;
#if defined(__ANDROID__)
    s->smooth = 0; /* frame interpolation currently causes rendering artifacts on Android */
    s->touch_controls = 1;
#else
    s->smooth = 1;
    s->touch_controls = 0;
#endif
    s->volume = 100;
    /* Every directional and face button has its own normalized position. */
    const float default_touch_layout[14][2] = {
        {0.150f, 0.674f}, {0.150f, 0.836f}, {0.105f, 0.755f}, {0.195f, 0.755f}, /* D-pad */
        {0.850f, 0.670f}, {0.803f, 0.755f}, {0.897f, 0.755f}, {0.850f, 0.840f}, /* △ □ ○ × */
        {0.115f, 0.190f}, {0.295f, 0.190f}, {0.705f, 0.190f}, {0.885f, 0.190f}, /* L2 L1 R1 R2 */
        {0.435f, 0.910f}, {0.565f, 0.910f}, /* Select Start */
    };
    memcpy(s->touch_layout, default_touch_layout, sizeof default_touch_layout);
    s->touch_scale = 1.0f;
    strcpy(s->text_lang, "ja");
    strcpy(s->voice_lang, "ja");
    for (int i = 0; i < SETTINGS_NUM_KEYS; i++) {
        snprintf(s->keys[i], sizeof s->keys[i], "%s", DEFAULT_KEYS[i]);
    }
}

static void settings_path(char *out, size_t cap) {
    snprintf(out, cap, "%ssettings.ini", settings_data_dir());
}

void settings_load(Settings *s) {
    settings_defaults(s);
    char path[1024], line[2200];
    settings_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char key[64], val[2048];
        if (sscanf(line, " %63[^= ] = %2047[^\r\n]", key, val) != 2) {
            continue;
        }
        if (!strcmp(key, "fullscreen")) s->fullscreen = atoi(val);
        else if (!strcmp(key, "width")) s->width = atoi(val);
        else if (!strcmp(key, "height")) s->height = atoi(val);
        else if (!strcmp(key, "render_scale")) s->render_scale = atoi(val);
        else if (!strcmp(key, "vsync")) s->vsync = atoi(val);
        else if (!strcmp(key, "smooth")) s->smooth = atoi(val);
        else if (!strcmp(key, "volume")) s->volume = atoi(val);
        else if (!strcmp(key, "cheat_open_nodes")) s->cheat_open_nodes = atoi(val);
        else if (!strcmp(key, "turbo")) s->turbo = atoi(val);
        else if (!strcmp(key, "cheat_genome")) s->cheat_genome = atoi(val);
        else if (!strcmp(key, "text_lang")) snprintf(s->text_lang, sizeof s->text_lang, "%s", val);
        else if (!strcmp(key, "voice_lang")) snprintf(s->voice_lang, sizeof s->voice_lang, "%s", val);
        else if (!strcmp(key, "setup_done")) s->setup_done = atoi(val);
        else if (!strcmp(key, "mods")) snprintf(s->mods, sizeof s->mods, "%s", val);
        else if (!strcmp(key, "dump_textures")) s->dump_textures = atoi(val);
        else if (!strcmp(key, "touch_controls")) s->touch_controls = atoi(val) != 0;
        else if (!strcmp(key, "touch_scale")) s->touch_scale = (float)atof(val);
        else if (!strcmp(key, "touch_layout")) {
            float values[28];
            int count = 0;
            char *cursor = val;
            while (count < 28 && *cursor) {
                char *end = NULL;
                float value = strtof(cursor, &end);
                if (end == cursor) break;
                values[count++] = value;
                cursor = end;
                if (*cursor == ',' || *cursor == ';') cursor++;
                else if (*cursor != '\0') break;
            }
            if (count == 28) {
                for (int i = 0; i < 14; i++) {
                    s->touch_layout[i][0] = values[i * 2];
                    s->touch_layout[i][1] = values[i * 2 + 1];
                }
            } else if (count == 16) {
                /* Migrate the previous 8-group layout without losing user placement. */
                const float dx_dpad = 0.045f, dy_dpad = 0.081f;
                const float dx_face = 0.047f, dy_face = 0.085f;
                const float dpx = values[0], dpy = values[1];
                const float fcx = values[2], fcy = values[3];
                const float migrated[14][2] = {
                    {dpx, dpy - dy_dpad}, {dpx, dpy + dy_dpad},
                    {dpx - dx_dpad, dpy}, {dpx + dx_dpad, dpy},
                    {fcx, fcy - dy_face}, {fcx - dx_face, fcy},
                    {fcx + dx_face, fcy}, {fcx, fcy + dy_face},
                    {values[4], values[5]}, {values[6], values[7]},
                    {values[8], values[9]}, {values[10], values[11]},
                    {values[12], values[13]}, {values[14], values[15]},
                };
                memcpy(s->touch_layout, migrated, sizeof migrated);
            }
            /* If the line is incomplete or malformed, retain the defaults. */
        }
        else if (!strncmp(key, "key_", 4)) {
            for (int i = 0; i < SETTINGS_NUM_KEYS; i++) {
                if (!strcmp(key + 4, SETTINGS_KEY_NAMES[i])) {
                    snprintf(s->keys[i], sizeof s->keys[i], "%s", val);
                }
            }
        }
    }
    fclose(f);
    if (s->width < 320) s->width = 320;
    if (s->height < 240) s->height = 240;
    if (s->render_scale < 0 || s->render_scale > 8) s->render_scale = 0;
    if (s->volume < 0 || s->volume > 100) s->volume = 100;
    if (s->touch_scale < 0.70f || s->touch_scale > 1.55f) s->touch_scale = 1.0f;
    const float default_touch_layout[14][2] = {
        {0.150f, 0.674f}, {0.150f, 0.836f}, {0.105f, 0.755f}, {0.195f, 0.755f},
        {0.850f, 0.670f}, {0.803f, 0.755f}, {0.897f, 0.755f}, {0.850f, 0.840f},
        {0.115f, 0.190f}, {0.295f, 0.190f}, {0.705f, 0.190f}, {0.885f, 0.190f},
        {0.435f, 0.910f}, {0.565f, 0.910f},
    };
    for (int i = 0; i < 14; i++) {
        if (s->touch_layout[i][0] < 0.02f || s->touch_layout[i][0] > 0.98f ||
            s->touch_layout[i][1] < 0.02f || s->touch_layout[i][1] > 0.98f) {
            s->touch_layout[i][0] = default_touch_layout[i][0];
            s->touch_layout[i][1] = default_touch_layout[i][1];
        }
    }
#if defined(__ANDROID__)
    s->smooth = 0; /* ignore a previously saved desktop setting */
#endif
    if (s->turbo < 0 || s->turbo > 8 || s->turbo == 1) s->turbo = 0;
}

void settings_save(const Settings *s) {
    char path[1024];
    settings_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "; Lain native client settings\n");
#if defined(__ANDROID__)
    const int smooth = 0;
#else
    const int smooth = s->smooth;
#endif
    fprintf(f, "fullscreen = %d\nwidth = %d\nheight = %d\nrender_scale = %d\nvsync = %d\n"
               "smooth = %d\nvolume = %d\n",
            s->fullscreen, s->width, s->height, s->render_scale, s->vsync, smooth, s->volume);
    fprintf(f, "; cheats\ncheat_open_nodes = %d\nturbo = %d\ncheat_genome = %d\n", s->cheat_open_nodes, s->turbo,
            s->cheat_genome);
    fprintf(f, "text_lang = %s\nvoice_lang = %s\nsetup_done = %d\n", s->text_lang, s->voice_lang,
            s->setup_done);
    fprintf(f, "; mods in load order: +on / -off\nmods = %s\ndump_textures = %d\ntouch_controls = %d\n",
            s->mods, s->dump_textures, s->touch_controls);
    fprintf(f, "touch_scale = %.3f\ntouch_layout = ", s->touch_scale);
    for (int i = 0; i < 14; i++) {
        fprintf(f, "%s%.4f,%.4f", i ? ";" : "", s->touch_layout[i][0], s->touch_layout[i][1]);
    }
    fputc('\n', f);
    fprintf(f, "; keyboard: SDL key names\n");
    for (int i = 0; i < SETTINGS_NUM_KEYS; i++) {
        fprintf(f, "key_%s = %s\n", SETTINGS_KEY_NAMES[i], s->keys[i]);
    }
    fclose(f);
}
