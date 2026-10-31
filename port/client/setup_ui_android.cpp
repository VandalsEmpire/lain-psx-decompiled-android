/* Android launcher: SDLActivity owns lifecycle; SDL2 provides the GLES context. */
#include "setup_ui.h"

#include <SDL.h>
#include <GLES3/gl3.h>

#include "discs.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

extern "C" int setup_ui_run(Settings *settings) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *window = SDL_CreateWindow(
        "Serial Experiments Lain", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
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
    ImGui::GetStyle().ScaleAllSizes(1.25f);
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    io.Fonts->AddFontDefault();

    bool running = true;
    bool play = false;
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Lain Android", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
        ImGui::TextUnformatted("SERIAL EXPERIMENTS LAIN");
        ImGui::Separator();
        ImGui::TextWrapped("Android native preview - SDL2 + OpenGL ES 3");
        ImGui::Spacing();

        char disc1[1024];
        const bool have_disc1 = discs_imported(settings_data_dir(), DISC_1, disc1, sizeof disc1);
        ImGui::TextWrapped("App data folder: %s", settings_data_dir());
        ImGui::Spacing();
        if (have_disc1) {
            ImGui::TextWrapped("Disc 1 detected: %s", disc1);
            if (ImGui::Button("Play", ImVec2(180, 54))) {
                settings_save(settings);
                play = true;
            }
        } else {
            ImGui::TextWrapped("Disc 1 has not been imported yet.");
            ImGui::TextWrapped("The Android Storage Access Framework picker is the next implementation step. For this preview, the native launcher is waiting for disc1.bin in the app data folder.");
        }
        ImGui::Spacing();
        if (ImGui::Button("Exit", ImVec2(180, 54))) {
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return play ? 1 : 0;
}
