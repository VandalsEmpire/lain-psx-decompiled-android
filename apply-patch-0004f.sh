#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

PUBS=port/psx/include/PsyX/PsyX_public.h
RENDER=port/psx/src/render/PsyX_render.cpp

echo "== HEADER =="
python3 - "$PUBS" <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

# Verifica se já foi corrigido
if '#ifdef __cplusplus\n} /* END_ANDROID_EXTERN_C */' in src or '/* ANDROID_C_LINKAGE_APPLIED */' in src:
    print("  já corrigido, pulando"); sys.exit(0)

old = '''#if defined(__ANDROID__)
extern SDL_Window *g_lain_launcher_window;
extern SDL_GLContext g_lain_launcher_context;
extern void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context);
#endif'''

new = '''#if defined(__ANDROID__)
/* ANDROID_C_LINKAGE_APPLIED: declare with C linkage so main.c finds them. */
#ifdef __cplusplus
extern "C" {
#endif
extern SDL_Window *g_lain_launcher_window;
extern SDL_GLContext g_lain_launcher_context;
extern void PsyX_SetReusedGLContext(SDL_Window *window, SDL_GLContext context);
#ifdef __cplusplus
} /* END_ANDROID_EXTERN_C */
#endif
#endif'''

if old not in src:
    sys.exit('ERRO: não achei o bloco exato. Roda:\n'
             '  sed -n "131,137p" ' + p + '\n'
             'e me manda a saída.')

src = src.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(src)
print("  OK: header agora envolve as 3 declarações em extern \"C\" condicional")
PY

echo "== CPP =="
python3 - "$RENDER" <<'PY'
import sys, re
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

if 'extern "C" void PsyX_SetReusedGLContext' in src:
    print("  cpp já corrigido, pulando"); sys.exit(0)

# Regex tolerante: aceita "void PsyX_SetReusedGLContext(...)" sem extern "C" antes
pat = re.compile(
    r'(?<!extern\s+"C"\s)void\s+PsyX_SetReusedGLContext\s*\('
)
m = pat.search(src)
if not m:
    sys.exit('ERRO: não achei a definição. Roda:\n'
             '  grep -n PsyX_SetReusedGLContext ' + p)

src = src[:m.start()] + 'extern "C" ' + src[m.start():]
open(p, "w", encoding="utf-8").write(src)
print("  OK: cpp agora define extern \"C\"")
PY

echo
echo "Confirmação (deve mostrar extern \"C\" em ambos):"
echo "--- header:"
sed -n '131,141p' "$PUBS"
echo "--- cpp:"
grep -n 'PsyX_SetReusedGLContext' "$RENDER" | tail -2