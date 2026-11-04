#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
javac -d "$out" \
  android/app/src/main/java/org/libsdl/app/ModArchiveInstaller.java \
  tools/tests/ModArchiveInstallerTest.java
java -cp "$out" org.libsdl.app.ModArchiveInstallerTest
