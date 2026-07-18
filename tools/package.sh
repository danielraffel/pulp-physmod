#!/bin/bash
# Build all instruments and assemble a single component-selectable macOS
# installer (.pkg) — each instrument is a choice in the installer's Customize
# pane. Signs + notarizes when a Developer ID is supplied, otherwise produces an
# unsigned local-install package.
#
#   tools/package.sh --sdk /path/to/pulp-sdk [--sign-app HASH --sign-installer HASH] [--notarize]
#
# Signing/notarization reuses the canonical Pulp packaging recipe
# (build_combined_installer.sh) when PULP_REPO points at a Pulp checkout.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$HERE/release-version.txt")"
SDK=""; SIGN_APP=""; SIGN_INST=""; NOTARIZE=0
while [[ $# -gt 0 ]]; do case "$1" in
  --sdk) SDK="$2"; shift 2;;
  --sign-app) SIGN_APP="$2"; shift 2;;
  --sign-installer) SIGN_INST="$2"; shift 2;;
  --notarize) NOTARIZE=1; shift;;
  *) echo "unknown arg: $1" >&2; exit 2;; esac; done
[[ -n "$SDK" ]] || { echo "need --sdk /path/to/pulp-sdk (cmake --install a Pulp build)"; exit 2; }

cmake -S "$HERE" -B "$HERE/build" -DCMAKE_BUILD_TYPE=Release -DPulp_DIR="$SDK/lib/cmake/Pulp"
cmake --build "$HERE/build" -j "$(getconf _NPROCESSORS_ONLN)"

# The plugins bundle the runtime dylib; point its rpath at @loader_path so the
# bundle is relocatable (installs on any Mac, not just the build host).
for fmt in "AU:component" "CLAP:clap" "VST3:vst3"; do
  d="${fmt%%:*}"; ext="${fmt##*:}"
  for name in VaDrum PulpKit ModalInstrument PreparedPiano BowedString Gong; do
    bin="$HERE/build/$d/$name.$ext/Contents/MacOS/$name"
    [[ -f "$bin" && -f "$HERE/build/$d/$name.$ext/Contents/MacOS/libwgpu_native.dylib" ]] || continue
    install_name_tool -add_rpath @loader_path "$bin" 2>/dev/null || true
    for rp in $(otool -l "$bin" | awk '/LC_RPATH/{getline;getline;print $2}' | grep -iE 'Caches/Pulp|fetchcontent' || true); do
      install_name_tool -delete_rpath "$rp" "$bin" 2>/dev/null || true
    done
  done
done

PLUGINS=()
for name in VaDrum PulpKit ModalInstrument PreparedPiano BowedString Gong; do
  PLUGINS+=(--plugin au "$HERE/build/AU/$name.component"
            --plugin clap "$HERE/build/CLAP/$name.clap"
            --plugin vst3 "$HERE/build/VST3/$name.vst3")
done

if [[ -n "$SIGN_APP" && -n "$SIGN_INST" && -n "${PULP_REPO:-}" ]]; then
  args=(--name PulpPhysmod --version "$VERSION" --sign-identity "$SIGN_APP"
        --installer-identity "$SIGN_INST" --out "$HERE/dist" "${PLUGINS[@]}")
  [[ "$NOTARIZE" == 1 ]] || args+=(--no-notarize)
  bash "$PULP_REPO/tools/scripts/build_combined_installer.sh" "${args[@]}"
else
  echo "No signing identity / PULP_REPO: producing an UNSIGNED local package."
  echo "(Sign+notarize needs --sign-app/--sign-installer and PULP_REPO=<pulp checkout>.)"
  ROOT="$HERE/build/pkgroot"; rm -rf "$ROOT"
  for name in VaDrum PulpKit ModalInstrument PreparedPiano BowedString Gong; do
    mkdir -p "$ROOT/Library/Audio/Plug-Ins/"{Components,CLAP,VST3}
    cp -R "$HERE/build/AU/$name.component" "$ROOT/Library/Audio/Plug-Ins/Components/"
    cp -R "$HERE/build/CLAP/$name.clap"    "$ROOT/Library/Audio/Plug-Ins/CLAP/"
    cp -R "$HERE/build/VST3/$name.vst3"    "$ROOT/Library/Audio/Plug-Ins/VST3/"
  done
  mkdir -p "$HERE/dist"
  pkgbuild --root "$ROOT" --identifier com.pulp.physmod --version "$VERSION" \
           --install-location / "$HERE/dist/PulpPhysmod-$VERSION-unsigned.pkg"
  echo "wrote $HERE/dist/PulpPhysmod-$VERSION-unsigned.pkg"
fi
