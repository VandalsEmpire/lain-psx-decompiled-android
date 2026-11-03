#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

PUBS=port/psx/include/PsyX/PsyX_public.h
SETUP_AND=port/client/setup_ui_android.cpp
MAIN_C=port/src/main.c
RENDER=port/psx/src/render/PsyX_render.cpp

for f in "$PUBS" "$SETUP_AND" "$MAIN_C" "$RENDER"; do
    [ -f "$f" ] || { echo "ERRO: não achei $f"; exit 1; }
    cp -v "$f" "$f.bak4"
done

echo "== 1/4 PsyX_public.h =="
python3 - "$PUBS" <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

if "PsyX_SetReusedGLContext" in src:
    print("  já tem declaração, pulando"); sys.exit(0)

# Adiciona declaração no fim (antes do último #ifdef __cplusplus ou no fim)
marker = "\n#ifdef __cplusplus\n}\n#endif"
if marker in src:
    insert = '''
#if defined(__ANDROID__)
/* Reused SDL resources from the launcher (Android): the launcher hands its
 * window and GL context to the game so we don't go through a Destroy+Create
 * cycle that leaves the SurfaceView empty long enough for Android to rotate
 * the display to portrait for one frame. */
extern SDL_Window *g_lain_launcher_window;
extern SDL_GLContext g_lain_launcher_context;
extern void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context);
#endif
'''
    src = src.replace(marker, insert + marker, 1)
else:
    src += '''
#if defined(__ANDROID__)
extern SDL_Window *g_lain_launcher_window;
extern SDL_GLContext g_lain_launcher_context;
extern void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context);
#endif
'''
open(p, "w", encoding="utf-8").write(src)
print("  OK")
PY

echo "== 2/4 setup_ui_android.cpp =="
python3 - "$SETUP_AND" <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

# Definir as globais logo após os includes (antes do primeiro 'static' ou '#define')
if "SDL_Window *g_lain_launcher_window" not in src:
    marker = "#include "
    idx = src.find("#include", src.rfind("\n\n", 0, src.find("static")))
    # Acha o último #include do bloco inicial
    last_inc = src.rfind('#include "', 0, src.find("static ", 200))
    if last_inc < 0:
        sys.exit("ERRO: não achei bloco de includes em setup_ui_android.cpp")
    end_of_last_inc = src.find("\n", last_inc) + 1
    insert = '''
#if defined(__ANDROID__)
/* Handed to PsyX (see PsyX_SetReusedGLContext) so the game reuses the
 * launcher's window+context instead of destroying and recreating them. */
SDL_Window *g_lain_launcher_window = NULL;
SDL_GLContext g_lain_launcher_context = NULL;
#endif
'''
    src = src[:end_of_last_inc] + insert + src[end_of_last_inc:]

# Substituir o bloco de deleção no fim
old = '''    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return play ? 1 : 0;'''
new = '''#if defined(__ANDROID__)
    if (play) {
        /* Give ownership to PsyX; do not destroy. */
        g_lain_launcher_window = window;
        g_lain_launcher_context = context;
    } else
#endif
    {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
    }
    return play ? 1 : 0;'''
if old not in src:
    sys.exit("ERRO: não achei bloco de deleção em setup_ui_android.cpp")
src = src.replace(old, new, 1)

open(p, "w", encoding="utf-8").write(src)
print("  OK")
PY

echo "== 3/4 main.c =="
python3 - "$MAIN_C" <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

old = '''    /* The launcher's window is gone; PsyCross opens the game window. */
    SDL_Quit();
    return boot_game(&settings, disc1, have2 ? disc2 : NULL);'''
new = '''#if defined(__ANDROID__)
    if (g_lain_launcher_window) {
        /* Hand the launcher's window and GL context to PsyX so the game
         * reuses them. No SDL_Quit here: it would leave the Activity's
         * SurfaceView empty long enough for Android to rotate the display
         * to portrait for one frame (the "flash" on Play). */
        PsyX_SetReusedGLContext(g_lain_launcher_window, g_lain_launcher_context);
    } else {
        SDL_Quit();
    }
#else
    SDL_Quit();
#endif
    return boot_game(&settings, disc1, have2 ? disc2 : NULL);'''

if old not in src:
    sys.exit("ERRO: não achei bloco final em main.c")
src = src.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(src)
print("  OK")
PY

echo "== 4/4 PsyX_render.cpp =="
python3 - "$RENDER" <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

# Adicionar globais + setter antes de GR_InitialiseGLContext
anchor = "int GR_InitialiseGLContext(char* windowName, int fullscreen)"
if anchor not in src:
    sys.exit("ERRO: não achei GR_InitialiseGLContext")

helper = '''#if defined(__ANDROID__)
static SDL_Window *g_reused_window = NULL;
static SDL_GLContext g_reused_context = NULL;

void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context) {
    g_reused_window = window;
    g_reused_context = context;
}
#endif

'''
if "PsyX_SetReusedGLContext" not in src:
    src = src.replace(anchor, helper + anchor, 1)

# No início da função, reusar se disponível
old_fn_start = '''int GR_InitialiseGLContext(char* windowName, int fullscreen)
{
        int windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI; // lain: HiDPI'''
new_fn_start = '''int GR_InitialiseGLContext(char* windowName, int fullscreen)
{
#if defined(__ANDROID__)
        if (g_reused_window) {
                g_window = g_reused_window;
                SDL_GL_MakeCurrent(g_window, g_reused_context);
                return 1;
        }
#endif
        int windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI; // lain: HiDPI'''
if old_fn_start not in src:
    sys.exit("ERRO: não achei início de GR_InitialiseGLContext")
src = src.replace(old_fn_start, new_fn_start, 1)

open(p, "w", encoding="utf-8").write(src)
print("  OK")
PY

echo
echo "Aplicado. Revise:"
echo "  git diff -- $PUBS $SETUP_AND $MAIN_C $RENDER"
echo
echo "Compile e teste:"
echo '  "$HOME/.local/opt/gradle-9.6.0/bin/gradle" --no-daemon --console=plain -p android assembleDebug'
echo
echo "Reverter: cp \$(ls ${PUBS}.bak4 ${SETUP_AND}.bak4 ${MAIN_C}.bak4 ${RENDER}.bak4) ..."