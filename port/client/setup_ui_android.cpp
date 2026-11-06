/* Android launcher: SDLActivity owns lifecycle; SDL2 provides the GLES context. */
#include "setup_ui.h"

#include <SDL.h>
#include <SDL_system.h>
#include <GLES3/gl3.h>
#include <jni.h>

#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "packs.h"
#include "discs.h"
#include "mods_ui.h"
#include "ui_locale.h"
#include "wired_theme.h"
#include "unused_gallery.h"
#include "png.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include "PsyX/PsyX_public.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define LAIN_DBG_UI(...) __android_log_print(ANDROID_LOG_INFO, "lain-dbg", __VA_ARGS__)
#else
#define LAIN_DBG_UI(...) do {} while (0)
#endif

#if defined(__ANDROID__)
/* Handed to PsyX (see PsyX_SetReusedGLContext) so the game reuses the
 * launcher's window+context instead of destroying and recreating them. */
SDL_Window *g_lain_launcher_window = NULL;
SDL_GLContext g_lain_launcher_context = NULL;
#endif

namespace {
std::mutex g_picker_mutex;
std::string g_picked_disc_path;
bool g_picker_result_ready = false;
std::mutex g_subtitle_mutex;
std::string g_subtitle_result_message;
std::string g_subtitle_result_code;
int g_subtitle_result_count = 0;
bool g_subtitle_result_ready = false;
std::mutex g_asset_export_mutex;
std::string g_asset_export_status;
std::atomic<bool> g_asset_export_busy{false};
// re:Lain Archive batch export v0.1.4
std::thread g_gallery_batch_thread;
std::atomic<bool> g_gallery_batch_ready{false};
std::atomic<size_t> g_gallery_batch_done{0};
std::atomic<size_t> g_gallery_batch_total{0};
std::mutex g_gallery_batch_mutex;
std::string g_gallery_batch_directory;


bool request_gallery_png_export(const std::string &private_path, const std::string &filename) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = cls ? env->GetMethodID(cls, "exportGalleryPng",
        "(Ljava/lang/String;Ljava/lang/String;)V") : nullptr;
    bool ok = method != nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
    jstring path = ok ? env->NewStringUTF(private_path.c_str()) : nullptr;
    jstring name = ok ? env->NewStringUTF(filename.c_str()) : nullptr;
    if (path && name) {
        env->CallVoidMethod(activity, method, path, name);
        if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
    } else ok = false;
    if (path) env->DeleteLocalRef(path);
    if (name) env->DeleteLocalRef(name);
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

bool request_gallery_zip_export(const std::string &private_dir) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = cls ? env->GetMethodID(cls, "exportGalleryZip", "(Ljava/lang/String;)V") : nullptr;
    bool ok = method != nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
    jstring directory = ok ? env->NewStringUTF(private_dir.c_str()) : nullptr;
    if (directory) {
        env->CallVoidMethod(activity, method, directory);
        if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
        env->DeleteLocalRef(directory);
    } else ok = false;
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

std::string gallery_export_filename(const GalleryItem &item, size_t flat_index) {
    std::string filename = "lain_" + std::to_string(flat_index) + "_";
    for (unsigned char c : item.title) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') filename.push_back((char)c);
        else if (c == ' ') filename.push_back('_');
        if (filename.size() >= 58) break;
    }
    return filename + ".png";
}

void gallery_export_png(const GalleryItem &item, size_t frame, size_t index) {
    if (item.frames.empty() || frame >= item.frames.size() ||
        item.w <= 0 || item.h <= 0 || item.w > 8192 || item.h > 8192 ||
        item.frames[frame].size() != (size_t)item.w * (size_t)item.h)
        return;
    if (g_asset_export_busy.exchange(true)) return;
    const std::string temp = std::string(settings_data_dir()) + ".wired-gallery-export.png";
    const uint8_t *rgba = (const uint8_t *)item.frames[frame].data();
    if (png_save(temp.c_str(), rgba, item.w, item.h) != 0) {
        g_asset_export_busy = false;
        std::lock_guard<std::mutex> lock(g_asset_export_mutex);
        g_asset_export_status = "PNG encode failed.";
        return;
    }
    if (!request_gallery_png_export(temp, gallery_export_filename(item, index))) {
        std::remove(temp.c_str());
        g_asset_export_busy = false;
        std::lock_guard<std::mutex> lock(g_asset_export_mutex);
        g_asset_export_status = "Android document picker unavailable.";
    }
}


bool request_android_disc_picker(const char *data_dir) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
        if (env && activity) {
            env->DeleteLocalRef(activity);
        }
        return false;
    }

    jclass activity_class = env->GetObjectClass(activity);
    jmethodID method = activity_class
        ? env->GetMethodID(activity_class, "openDiscPicker", "(Ljava/lang/String;)V")
        : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    bool ok = method != nullptr;
    jstring directory = ok ? env->NewStringUTF(data_dir) : nullptr;
    if (ok && directory) {
        env->CallVoidMethod(activity, method, directory);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            ok = false;
        }
    } else {
        ok = false;
    }

    if (directory) env->DeleteLocalRef(directory);
    if (activity_class) env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);
    return ok;
}

bool take_android_picker_result(std::string &path) {
    std::lock_guard<std::mutex> lock(g_picker_mutex);
    if (!g_picker_result_ready) {
        return false;
    }
    path.swap(g_picked_disc_path);
    g_picked_disc_path.clear();
    g_picker_result_ready = false;
    return true;
}

bool request_android_subtitles(const char *data_dir, const char *method_name) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
        if (env && activity) env->DeleteLocalRef(activity);
        return false;
    }
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID method = activity_class
        ? env->GetMethodID(activity_class, method_name, "(Ljava/lang/String;)V")
        : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    bool ok = method != nullptr;
    jstring directory = ok ? env->NewStringUTF(data_dir) : nullptr;
    if (ok && directory) {
        env->CallVoidMethod(activity, method, directory);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            ok = false;
        }
    } else {
        ok = false;
    }
    if (directory) env->DeleteLocalRef(directory);
    if (activity_class) env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);
    return ok;
}

bool request_android_community_subtitles(const char *data_dir, const char *language) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = cls ? env->GetMethodID(cls,"downloadCommunitySubtitles",
        "(Ljava/lang/String;Ljava/lang/String;)V") : nullptr;
    bool ok = method != nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
    jstring directory = ok ? env->NewStringUTF(data_dir) : nullptr;
    jstring code = ok ? env->NewStringUTF(language) : nullptr;
    if (directory && code) {
        env->CallVoidMethod(activity, method, directory, code);
        if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
    } else ok = false;
    if (directory) env->DeleteLocalRef(directory);
    if (code) env->DeleteLocalRef(code);
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

bool take_android_subtitle_result(std::string &code, int &count, std::string &message) {
    std::lock_guard<std::mutex> lock(g_subtitle_mutex);
    if (!g_subtitle_result_ready) return false;
    code.swap(g_subtitle_result_code);
    g_subtitle_result_code.clear();
    count = g_subtitle_result_count;
    message.swap(g_subtitle_result_message);
    g_subtitle_result_message.clear();
    g_subtitle_result_ready = false;
    return true;
}
}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_LainActivity_nativeOnDiscPicked(JNIEnv *env, jclass, jstring value) {
    std::string path;
    if (value) {
        const char *chars = env->GetStringUTFChars(value, nullptr);
        if (chars) {
            path = chars;
            env->ReleaseStringUTFChars(value, chars);
        }
    }
    std::lock_guard<std::mutex> lock(g_picker_mutex);
    g_picked_disc_path = std::move(path);
    g_picker_result_ready = true;
}

extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_LainActivity_nativeOnModFolderImported(JNIEnv *env, jclass,
                                                            jstring folder_path, jstring message) {
    std::string path, status;
    if (folder_path) {
        const char *chars = env->GetStringUTFChars(folder_path, nullptr);
        if (chars) { path = chars; env->ReleaseStringUTFChars(folder_path, chars); }
    }
    if (message) {
        const char *chars = env->GetStringUTFChars(message, nullptr);
        if (chars) { status = chars; env->ReleaseStringUTFChars(message, chars); }
    }
    mods_android_import_result(path.c_str(), status.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_LainActivity_nativeOnSubtitlesDownloaded(JNIEnv *env, jclass,
                                                              jstring code, jint subtitle_count,
                                                              jstring message) {
    std::string pack_code, status;
    if (code) {
        const char *chars = env->GetStringUTFChars(code, nullptr);
        if (chars) { pack_code = chars; env->ReleaseStringUTFChars(code, chars); }
    }
    if (message) {
        const char *chars = env->GetStringUTFChars(message, nullptr);
        if (chars) { status = chars; env->ReleaseStringUTFChars(message, chars); }
    }
    std::lock_guard<std::mutex> lock(g_subtitle_mutex);
    g_subtitle_result_code = std::move(pack_code);
    g_subtitle_result_count = (int)subtitle_count;
    g_subtitle_result_message = std::move(status);
    g_subtitle_result_ready = true;
}

extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_LainActivity_nativeOnAssetExportFinished(JNIEnv *env, jclass, jstring message) {
    const char *raw = message ? env->GetStringUTFChars(message, nullptr) : nullptr;
    {
        std::lock_guard<std::mutex> lock(g_asset_export_mutex);
        g_asset_export_status = raw ? raw : "Export completed.";
    }
    if (raw) env->ReleaseStringUTFChars(message, raw);
    g_asset_export_busy.store(false);
}

namespace {

/* Read art from APK assets through SDL's Android asset manager. An absent file
 * simply falls back to the built-in vector portrait. No game disc data required. */
GLuint wired_load_hero_texture(int &w, int &h) {
    w = h = 0;
    SDL_RWops *rw = SDL_RWFromFile("wired_hero.png", "rb");
    if (!rw) return 0;
    const Sint64 size = SDL_RWsize(rw);
    if (size <= 0 || size > 16 * 1024 * 1024) { SDL_RWclose(rw); return 0; }
    std::vector<uint8_t> bytes((size_t)size);
    const bool read_ok = SDL_RWread(rw, bytes.data(), 1, (size_t)size) == (size_t)size;
    SDL_RWclose(rw);
    if (!read_ok) return 0;
    char err[256] = {};
    uint8_t *pixels = png_decode(bytes.data(), bytes.size(), &w, &h, err, sizeof err);
    if (!pixels) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Wired hero PNG: %s", err);
        return 0;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    std::free(pixels);
    return texture;
}

/* Android version of the desktop Unused tab. The decoder is shared verbatim
 * (unused_gallery.cpp); only the presentation and GL texture handling differ. */
struct AndroidGallery {
    std::atomic<int> state{0}; // 0 not loaded, 1 loading, 2 ready, 3 failed
    std::vector<GallerySection> sections;
    std::string error;
    std::vector<GLuint> textures;       // flattened over items; 0 for sound items
    std::vector<int> uploaded_frames;  // one streaming texture per animated item
    bool uploaded = false;
};

AndroidGallery g_gallery; // static: the worker may finish after the launcher tab closes
SDL_AudioDeviceID g_gallery_audio = 0;
const GalleryItem *g_gallery_playing = nullptr;

// The staging directory is owned by re:Lain and contains PNGs only.
void gallery_batch_discard(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (!dir) return;
    for (dirent *ent; (ent = readdir(dir)) != nullptr;) {
        const std::string name = ent->d_name;
        if (name.rfind("lain_", 0) != 0 || name.size() < 8 ||
            name.substr(name.size() - 4) != ".png") continue;
        std::remove((path + "/" + name).c_str());
    }
    closedir(dir);
    rmdir(path.c_str());
}

void gallery_batch_fail(const std::string &message, const std::string &path) {
    gallery_batch_discard(path);
    {
        std::lock_guard<std::mutex> lock(g_asset_export_mutex);
        g_asset_export_status = message;
    }
    g_asset_export_busy.store(false);
}

bool gallery_batch_frame_valid(const GalleryItem &item) {
    return !item.frames.empty() && item.w > 0 && item.h > 0 &&
           item.w <= 4096 && item.h <= 4096 &&
           (size_t)item.w * item.h <= 16 * 1024 * 1024;
}

std::string gallery_batch_frame_name(const GalleryItem &item, size_t index, size_t frame) {
    const std::string file = gallery_export_filename(item, index);
    const std::string base = file.substr(0, file.size() - 4);
    char suffix[32];
    std::snprintf(suffix, sizeof suffix, "_f%04zu.png", frame + 1);
    return base + suffix;
}

void gallery_batch_begin(AndroidGallery &gallery) {
    if (g_asset_export_busy.exchange(true)) return;
    if (g_gallery_batch_thread.joinable()) g_gallery_batch_thread.join();
    size_t count = 0;
    for (const auto &section : gallery.sections)
        for (const auto &item : section.items) {
            if (!gallery_batch_frame_valid(item)) continue;
            bool all_valid = true;
            for (const auto &frame : item.frames)
                if (frame.size() != (size_t)item.w * item.h) { all_valid = false; break; }
            if (all_valid) count += item.frames.size();
        }
    if (!count || count > 4096) {
        gallery_batch_fail(count ? "Too many frames for one ZIP (limit: 4096)." :
                                  "No valid image frames to export.", "");
        return;
    }
    g_gallery_batch_done = 0;
    g_gallery_batch_total = count;
    g_gallery_batch_ready = false;
    const std::string dir = std::string(settings_data_dir()) + ".wired-gallery-batch";
    // Only a previous export's files are removed; no disc or user data is touched.
    gallery_batch_discard(dir);
    if (mkdir(dir.c_str(), 0700) != 0) {
        gallery_batch_fail("Could not create temporary export folder.", dir);
        return;
    }
    g_gallery_batch_thread = std::thread([&gallery, dir] {
        size_t index = 0;
        size_t total_bytes = 0;
        for (const auto &section : gallery.sections) {
            for (const auto &item : section.items) {
                const size_t current_index = index++;
                if (!gallery_batch_frame_valid(item)) continue;
                bool all_valid = true;
                for (const auto &frame : item.frames)
                    if (frame.size() != (size_t)item.w * item.h) { all_valid = false; break; }
                if (!all_valid) continue;
                for (size_t i = 0; i < item.frames.size(); ++i) {
                    const std::string path = dir + "/" + gallery_batch_frame_name(item, current_index, i);
                    if (png_save(path.c_str(), (const uint8_t *)item.frames[i].data(), item.w, item.h) != 0) {
                        gallery_batch_fail("Could not encode a PNG; batch aborted.", dir);
                        return;
                    }
                    struct stat st = {};
                    if (stat(path.c_str(), &st) != 0 || st.st_size < 1 ||
                        (uint64_t)st.st_size > 64u * 1024u * 1024u) {
                        gallery_batch_fail("PNG size validation failed.", dir);
                        return;
                    }
                    total_bytes += (size_t)st.st_size;
                    if (total_bytes > 400u * 1024u * 1024u) {
                        gallery_batch_fail("Batch exceeds 400 MiB temporary storage limit.", dir);
                        return;
                    }
                    ++g_gallery_batch_done;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_gallery_batch_mutex);
            g_gallery_batch_directory = dir;
        }
        g_gallery_batch_ready.store(true);
    });
}

void gallery_batch_pump() {
    if (!g_gallery_batch_ready.exchange(false)) return;
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(g_gallery_batch_mutex);
        dir.swap(g_gallery_batch_directory);
    }
    if (!request_gallery_zip_export(dir))
        gallery_batch_fail("Could not open the Android ZIP export picker.", dir);
}

void gallery_sound_stop() {
    if (g_gallery_audio) SDL_ClearQueuedAudio(g_gallery_audio);
    g_gallery_playing = nullptr;
}

void gallery_sound_close() {
    if (g_gallery_audio) SDL_CloseAudioDevice(g_gallery_audio);
    g_gallery_audio = 0;
    g_gallery_playing = nullptr;
}

void gallery_sound_play(const GalleryItem &item) {
    if (item.pcm.empty() || item.rate <= 0 || item.channels <= 0) return;
    if (!g_gallery_audio) {
        if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Unused gallery: audio init failed: %s", SDL_GetError());
            return;
        }
        SDL_AudioSpec want = {}, have = {};
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        g_gallery_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!g_gallery_audio) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Unused gallery: audio device failed: %s", SDL_GetError());
            return;
        }
        SDL_PauseAudioDevice(g_gallery_audio, 0);
    }

    const size_t in_frames = item.pcm.size() / (size_t)item.channels;
    if (!in_frames) return;
    const size_t out_frames = (size_t)((double)in_frames * 44100.0 / item.rate);
    std::vector<int16_t> out(out_frames * 2);
    for (size_t i = 0; i < out_frames; ++i) {
        const double pos = (double)i * item.rate / 44100.0;
        const size_t a = std::min((size_t)pos, in_frames - 1);
        const size_t b = std::min(a + 1, in_frames - 1);
        const double f = pos - a;
        for (int c = 0; c < 2; ++c) {
            const int ch = item.channels == 2 ? c : 0;
            const double v = item.pcm[a * item.channels + ch] * (1.0 - f) +
                             item.pcm[b * item.channels + ch] * f;
            out[i * 2 + c] = (int16_t)v;
        }
    }
    gallery_sound_stop();
    const size_t bytes = out.size() * sizeof(int16_t);
    if (bytes <= UINT32_MAX && SDL_QueueAudio(g_gallery_audio, out.data(), (Uint32)bytes) == 0) {
        g_gallery_playing = &item;
    }
}

void gallery_start(AndroidGallery &gallery) {
    char d1[1024], d2[1024];
    const bool have1 = discs_imported(settings_data_dir(), DISC_1, d1, sizeof d1);
    const bool have2 = discs_imported(settings_data_dir(), DISC_2, d2, sizeof d2);
    if (!have1) {
        gallery.error = "Import disc 1 first in the Discs tab.";
        gallery.state.store(3);
        return;
    }
    gallery.error.clear();
    gallery.sections.clear();
    gallery.state.store(1);
    const std::string p1 = d1;
    const std::string p2 = have2 ? d2 : "";
    std::thread([&gallery, p1, p2] {
        std::string error;
        std::vector<GallerySection> decoded;
        const bool ok = gallery_load(p1.c_str(), p2.empty() ? nullptr : p2.c_str(), decoded, error);
        gallery.sections = std::move(decoded);
        gallery.error = std::move(error);
        gallery.state.store(ok ? 2 : 3);
    }).detach();
}

void gallery_release_gpu(AndroidGallery &gallery) {
    for (GLuint texture : gallery.textures) {
        if (texture) glDeleteTextures(1, &texture);
    }
    gallery.textures.clear();
    gallery.uploaded_frames.clear();
    gallery.uploaded = false;
}

void gallery_upload(AndroidGallery &gallery) {
    gallery.textures.clear();
    gallery.uploaded_frames.clear();
    for (const GallerySection &section : gallery.sections) {
        for (const GalleryItem &item : section.items) {
            GLuint texture = 0;
            int uploaded_frame = -1;
            if (!item.frames.empty()) {
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                const std::vector<uint32_t> &pixels = item.frames.front();
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item.w, item.h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                uploaded_frame = 0;
            }
            gallery.textures.push_back(texture);
            gallery.uploaded_frames.push_back(uploaded_frame);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    gallery.uploaded = true;
}

void gallery_draw_android(AndroidGallery &gallery) {
    int state = gallery.state.load();
    if (state == 0) {
        gallery_start(gallery);
        state = gallery.state.load();
    }
    if (state == 1) {
        ImGui::Spacing();
        ImGui::TextDisabled("Reading and decoding your own disc images...");
        ImGui::TextWrapped("This can take a little while the first time. The game data is read from your imported discs and is not included in re:Lain.");
        return;
    }
    if (state == 3) {
        ImGui::TextWrapped("%s", gallery.error.c_str());
        if (ImGui::Button(reLain_ui_text("Try again / reload", "Tentar novamente"), ImVec2(-1, 54))) {
            gallery.state.store(0);
        }
        return;
    }

    if (!gallery.uploaded) gallery_upload(gallery);

    ImGui::TextWrapped("Unused assets decoded from your own discs. Export a PNG by choosing a destination with Android Files; nothing is bundled with the app.");
    {
        std::lock_guard<std::mutex> lock(g_asset_export_mutex);
        if (!g_asset_export_status.empty()) ImGui::TextWrapped("%s", g_asset_export_status.c_str());
    }
    if (g_asset_export_busy.load()) {
        const size_t n = g_gallery_batch_total.load();
        const size_t done = g_gallery_batch_done.load();
        if (n && done <= n && !g_gallery_batch_ready.load())
            ImGui::TextDisabled("Export: %zu / %zu frames (or Android picker)...", done, n);
        else ImGui::TextDisabled("Export / document picker in progress...");
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(gallery.state.load() == 1 || g_asset_export_busy.load());
    if (ImGui::Button(reLain_ui_text("Reload gallery", "Recarregar galeria"), ImVec2(-1, 48))) {
        gallery_release_gpu(gallery);
        gallery_sound_stop();
        gallery.sections.clear();
        gallery.error.clear();
        gallery.state.store(0);
    }
    ImGui::EndDisabled();

    char disc2_path[1024];
    const bool have_disc2 = discs_imported(settings_data_dir(), DISC_2, disc2_path, sizeof disc2_path);
    if (!have_disc2) {
        ImGui::TextDisabled("Note: import Disc 2 in the Discs tab and reload to see its additional rare content.");
    }

    size_t total_items = 0;
    for (const GallerySection &section : gallery.sections) total_items += section.items.size();
    if (total_items == 0) {
        ImGui::TextWrapped("No gallery entries could be decoded from these disc images.");
        return;
    }

    ImGui::BeginDisabled(g_asset_export_busy.load());
    if (ImGui::Button(reLain_ui_text("EXPORT ALL IMAGE FRAMES (ZIP)",
                                    "EXPORTAR TODAS AS IMAGENS (ZIP)"), ImVec2(-1, 50))) {
        gallery_batch_begin(gallery);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Every frame of every image/animation; audio not included.");

    const double now = ImGui::GetTime();
    size_t flat_index = 0;
    for (const GallerySection &section : gallery.sections) {
        ImGui::Spacing();
        ImGui::SeparatorText(section.title.c_str());
        ImGui::TextWrapped("%s", section.intro.c_str());
        ImGui::Spacing();

        for (const GalleryItem &item : section.items) {
            const GLuint texture = gallery.textures[flat_index];
            int &uploaded_frame = gallery.uploaded_frames[flat_index];
            ImGui::PushID((int)flat_index);

            if (!item.frames.empty() && texture) {
                size_t frame = 0;
                if (item.fps > 0.0f && item.frames.size() > 1) {
                    frame = (size_t)(now * item.fps) % item.frames.size();
                }
                if ((int)frame != uploaded_frame) {
                    glBindTexture(GL_TEXTURE_2D, texture);
                    const std::vector<uint32_t> &pixels = item.frames[frame];
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item.w, item.h, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    uploaded_frame = (int)frame;
                }
                const float available = std::max(64.0f, ImGui::GetContentRegionAvail().x - 8.0f);
                const float max_width = std::min(available, 480.0f);
                const float max_height = 320.0f;
                float scale = std::min(max_width / (float)item.w, max_height / (float)item.h);
                if (item.w <= 160 && item.h <= 160) scale = std::min(2.0f, scale);
                const ImVec2 image_size(item.w * scale, item.h * scale);
                ImGui::Image((ImTextureID)(intptr_t)texture, image_size);
                ImGui::TextWrapped("%s", item.title.c_str());
                ImGui::BeginDisabled(g_asset_export_busy.load());
                if (ImGui::Button(reLain_ui_text("EXPORT THIS FRAME AS PNG", "EXPORTAR QUADRO COMO PNG"),
                                  ImVec2(-1, ImGui::GetFrameHeight() * 1.7f))) {
                    gallery_export_png(item, frame, flat_index);
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("%d x %d PNG / original resolution", item.w, item.h);
                if (item.fps > 0.0f && item.frames.size() > 1) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu frames)", item.frames.size());
                }
            } else if (!item.pcm.empty()) {
                if (g_gallery_playing == &item && g_gallery_audio &&
                    SDL_GetQueuedAudioSize(g_gallery_audio) == 0) {
                    g_gallery_playing = nullptr;
                }
                const bool playing = g_gallery_playing == &item;
                if (ImGui::Button(playing ? "Stop audio" : "Play audio",
                                  ImVec2(-1, ImGui::GetFrameHeight() * 1.8f))) {
                    if (playing) gallery_sound_stop();
                    else gallery_sound_play(item);
                }
                ImGui::TextWrapped("%s", item.title.c_str());
                if (item.rate > 0 && item.channels > 0) {
                    ImGui::TextDisabled("Duration: %.1f seconds",
                        item.pcm.size() / (double)item.channels / item.rate);
                }
            }
            ImGui::TextWrapped("%s", item.caption.c_str());
            ImGui::PopID();
            ImGui::Spacing();
            ++flat_index;
        }
    }
}

} // namespace

extern "C" int setup_ui_run(Settings *settings) {
    LAIN_DBG_UI("setup_ui_run: entry");
SDL_Window *window = NULL;
    SDL_GLContext context = NULL;
#if defined(__ANDROID__)
    /* Session loop (item 1.3): reuse launcher window (reuse launcher window)
     * on the 2nd+ entry instead of creating a 2nd SurfaceView, which makes
     * Android kill the Activity silently. */
    LAIN_DBG_UI("setup_ui_run: reuse check launcher=%p ctx=%p",
        (void*)g_lain_launcher_window, (void*)g_lain_launcher_context);
    if (g_lain_launcher_window && g_lain_launcher_context) {
        window = g_lain_launcher_window;
        context = g_lain_launcher_context;
        LAIN_DBG_UI("setup_ui_run: MakeCurrent (reuse) start");
        SDL_GL_MakeCurrent(window, context);
        LAIN_DBG_UI("setup_ui_run: MakeCurrent (reuse) OK");
    } else
#endif
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        window = SDL_CreateWindow(
            "re:Lain", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Android launcher window: %s", SDL_GetError());
            return 0;
        }

        context = SDL_GL_CreateContext(window);
        if (!context) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Android GLES 3 context: %s", SDL_GetError());
            SDL_DestroyWindow(window);
            return 0;
        }
        SDL_GL_MakeCurrent(window, context);
    }
    LAIN_DBG_UI("setup_ui_run: SetSwapInterval");
    SDL_GL_SetSwapInterval(1);
    LAIN_DBG_UI("setup_ui_run: SetSwapInterval OK");

    LAIN_DBG_UI("setup_ui_run: ImGui CreateContext start");
#if defined(__ANDROID__)
    /* The game runs its own ImGui (F1 overlay). longjmp skips its shutdown,
     * so the global ImGui context still points at the game's backends.
     * Destroy them before init, or ImGui_ImplSDL2_InitForOpenGL crashes
     * on stale BackendPlatformUserData. Only runs on the 2nd+ entry. */
    if (ImGui::GetCurrentContext() != nullptr) {
        LAIN_DBG_UI("setup_ui_run: orphan ImGui detected, cleaning up");
        ImGuiIO &orphan = ImGui::GetIO();
        if (orphan.BackendRendererUserData) {
            LAIN_DBG_UI("setup_ui_run:  ImGui_ImplOpenGL3_Shutdown");
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (orphan.BackendPlatformUserData) {
            LAIN_DBG_UI("setup_ui_run:  ImGui_ImplSDL2_Shutdown");
            ImGui_ImplSDL2_Shutdown();
        }
        ImGui::DestroyContext();
        LAIN_DBG_UI("setup_ui_run: orphan ImGui cleaned");
    }
#endif
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    LAIN_DBG_UI("setup_ui_run: ImGui CreateContext OK");
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();
    wired::setup_style();
    reLain_ui_language_load(settings_data_dir());
    // re:Lain Latin subtitle scope: keep only installed Latin scripts.
    const std::string selected_subtitle = settings->text_lang;
    if (selected_subtitle != "ja" && selected_subtitle != "pt-BR" &&
        selected_subtitle != "en" && selected_subtitle != "en-laingame" &&
        selected_subtitle != "es-ES" && selected_subtitle != "fr" &&
        selected_subtitle != "de") {
        SDL_strlcpy(settings->text_lang, "ja", sizeof settings->text_lang);
        settings_save(settings);
    }
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    float ui_scale = 1.55f; /* a readable fallback for devices that do not report DPI */
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0 && hdpi > 0.0f) {
        ui_scale = hdpi / 220.0f;
        if (ui_scale < 1.40f) ui_scale = 1.40f;
        if (ui_scale > 1.95f) ui_scale = 1.95f;
    }
    ImGui::GetStyle().ScaleAllSizes(ui_scale);
    ImGui::GetStyle().FontScaleMain = ui_scale;
    LAIN_DBG_UI("setup_ui_run: ImGui_ImplSDL2_InitForOpenGL start");
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    LAIN_DBG_UI("setup_ui_run: ImGui_ImplSDL2_InitForOpenGL OK");
    LAIN_DBG_UI("setup_ui_run: ImGui_ImplOpenGL3_Init start");
    ImGui_ImplOpenGL3_Init("#version 300 es");
    LAIN_DBG_UI("setup_ui_run: ImGui_ImplOpenGL3_Init OK");
    LAIN_DBG_UI("setup_ui_run: AddFontDefault start");
    io.Fonts->AddFontDefault();
    LAIN_DBG_UI("setup_ui_run: AddFontDefault OK");
    int wired_hero_width = 0, wired_hero_height = 0;
    GLuint wired_hero_texture = wired_load_hero_texture(wired_hero_width, wired_hero_height);

    static ImportJob job = {};
    bool running = true;
    bool play = false;
    bool picker_waiting = false;
    bool subtitles_waiting = false;
    std::string last_message;
    std::string subtitle_message;
    std::vector<PackOption> installed_subtitle_packs = find_packs("lang", "Japanese (original)");
    std::string language_message;
    int launcher_page = 0; // 0 Home, 1 Discs, 2 Mods, 3 Archive, 4 Config, 5 Subtitles
    bool main_open = true;
    bool status_open = true;

    while (running && !play) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (launcher_page != 0) launcher_page = 0;
                else if (!main_open) main_open = true;
                else running = false;
            }
        }

        std::string picked_path;
        if (take_android_picker_result(picked_path)) {
            picker_waiting = false;
            if (!picked_path.empty()) {
                discs_import_start(&job, picked_path.c_str(), settings_data_dir());
                if (job.state == IMPORT_RUNNING) {
                    last_message = "Checking and importing the selected image...";
                } else {
                    last_message = job.message;
                }
            } else {
                last_message = "No disc image was selected, or the copy failed.";
            }
        }
        if (job.state == IMPORT_DONE || job.state == IMPORT_FAILED) {
            last_message = job.message;
            job.state = IMPORT_IDLE;
        }
        int subtitle_count = 0;
        std::string subtitle_code, downloaded_subtitles;
        if (take_android_subtitle_result(subtitle_code, subtitle_count, downloaded_subtitles)) {
            subtitles_waiting = false;
            subtitle_message = downloaded_subtitles;
            if (subtitle_count > 0) {
                SDL_Log("re:Lain: installed %d subtitle tracks for %s", subtitle_count, subtitle_code.c_str());
                // Select only a known, actually installed pack. No changes to voice language.
                const bool latin_code = subtitle_code == "pt-BR" || subtitle_code == "en" ||
                                        subtitle_code == "es-ES" || subtitle_code == "fr" ||
                                        subtitle_code == "de" || subtitle_code == "en-laingame";
                if (latin_code) {
                    installed_subtitle_packs = find_packs("lang", "Japanese (original)");
                    for (const auto &pack : installed_subtitle_packs) {
                        if (pack.code == subtitle_code) {
                            SDL_strlcpy(settings->text_lang, subtitle_code.c_str(), sizeof settings->text_lang);
                            settings_save(settings);
                            subtitle_message += reLain_ui_text(
                                "  Ready: selected for next Play.",
                                "  Pronto: selecionado para o próximo jogo.");
                            break;
                        }
                    }
                }
            }
        }

        gallery_batch_pump();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImVec2 screen = io.DisplaySize;
        wired::desktop(screen, ui_scale);
        char disc1[1024] = {}, disc2[1024] = {};
        const bool have_disc1 = discs_imported(settings_data_dir(), DISC_1, disc1, sizeof disc1);
        const bool have_disc2 = discs_imported(settings_data_dir(), DISC_2, disc2, sizeof disc2);
        const bool busy = job.state == IMPORT_RUNNING;
        const bool can_play = have_disc1 && !busy && !picker_waiting && !mods_android_import_busy() && !g_asset_export_busy.load() && !subtitles_waiting;

        // A single desktop task button restores the movable workstation window.
        // The bottom bar and top bar are purely decorative, not duplicated navigation.
        // The desktop strip is 34*ui_scale high. No window padding is allowed
        // here: the previous default padding + oversized font clipped buttons.
        const float task_height = 23.0f * ui_scale;
        const float task_width = std::min(385.0f * ui_scale, screen.x - 46.0f * ui_scale);
        ImGui::SetNextWindowPos(ImVec2(37.0f * ui_scale,
            screen.y - (34.0f * ui_scale) + 5.0f * ui_scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::max(50.0f, task_width), task_height), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * ui_scale, 2.0f * ui_scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f * ui_scale, 0));
        ImGui::Begin("##wired_taskbar", nullptr, ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        const float main_button_w = std::min(242.0f * ui_scale, std::max(25.0f, task_width * .70f));
        if (ImGui::Button("WIRED WORKSTATION // re:Lain", ImVec2(main_button_w, task_height))) {
            main_open = true;
            ImGui::SetWindowFocus("WIRED WORKSTATION // re:Lain");
        }
        ImGui::SameLine();
        if (ImGui::Button(status_open ? "STATUS -" : "STATUS +",
                          ImVec2(std::max(25.0f, task_width - main_button_w - 5.0f * ui_scale), task_height)))
            status_open = !status_open;
        ImGui::End();
        ImGui::PopStyleVar(3);

        if (main_open) {
            ImGui::SetNextWindowPos(ImVec2(screen.x*.075f, screen.y*.15f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(screen.x*.61f, screen.y*.70f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(std::min(460.0f * ui_scale,screen.x*.68f),std::min(290.0f*ui_scale,screen.y*.61f)),
                ImVec2(screen.x*.96f,screen.y*.86f));
            if (ImGui::Begin("WIRED WORKSTATION // re:Lain", &main_open,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
                if (launcher_page == 0) {
                    const float available_w = ImGui::GetContentRegionAvail().x;
                    const float available_h = ImGui::GetContentRegionAvail().y;
                    const float foot_h = 70.0f*ui_scale;
                    const bool columns = available_w >= 660.0f*ui_scale;
                    float body_h = columns ? std::max(180.0f*ui_scale, available_h-foot_h) : 170.0f*ui_scale;
                    const float left_w = columns ? (available_w-ImGui::GetStyle().ItemSpacing.x)*.54f : available_w;
                    ImGui::BeginChild("##wired_hero", ImVec2(left_w, body_h), true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    if (wired_hero_texture && wired_hero_width > 0 && wired_hero_height > 0) {
                        // Contain rather than cover: the old crop clipped the
                        // GitHub credit at the bottom of the original art.
                        const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
                        const float border = 3.0f * ui_scale;
                        const float safe_w = std::max(1.0f, sz.x - border * 2.0f);
                        const float safe_h = std::max(1.0f, sz.y - border * 2.0f);
                        const float fit = std::min(safe_w / wired_hero_width, safe_h / wired_hero_height);
                        const float image_w = wired_hero_width * fit;
                        const float image_h = wired_hero_height * fit;
                        const ImVec2 a(p.x + (sz.x-image_w)*.5f, p.y + (sz.y-image_h)*.5f);
                        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)wired_hero_texture,
                            a, ImVec2(a.x + image_w, a.y + image_h), ImVec2(0,0), ImVec2(1,1));
                    } else {
                        wired::hero_background(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
                    }
                    if (!wired_hero_texture) {
                    ImGui::SetCursorPos(ImVec2(18.0f*ui_scale, 18.0f*ui_scale));
                    ImGui::TextColored(ImVec4(.75f,.71f,1,1), "re:Lain_");
                    ImGui::SetCursorPos(ImVec2(18.0f*ui_scale, 51.0f*ui_scale));
                    ImGui::TextDisabled("serial experiments lain");
                    const float credit_y = std::max(87.0f*ui_scale,body_h-59.0f*ui_scale);
                    ImGui::SetCursorPos(ImVec2(18.0f*ui_scale, credit_y));
                    ImGui::TextColored(ImVec4(.72f,.77f,.92f,1), "fan port / Android");
                    ImGui::Text("github.com/VandalsEmpire");
                    }
                    ImGui::EndChild();
                    if (columns) ImGui::SameLine();
                    ImGui::BeginChild("##wired_actions", ImVec2(columns ? 0 : available_w, columns ? body_h : 0), false,
                                      columns ? ImGuiWindowFlags_NoScrollbar : 0);
                    const ImVec2 button_size(-1.0f, columns ? body_h*.171f : 45.0f*ui_scale);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.52f,.52f,.78f,1));
                    ImGui::BeginDisabled(!can_play);
                    if (ImGui::Button(reLain_ui_text("  >  PLAY", "  >  JOGAR"), button_size)) {
                        settings_save(settings);
                        play = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor();
                    if (ImGui::Button(reLain_ui_text("DISCS", "DISCOS"), button_size)) launcher_page = 1;
                    if (ImGui::Button("MODS", button_size)) launcher_page = 2;
                    if (ImGui::Button(reLain_ui_text("ARCHIVE", "ARQUIVO"), button_size)) launcher_page = 3;
                    if (ImGui::Button(reLain_ui_text("CONFIG", "AJUSTES"), button_size)) launcher_page = 4;
                    ImGui::EndChild();
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(.50f,.93f,.58f,1), "%s: %s    %s: %s",
                        reLain_ui_text("Disc 1", "Disco 1"), have_disc1 ? "READY" : "MISSING",
                        reLain_ui_text("Disc 2", "Disco 2"), have_disc2 ? "READY" : "MISSING");
                    ImGui::TextDisabled("Android (ARM64)  /  Wired Workstation");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(std::min(200.0f*ui_scale,ImGui::GetContentRegionAvail().x));
                    const char *languages[] = {"System", "English", "Português (Brasil)"};
                    int lang = reLain_ui_language_choice();
                    if (ImGui::Combo("##launcher_language", &lang, languages, 3)) {
                        if (!reLain_ui_language_save(settings_data_dir(), lang))
                            language_message = reLain_ui_text("Could not save language preference.", "Não foi possível salvar o idioma.");
                        else language_message.clear();
                    }
                    if (!language_message.empty()) ImGui::TextWrapped("%s", language_message.c_str());
                } else {
                    if (ImGui::Button(reLain_ui_text("< HOME", "< INÍCIO"))) launcher_page = 0;
                    ImGui::SameLine();
                    const char *page_names_en[] = {"", "DISCS", "MODS", "ARCHIVE / UNUSED", "CONFIG", "SUBTITLES"};
                    const char *page_names_pt[] = {"", "DISCOS", "MODS", "ARQUIVO / EXTRAS", "AJUSTES", "LEGENDAS"};
                    ImGui::TextColored(ImVec4(.77f,.80f,1,1), "%s",reLain_ui_pt()?page_names_pt[launcher_page]:page_names_en[launcher_page]);
                    ImGui::Separator();
                    ImGui::BeginChild("##wired_page_scroller",ImVec2(0,0),false);
                    if (launcher_page == 1) {
                        ImGui::SeparatorText(reLain_ui_text("Game discs", "Discos do jogo"));
                        ImGui::Text("%s (Site A): %s", reLain_ui_text("Disc 1", "Disco 1"), have_disc1 ? "READY" : "MISSING");
                        ImGui::Text("%s (Site B): %s", reLain_ui_text("Disc 2", "Disco 2"), have_disc2 ? "READY" : "MISSING");
                        ImGui::TextWrapped("%s",reLain_ui_text(
                            "Choose a raw .bin or .img data track (2352 bytes/sector). The disc is identified automatically.",
                            "Selecione uma trilha de dados .bin ou .img (2352 bytes/setor). O disco será identificado automaticamente."));
                        ImGui::TextDisabled("%s",reLain_ui_text(
                            "Disc images stay in the app's private directory.", "As imagens ficam na pasta privada do aplicativo."));
                        ImGui::Spacing();
                        ImGui::BeginDisabled(busy || picker_waiting);
                        if (ImGui::Button(reLain_ui_text("ADD / REPLACE DISC", "IMPORTAR / SUBSTITUIR DISCO"),
                                          ImVec2(-1,54.0f*ui_scale))) {
                            if (request_android_disc_picker(settings_data_dir())) {
                                picker_waiting = true;
                                last_message = reLain_ui_text("Select a BIN/IMG data track...", "Selecione a trilha BIN/IMG...");
                            } else last_message = reLain_ui_text("Could not open Android picker.", "Não foi possível abrir o seletor Android.");
                        }
                        ImGui::EndDisabled();
                        if (picker_waiting) ImGui::TextDisabled("%s",reLain_ui_text("Waiting for Android file picker...", "Aguardando o seletor Android..."));
                        if (busy) ImGui::ProgressBar(job.progress,ImVec2(-1,0),reLain_ui_text("Importing...", "Importando..."));
                        if (!last_message.empty()) ImGui::TextWrapped("%s",last_message.c_str());
                    } else if (launcher_page == 2) {
                        mods_tab_draw(settings);
                    } else if (launcher_page == 3) {
                        gallery_draw_android(g_gallery);
                    } else if (launcher_page == 4) {
                        ImGui::SeparatorText(reLain_ui_text("Launcher preferences", "Preferências do launcher"));
                        ImGui::TextWrapped("%s",reLain_ui_text(
                            "The language selector is on the Home screen. It affects the launcher, not the original game.",
                            "O seletor de idioma fica na tela inicial. Ele modifica o launcher, não o jogo original."));
                        ImGui::Checkbox(reLain_ui_text("Show session window", "Mostrar janela da sessão"), &status_open);
                        ImGui::Spacing();
                        if (ImGui::Button(reLain_ui_text("SUBTITLE PACKS", "PACOTES DE LEGENDAS"),ImVec2(-1,54*ui_scale))) launcher_page = 5;
                        ImGui::SeparatorText("About re:Lain");
                        ImGui::TextWrapped("Serial Experiments Lain - Android native port");
                        ImGui::TextWrapped("github.com/VandalsEmpire/lain-psx-decompiled-android");
                        ImGui::TextDisabled("%s",reLain_ui_text(
                            "Unix workstation concept: SGI IRIX / KDE classic / Wired.",
                            "Conceito de workstation: SGI IRIX / KDE clássico / Wired."));
                        ImGui::Spacing();
                        if (ImGui::Button(reLain_ui_text("EXIT APPLICATION", "SAIR DO APLICATIVO"),ImVec2(-1,50*ui_scale))) running = false;
                    } else if (launcher_page == 5) {
                        // re:Lain integrated subtitle install/select v0.1.6
                        ImGui::SeparatorText(reLain_ui_text("Subtitle languages", "Idiomas das legendas"));
                        ImGui::TextWrapped("%s", reLain_ui_text(
                            "Download a translation here. Once installed, it is automatically selected. "
                            "You can switch among downloaded languages any time.",
                            "Baixe uma tradução aqui. Ao instalar, ela é selecionada automaticamente. "
                            "Você pode alternar entre os idiomas instalados quando quiser."));
                        ImGui::Spacing();

                        const bool original_selected = SDL_strcmp(settings->text_lang, "ja") == 0;
                        ImGui::TextUnformatted(reLain_ui_text("Japanese (original game)", "Japonês (jogo original)"));
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", reLain_ui_text("Included", "Incluído"));
                        ImGui::BeginDisabled(original_selected || subtitles_waiting);
                        if (ImGui::Button(reLain_ui_text("USE ORIGINAL", "USAR ORIGINAL"), ImVec2(-1, 42.0f * ui_scale))) {
                            SDL_strlcpy(settings->text_lang, "ja", sizeof settings->text_lang);
                            settings_save(settings);
                            subtitle_message = reLain_ui_text("Original Japanese selected.", "Japonês original selecionado.");
                        }
                        ImGui::EndDisabled();
                        ImGui::Separator();

                        struct SubtitleLanguage {
                            const char *code;
                            const char *name;
                            bool alternative_ass;
                        };
                        static const SubtitleLanguage supported[] = {
                            {"pt-BR", "Português brasileiro", false},
                            {"en", "English (lainTSX)", false},
                            {"es-ES", "Español", false},
                            {"fr", "Français", false},
                            {"de", "Deutsch", false},
                            {"en-laingame", "English (ASS / laingame.net)", true}
                        };
                        const auto &installed = installed_subtitle_packs;
                        for (const auto &language : supported) {
                            bool is_installed = false;
                            for (const auto &pack : installed) {
                                if (pack.code == language.code) { is_installed = true; break; }
                            }
                            const bool selected = SDL_strcmp(settings->text_lang, language.code) == 0;
                            ImGui::PushID(language.code);
                            ImGui::TextUnformatted(language.name);
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", selected
                                ? reLain_ui_text("[ACTIVE]", "[ATIVO]")
                                : is_installed
                                  ? reLain_ui_text("[INSTALLED]", "[INSTALADO]")
                                  : reLain_ui_text("[NOT INSTALLED]", "[NÃO INSTALADO]"));

                            ImGui::BeginDisabled(subtitles_waiting || !is_installed || selected);
                            if (ImGui::Button(reLain_ui_text("SELECT", "SELECIONAR"),
                                              ImVec2(158.0f * ui_scale, 40.0f * ui_scale))) {
                                SDL_strlcpy(settings->text_lang, language.code, sizeof settings->text_lang);
                                settings_save(settings);
                                subtitle_message = reLain_ui_text("Subtitle language selected.", "Idioma de legendas selecionado.");
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            ImGui::BeginDisabled(subtitles_waiting);
                            if (ImGui::Button(is_installed
                                    ? reLain_ui_text("REINSTALL", "REINSTALAR")
                                    : reLain_ui_text("DOWNLOAD", "BAIXAR"),
                                    ImVec2(-1, 40.0f * ui_scale))) {
                                const bool started = language.alternative_ass
                                    ? request_android_subtitles(settings_data_dir(), "downloadEnglishSubtitles")
                                    : request_android_community_subtitles(settings_data_dir(), language.code);
                                if (started) {
                                    subtitles_waiting = true;
                                    subtitle_message = reLain_ui_text(
                                        "Downloading and installing subtitles...",
                                        "Baixando e instalando legendas...");
                                } else {
                                    subtitle_message = reLain_ui_text(
                                        "Could not start the subtitle download.",
                                        "Não foi possível iniciar o download das legendas.");
                                }
                            }
                            ImGui::EndDisabled();
                            ImGui::PopID();
                            ImGui::Spacing();
                        }
                        if (subtitles_waiting) ImGui::TextDisabled("%s", reLain_ui_text(
                            "Download in progress. Play is temporarily disabled.",
                            "Download em andamento. Jogar temporariamente desativado."));
                        if (!subtitle_message.empty()) ImGui::TextWrapped("%s", subtitle_message.c_str());
                        ImGui::TextDisabled("%s", reLain_ui_text(
                            "Community subtitle packs are optional. Japanese audio remains original.",
                            "Os pacotes de legendas são opcionais. O áudio japonês permanece original."));
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::End();
        }

        if (status_open) {
            ImGui::SetNextWindowPos(ImVec2(screen.x*.704f,screen.y*.255f),ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(screen.x*.282f, screen.y*.48f),ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(std::min(220.0f*ui_scale,screen.x*.38f),std::min(200.0f*ui_scale,screen.y*.4f)),
                ImVec2(screen.x*.88f,screen.y*.80f));
            if (ImGui::Begin("SESSION STATUS", &status_open,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
                ImGui::SeparatorText(reLain_ui_text("System", "Sistema"));
                ImGui::TextUnformatted("re:Lain / Android ARM64");
                ImGui::TextColored(ImVec4(.49f,.96f,.55f,1),"READY");
                ImGui::SeparatorText(reLain_ui_text("Resources", "Recursos"));
                ImGui::Text("%s: %s",reLain_ui_text("Disc 1", "Disco 1"),have_disc1?"READY":"MISSING");
                ImGui::Text("%s: %s",reLain_ui_text("Disc 2", "Disco 2"),have_disc2?"READY":"MISSING");
                const ModsUiCounts mod_counts = mods_ui_counts(settings);
                ImGui::Text("%s: %d", reLain_ui_text("Installed mods", "Mods instalados"), mod_counts.installed);
                ImGui::TextColored(ImVec4(.49f,.96f,.55f,1), "%s: %d",
                    reLain_ui_text("Enabled (next launch)", "Habilitados (próximo jogo)"), mod_counts.enabled);
                ImGui::SeparatorText("Wired");
                ImGui::TextDisabled("%s",reLain_ui_text("Local / animated topology", "Local / topologia animada"));
                const float graph_w=std::max(20.0f,ImGui::GetContentRegionAvail().x);
                const float graph_h=std::min(145.0f*ui_scale,std::max(56.0f,ImGui::GetContentRegionAvail().y-7.0f*ui_scale));
                wired::graph(ImVec2(graph_w,graph_h));
            }
            ImGui::End();
        }

        ImGui::Render();
        int drawable_w = 0, drawable_h = 0;
        SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
        glViewport(0, 0, drawable_w, drawable_h);
        glClearColor(0.025f, 0.03f, 0.045f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    if (g_gallery_batch_thread.joinable()) g_gallery_batch_thread.join();
    gallery_sound_close();
    gallery_release_gpu(g_gallery);
    if (wired_hero_texture) glDeleteTextures(1, &wired_hero_texture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
#if defined(__ANDROID__)
    if (play) {
        /* Give ownership to PsyX; do not destroy (reused next launcher). */
        g_lain_launcher_window = window;
        g_lain_launcher_context = context;
    } else {
        /* Exit: destroy and clear so the next cold start is fresh. */
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        g_lain_launcher_window = NULL;
        g_lain_launcher_context = NULL;
    }
#else
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
#endif
    return play ? 1 : 0;
}
