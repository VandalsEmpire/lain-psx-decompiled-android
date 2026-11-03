#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

RENDER=port/psx/src/render/PsyX_render.cpp

# 1) Garante que o header está incluído (necessário pra herdar linkage C)
if ! grep -qE '#include.*PsyX_public\.h' "$RENDER"; then
    echo "  header PsyX_public.h não incluído, adicionando..."
    # Acha o primeiro #include e coloca antes
    FIRST_INC=$(grep -n "^#include" "$RENDER" | head -1 | cut -d: -f1)
    if [ -z "$FIRST_INC" ]; then
        echo "ERRO: não achei #include"
        exit 1
    fi
    sed -i "${FIRST_INC}i #include \"PsyX/PsyX_public.h\"" "$RENDER"
else
    echo "  header PsyX_public.h já incluído ✅"
fi

# 2) Remove extern "C" da definição (header já declara como C)
python3 - "$RENDER" <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

old = '''extern "C" void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context) {
    g_reused_window = window;
    g_reused_context = context;
}'''

new = '''/* Linkage inherited from PsyX_public.h (extern "C" block). */
void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context) {
    g_reused_window = window;
    g_reused_context = context;
}'''

if old not in src:
    sys.exit('ERRO: não achei a definição com extern "C". Confere com: grep -n "PsyX_SetReusedGLContext" ' + p)

src = src.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(src)
print("OK: extern \"C\" removido da definição")
PY

echo
echo "Confirma:"
echo "  grep -n 'PsyX_SetReusedGLContext\\|#include.*PsyX_public' $RENDER | head -5"