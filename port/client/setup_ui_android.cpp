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


#include "discs.h"
#include "mods_ui.h"
#include "unused_gallery.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace {
std::mutex g_picker_mutex;
std::string g_picked_disc_path;
bool g_picker_result_ready = false;
std::mutex g_subtitle_mutex;
std::string g_subtitle_result_message;
int g_subtitle_result_count = 0;
bool g_subtitle_result_ready = false;

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

bool request_android_english_subtitles(const char *data_dir) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
        if (env && activity) env->DeleteLocalRef(activity);
        return false;
    }
    jclass activity_class = env->GetObjectClass(activity);
    jmethodID method = activity_class
        ? env->GetMethodID(activity_class, "downloadEnglishSubtitles", "(Ljava/lang/String;)V")
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

bool take_english_subtitle_result(int &count, std::string &message) {
    std::lock_guard<std::mutex> lock(g_subtitle_mutex);
    if (!g_subtitle_result_ready) return false;
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
Java_org_libsdl_app_LainActivity_nativeOnEnglishSubtitlesDownloaded(JNIEnv *env, jclass,
                                                                     jint subtitle_count, jstring message) {
    std::string status;
    if (message) {
        const char *chars = env->GetStringUTFChars(message, nullptr);
        if (chars) { status = chars; env->ReleaseStringUTFChars(message, chars); }
    }
    std::lock_guard<std::mutex> lock(g_subtitle_mutex);
    g_subtitle_result_count = (int)subtitle_count;
    g_subtitle_result_message = std::move(status);
    g_subtitle_result_ready = true;
}

namespace {

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
        if (ImGui::Button("Try again / reload", ImVec2(-1, 54))) {
            gallery.state.store(0);
        }
        return;
    }

    if (!gallery.uploaded) gallery_upload(gallery);

    ImGui::TextWrapped("Things found on the Serial Experiments Lain discs that the final game does not normally show. Everything below is decoded directly from your own disc images.");
    ImGui::Spacing();
    ImGui::BeginDisabled(gallery.state.load() == 1);
    if (ImGui::Button("Reload gallery", ImVec2(-1, 48))) {
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *window = SDL_CreateWindow(
        "re:Lain", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Android launcher window: %s", SDL_GetError());
        return 0;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Android GLES 3 context: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        return 0;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    float ui_scale = 1.55f; /* a readable fallback for devices that do not report DPI */
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0 && hdpi > 0.0f) {
        ui_scale = hdpi / 220.0f;
        if (ui_scale < 1.40f) ui_scale = 1.40f;
        if (ui_scale > 1.95f) ui_scale = 1.95f;
    }
    ImGui::GetStyle().ScaleAllSizes(ui_scale);
    ImGui::GetStyle().FontScaleMain = ui_scale;
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    io.Fonts->AddFontDefault();

    static ImportJob job = {};
    bool running = true;
    bool play = false;
    bool picker_waiting = false;
    bool subtitles_waiting = false;
    std::string last_message;
    std::string subtitle_message;

    while (running && !play) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
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
        std::string downloaded_subtitles;
        if (take_english_subtitle_result(subtitle_count, downloaded_subtitles)) {
            subtitles_waiting = false;
            subtitle_message = downloaded_subtitles;
            if (subtitle_count > 0) SDL_Log("re:Lain: installed %d English subtitle files", subtitle_count);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("re:Lain", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
        ImGui::TextUnformatted("re:Lain");
        ImGui::TextDisabled("Serial Experiments Lain - Android");
        ImGui::Separator();

        char disc1[1024], disc2[1024];
        const bool have_disc1 = discs_imported(settings_data_dir(), DISC_1, disc1, sizeof disc1);
        const bool have_disc2 = discs_imported(settings_data_dir(), DISC_2, disc2, sizeof disc2);
        const bool busy = job.state == IMPORT_RUNNING;
        ImGui::BeginChild("launcher_tab_viewport", ImVec2(0, -76.0f * ui_scale), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        if (ImGui::BeginTabBar("launcher_tabs")) {
            if (ImGui::BeginTabItem("Discs")) {
                ImGui::SeparatorText("Game discs");
                ImGui::Text("Disc 1 (Site A): %s", have_disc1 ? "installed" : "not installed");
                ImGui::Text("Disc 2 (Site B): %s", have_disc2 ? "installed" : "not installed");
                ImGui::TextWrapped("Choose the raw .bin or .img data track (2352 bytes per sector).");
                ImGui::TextWrapped("The game identifies automatically whether it is Disc 1 or Disc 2.");
                ImGui::TextWrapped("Imported discs are stored in this app's private folder.");
                ImGui::Spacing();

                ImGui::BeginDisabled(busy || picker_waiting);
                if (ImGui::Button("Add / replace disc image", ImVec2(300.0f * ui_scale, 58.0f * ui_scale))) {
                    if (request_android_disc_picker(settings_data_dir())) {
                        picker_waiting = true;
                        last_message = "Choose a BIN or IMG data track...";
                    } else {
                        last_message = "Could not open the Android document picker.";
                    }
                }
                ImGui::EndDisabled();

                if (picker_waiting) {
                    ImGui::TextDisabled("Waiting for file selection or copy...");
                }
                if (busy) {
                    ImGui::ProgressBar(job.progress, ImVec2(-1, 0), "Importing and identifying...");
                }
                if (!last_message.empty()) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", last_message.c_str());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Mods")) {
                ImGui::BeginChild("android_mods_page", ImVec2(0, 0), false);
                mods_tab_draw(settings);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Subtitles")) {
                ImGui::SeparatorText("English fan subtitles");
                ImGui::TextWrapped("Download the completed timed English subtitle set from laingame.net's public source repository.");
                ImGui::TextDisabled("The subtitles are optional and are not bundled with re:Lain.");
                ImGui::Spacing();
                ImGui::BeginDisabled(subtitles_waiting);
                if (ImGui::Button("Download English subtitles", ImVec2(320.0f * ui_scale, 58.0f * ui_scale))) {
                    if (request_android_english_subtitles(settings_data_dir())) {
                        subtitles_waiting = true;
                        subtitle_message = "Connecting to GitHub and installing the subtitle files...";
                    } else {
                        subtitle_message = "Could not start the Android download service.";
                    }
                }
                ImGui::EndDisabled();
                if (subtitles_waiting) ImGui::TextDisabled("Downloading and checking subtitle files...");
                if (!subtitle_message.empty()) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", subtitle_message.c_str());
                }
                ImGui::Spacing();
                ImGui::TextWrapped("After starting the game, open F1 > Settings > Text / subtitles and select English (laingame.net fan subtitles).");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Unused")) {
                ImGui::BeginChild("android_unused_page", ImVec2(0, 0), false);
                gallery_draw_android(g_gallery);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        ImGui::Separator();
        const float footer_button_width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(!have_disc1 || busy || picker_waiting);
        if (ImGui::Button("Play", ImVec2(footer_button_width, 58.0f * ui_scale))) {
            settings_save(settings);
            play = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Exit", ImVec2(footer_button_width, 58.0f * ui_scale))) {
            running = false;
        }
        ImGui::End();

        ImGui::Render();
        int drawable_w = 0, drawable_h = 0;
        SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
        glViewport(0, 0, drawable_w, drawable_h);
        glClearColor(0.025f, 0.03f, 0.045f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    gallery_sound_close();
    gallery_release_gpu(g_gallery);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return play ? 1 : 0;
}
