#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

MANIFEST=android/app/src/main/AndroidManifest.xml
ACTIVITY=android/app/src/main/java/org/libsdl/app/LainActivity.java

echo "[1/3] Backup dos originais..."
cp -v "$MANIFEST"  "$MANIFEST.bak"
cp -v "$ACTIVITY"  "$ACTIVITY.bak"

echo "[2/3] Aplicando Manifest..."
cat > "$MANIFEST" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-feature
        android:glEsVersion="0x00030000"
        android:required="true" />
    <uses-feature
        android:name="android.hardware.touchscreen"
        android:required="false" />
    <uses-feature
        android:name="android.hardware.gamepad"
        android:required="false" />

    <uses-permission android:name="android.permission.VIBRATE" />
    <uses-permission android:name="android.permission.INTERNET" />

    <application
        android:allowBackup="false"
        android:label="re:Lain"
        android:icon="@mipmap/lain_launcher"
        android:roundIcon="@mipmap/lain_launcher"
        android:theme="@style/AppTheme"
        android:hardwareAccelerated="true">
        <activity
            android:name="org.libsdl.app.LainActivity"
            android:configChanges="keyboard|keyboardHidden|orientation|screenSize|smallestScreenSize|screenLayout|uiMode|navigation|density"
            android:exported="true"
            android:launchMode="singleTask"
            android:screenOrientation="landscape"
            android:resizeableActivity="false"
            android:alwaysRetainTaskState="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
XML

echo "[3/3] Aplicando LainActivity (patches pontuais)..."
python3 - "$ACTIVITY" <<'PY'
import sys, io, re
p = sys.argv[1]
src = open(p, encoding="utf-8").read()

# A) imports
anchor_import = "import android.content.Intent;\n"
if "import android.content.pm.ActivityInfo;" not in src:
    src = src.replace(
        anchor_import,
        anchor_import
        + "import android.content.pm.ActivityInfo;\n"
        + "import android.content.res.Configuration;\n",
        1,
    )
anchor_uri = "import android.net.Uri;\n"
if "import android.os.Bundle;" not in src:
    src = src.replace(
        anchor_uri,
        anchor_uri + "import android.os.Bundle;\n",
        1,
    )

# B) métodos de orientação
block = '''    // ---- Orientation enforcement (Sprint 1, item 1.1) ----------------------
    // Belt-and-suspenders: the manifest already declares landscape, but on
    // Android 12+ (especially multi-window, OEM skins and foldables) the
    // system may ignore it, and SDL may race us in onCreate. Re-asserting
    // after super.onCreate / onResume / onStart / focus changes keeps the
    // whole app locked to landscape without depending on native hints.

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        enforceLandscape();
    }

    @Override
    protected void onStart() {
        super.onStart();
        enforceLandscape();
    }

    @Override
    protected void onResume() {
        super.onResume();
        enforceLandscape();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (newConfig.orientation != Configuration.ORIENTATION_LANDSCAPE) {
            enforceLandscape();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enforceLandscape();
        }
    }

    private void enforceLandscape() {
        if (getRequestedOrientation() != ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE) {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        }
    }

'''

if "private void enforceLandscape()" not in src:
    marker = "    private String queryDisplayName(Uri uri) {"
    if marker not in src:
        sys.exit("ERRO: não achei o ponto de inserção em LainActivity.java")
    src = src.replace(marker, block + marker, 1)

open(p, "w", encoding="utf-8").write(src)
print("OK: LainActivity.java atualizado")
PY

echo
echo "Pronto. Revise com:"
echo "  git diff -- $MANIFEST $ACTIVITY"
echo
echo "Depois compile:"
echo "  cd android && ./gradlew assembleDebug"
echo
echo "Para reverter tudo:"
echo "  cp $MANIFEST.bak $MANIFEST && cp $ACTIVITY.bak $ACTIVITY"