#!/usr/bin/env bash
# Run Android APK packaging in a Linux container (so androiddeployqt from _install runs).
# Usage: from the krita repo root:
#   packaging/android/docker/build-apk-in-docker.sh
# Or:
#   ./packaging/android/docker/build-apk-in-docker.sh
#
# Requires: Docker, and that you already ran the full Android build on the host so
# _build and _install exist.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Repo root: one level up from packaging/android/docker
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
IMAGE_NAME="krita-android-apk-builder"

cd "$REPO_ROOT"

ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
KDECI_ANDROID_ABI="${KDECI_ANDROID_ABI:-$ANDROID_ABI}"

if [[ ! -d _build ]] || [[ ! -d _install ]]; then
  echo "Error: _build and _install must exist. Run the full Android build first (run-ci-build.py)." >&2
  exit 1
fi

# Build image if missing or not amd64 (NDK only has linux-x86_64 host toolchain)
NEED_BUILD=0
if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
  NEED_BUILD=1
elif [ "$(docker image inspect --format '{{.Architecture}}' "$IMAGE_NAME" 2>/dev/null)" != "amd64" ]; then
  echo "Re-building $IMAGE_NAME for linux/amd64 (NDK requires x86_64 host toolchain)..."
  docker rmi "$IMAGE_NAME" 2>/dev/null || true
  NEED_BUILD=1
fi
if [ "$NEED_BUILD" = "1" ]; then
  echo "Building Docker image $IMAGE_NAME for linux/amd64 (one-time)..."
  docker build --platform linux/amd64 -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$SCRIPT_DIR"
fi

echo "Running create-apk in container (mounting repo at /krita)..."
# Use linux/amd64 so the NDK host toolchain (linux-x86_64 only) runs natively; on ARM Mac this uses emulation.
docker run --rm --platform linux/amd64 \
  -v "$REPO_ROOT":/krita \
  -e "ANDROID_ABI=$ANDROID_ABI" \
  -e "KDECI_ANDROID_ABI=$KDECI_ANDROID_ABI" \
  -e KRITA_INSTALL_PREFIX=/krita/_install \
  -e ANDROID_SDK_ROOT=/opt/android-sdk \
  -e ANDROID_NDK_ROOT=/opt/android-sdk/ndk/27.1.12297006 \
  -e KDECI_ANDROID_NDK_ROOT=/opt/android-sdk/ndk/27.1.12297006 \
  -e KDECI_ANDROID_SDK_ROOT=/opt/android-sdk \
  -e GRADLE_OPTS=-Xmx8g \
  -e CI_COMMIT_SHORT_SHA="${CI_COMMIT_SHORT_SHA:-$(git rev-parse --short HEAD 2>/dev/null || echo 'local')}" \
  -e HOST_REPO_PATH="$REPO_ROOT" \
  -w /krita/_build \
  "$IMAGE_NAME" \
  bash -c 'HP="${HOST_REPO_PATH}"
  if [ -f /krita/_install/bin/qmlimportscanner ]; then mv /krita/_install/bin/qmlimportscanner /krita/_install/bin/qmlimportscanner.real; printf '\''#!/bin/sh\necho \"[]\"\n'\'' > /krita/_install/bin/qmlimportscanner; chmod +x /krita/_install/bin/qmlimportscanner; fi
  case "$(uname -m)" in aarch64|arm64) NDK_PREBUILT=linux-aarch64;; *) NDK_PREBUILT=linux-x86_64;; esac
  [ -d "/opt/android-sdk/ndk/27.1.12297006/toolchains/llvm/prebuilt/${NDK_PREBUILT}" ] || NDK_PREBUILT=linux-x86_64
  NDK_SED="s#toolchains/llvm/prebuilt/darwin-arm64#toolchains/llvm/prebuilt/${NDK_PREBUILT}#g;s#toolchains/llvm/prebuilt/darwin-x86_64#toolchains/llvm/prebuilt/${NDK_PREBUILT}#g;s#toolchains/llvm/prebuilt/linux-x86_64#toolchains/llvm/prebuilt/${NDK_PREBUILT}#g;s#toolchains/llvm/prebuilt/linux-aarch64#toolchains/llvm/prebuilt/${NDK_PREBUILT}#g;s#toolchains/llvm/prebuilt/linux-x86_64_64#toolchains/llvm/prebuilt/${NDK_PREBUILT}#g"
  CMake_MODULES=$(dirname "$(ls /usr/share/cmake-3.*/Modules/CMakeCXXCompilerABI.cpp 2>/dev/null | head -1)" 2>/dev/null); [ -z "$CMake_MODULES" ] && CMake_MODULES=/usr/share/cmake/Modules
  CMAKE_MODULES_SED="s#/usr/share/cmake/Modules#${CMake_MODULES}#g"
  BINARY_DIR_SED="s#CMAKE_BINARY_DIR = /krita\$#CMAKE_BINARY_DIR = /krita/_build#g;s#/krita/libs/version/CMakeFiles#/krita/_build/libs/version/CMakeFiles#g;s#/krita/CMakeFiles #/krita/_build/CMakeFiles #g;s#cd /krita/libs/version #cd /krita/_build/libs/version #g;s#cd /krita &&#cd /krita/_build \&\&#g;s# /krita /krita/libs/version /krita /krita/libs/version # /krita /krita/libs/version /krita/_build /krita/_build/libs/version #g;s#/krita/libs/version/kritaversion_autogen#/krita/_build/libs/version/kritaversion_autogen#g"
  for f in CMakeCache.txt Makefile CMakeFiles/Makefile2; do [ -f "$f" ] && sed -i "s#${HP}#/krita#g;s#/opt/homebrew/bin/cmake#/usr/bin/cmake#g;s#/opt/homebrew/opt/qt@5/bin#/krita/_install/bin#g;${NDK_SED};${CMAKE_MODULES_SED};${BINARY_DIR_SED}" "$f"; done
  sed -i "s/CMakeFiles\/create-apk.dir\/rule: cmake_check_build_system/CMakeFiles\/create-apk.dir\/rule:/;s/CMakeFiles\/create-apk-krita.dir\/rule: cmake_check_build_system/CMakeFiles\/create-apk-krita.dir\/rule:/" CMakeFiles/Makefile2
  find . -type f \( -name "*.make" -o -name "Makefile" -o -name "Makefile.cmake" -o -name "DependInfo.cmake" -o -name "cmake_clean.cmake" -o -name "AutogenInfo.json" -o -name "*.json" -o -name "link.txt" -o -name "flags.make" -o -name "module-plugins" \) 2>/dev/null | while read f; do sed -i "s#${HP}#/krita#g;s#/opt/homebrew/bin/cmake#/usr/bin/cmake#g;s#/opt/homebrew/opt/qt@5/bin#/krita/_install/bin#g;s#/opt/homebrew/share/cmake#/usr/share/cmake#g;s#/Users/[^/]*/Library/Android/sdk#/opt/android-sdk#g;${NDK_SED};${CMAKE_MODULES_SED};${BINARY_DIR_SED}" "$f" 2>/dev/null; done
  DARWIN_SED="s#darwin-[a-z0-9]*#${NDK_PREBUILT}#g"
  for f in android_deployment_settings.json krita-deployment.json.in1 krita-deployment.json.in2 krita-deployment.json; do [ -f "$f" ] && sed -i "s#${HP}#/krita#g;s#/Users/[^/]*/Library/Android#/opt/android-sdk#g;${DARWIN_SED}" "$f"; done
  for f in krita-deployment.json.in1 krita-deployment.json.in2 krita-deployment.json android_deployment_settings.json; do
    [ -f "$f" ] && grep -v qml-import-paths "$f" > "$f.tmp" && mv "$f.tmp" "$f" || true
  done
  if [ ! -f CMakeFiles/Makefile2 ]; then echo "Error: CMakeFiles missing. On the host run: cd _build && cmake .. (with your Android toolchain) to regenerate."; exit 1; fi
  make create-apk'

APK_DIR="$REPO_ROOT/_build/krita_build_apk/build/outputs/apk/debug"
if [[ -d "$APK_DIR" ]]; then
  APK=$(find "$APK_DIR" -name "*.apk" 2>/dev/null | head -1)
  if [[ -n "$APK" ]]; then
    echo ""
    echo "APK built: $APK"
    echo "Install on device/emulator: adb install -r $APK"
  fi
else
  echo "APK dir not found at $APK_DIR; check _build/krita_build_apk for outputs."
fi
