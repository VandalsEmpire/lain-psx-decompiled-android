#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

RENDER=port/psx/src/render/PsyX_render.cpp

if [ ! -f "$RENDER.bak4" ]; then
    cp -v "$RENDER" "$RENDER.bak4"
fi

python3 - "$RENDER" <<'PY'
import sys, re
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

# 1) Insere o setter antes de GR_InitialiseGLContext (regex tolerante a espaços)
m = re.search(
    r'^[ \t]*int[ \t]+GR_InitialiseGLContext[ \t]*\([ \t]*char[ \t]*\*[ \t]*windowName[ \t]*,[ \t]*int[ \t]+fullscreen[ \t]*\)',
    src, re.M
)
if not m:
    sys.exit("ERRO: não achei a assinatura de GR_InitialiseGLContext")

if "PsyX_SetReusedGLContext" not in src:
    helper = '''#if defined(__ANDROID__)
static SDL_Window *g_reused_window = NULL;
static SDL_GLContext g_reused_context = NULL;

void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context) {
    g_reused_window = window;
    g_reused_context = context;
}
#endif

'''
    src = src[:m.start()] + helper + src[m.start():]

# 2) Rebusca (offset mudou) e insere early-return dentro da função
m2 = re.search(
    r'^[ \t]*int[ \t]+GR_InitialiseGLContext[ \t]*\([^)]*\)',
    src, re.M
)
if not m2:
    sys.exit("ERRO: perdeu a assinatura após inserção")

# Acha a '{' logo após a assinatura
brace = src.find('{', m2.end())
if brace < 0:
    sys.exit("ERRO: não achei a '{' da função")

early = '''
#if defined(__ANDROID__)
        if (g_reused_window) {
                g_window = g_reused_window;
                SDL_GL_MakeCurrent(g_window, g_reused_context);
                return 1;
        }
#endif
'''
# Confirma que a early ainda não está lá
if "if (g_reused_window)" not in src:
    src = src[:brace+1] + early + src[brace+1:]

open(p, "w", encoding="utf-8").write(src)
print("OK PsyX_render.cpp")
PY

echo
echo "Verificação:"
echo "  grep -n 'PsyX_SetReusedGLContext\\|g_reused_window' $RENDER"
echo
echo "Diff completo:"
echo "  git diff -- port/psx/include/PsyX/PsyX_public.h port/client/setup_ui_android.cpp port/src/main.c port/psx/src/render/PsyX_render.cpp"