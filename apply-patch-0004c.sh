#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

RENDER=port/psx/src/render/PsyX_render.cpp

python3 - "$RENDER" <<'PY'
import sys, re
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

# Já tem extern "C"? Então não mexe
if 'extern "C" void PsyX_SetReusedGLContext' in src:
    print("já está em extern \"C\", pulando")
    sys.exit(0)

old = '''void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context) {
    g_reused_window = window;
    g_reused_context = context;
}'''

new = '''extern "C" void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context) {
    g_reused_window = window;
    g_reused_context = context;
}'''

if old not in src:
    sys.exit('ERRO: não achei o setter exato. Roda "grep -n PsyX_SetReusedGLContext port/psx/src/render/PsyX_render.cpp" e me manda')

src = src.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(src)
print("OK: setter agora em extern \"C\"")
PY

echo
echo "Confirma:"
echo "  grep -n 'extern \"C\" void PsyX_SetReusedGLContext' port/psx/src/render/PsyX_render.cpp"