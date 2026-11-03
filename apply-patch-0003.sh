#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ACTIVITY=android/app/src/main/java/org/libsdl/app/LainActivity.java
cp -v "$ACTIVITY" "$ACTIVITY.bak3"

python3 - "$ACTIVITY" <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

# 1) import View
if "import android.view.View;" not in src:
    anchor = "import android.provider.DocumentsContract.Document;\n"
    if anchor not in src:
        sys.exit("ERRO: não achei o anchor de import")
    src = src.replace(anchor, anchor + "import android.view.View;\n", 1)

# 2) substitui onCreate
old = '''    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        enforceLandscape();
    }'''
new = '''    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        enforceLandscape();
        enterImmersiveFullscreen();
    }'''
if old not in src:
    sys.exit("ERRO: não achei onCreate")
src = src.replace(old, new, 1)

# 3) substitui onResume
old = '''    @Override
    protected void onResume() {
        super.onResume();
        enforceLandscape();
    }'''
new = '''    @Override
    protected void onResume() {
        super.onResume();
        enforceLandscape();
        enterImmersiveFullscreen();
    }'''
if old not in src:
    sys.exit("ERRO: não achei onResume")
src = src.replace(old, new, 1)

# 4) substitui onWindowFocusChanged + adiciona enterImmersiveFullscreen
old = '''    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enforceLandscape();
        }
    }'''
new = '''    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enforceLandscape();
            enterImmersiveFullscreen();
        }
    }

    /**
     * Hides the status and navigation bars from the moment the activity
     * is created, so the launcher -> game transition does not trigger
     * Android's "screenshot rotation" animation (which shows one portrait
     * frame while the system bars animate away).
     */
    private void enterImmersiveFullscreen() {
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
        );
    }'''
if old not in src:
    sys.exit("ERRO: não achei onWindowFocusChanged")
src = src.replace(old, new, 1)

open(p, "w", encoding="utf-8").write(src)
print("OK: LainActivity.java atualizado")
PY

echo
echo "Compile:"
echo '  "$HOME/.local/opt/gradle-9.6.0/bin/gradle" --no-daemon --console=plain -p android assembleDebug'
echo
echo "Reverter: cp $ACTIVITY.bak3 $ACTIVITY"