#!/usr/bin/env bash
# Krita Android APK build on a Linux server (e.g. Nobara).
# Pulls the given branch (or master by default). No Android Studio required.
#
# Usage: ./run-build.sh [BRANCH]
#   BRANCH  optional; branch to build (default: master)
#   e.g.    ./run-build.sh              # build master
#           ./run-build.sh my-feature    # build my-feature
# For your fork: export KRITA_REPO_URL=https://invent.kde.org/YOUR_USERNAME/krita.git

set -e

SERVER_ROOT="${KRITA_SERVER_ROOT:-$HOME/source/krita_build_server}"
REPO_DIR="${SERVER_ROOT}/krita"
IMAGE_NAME="krita-android-apk-builder"
SCRIPT_IN_REPO="packaging/android/docker/build-apk-in-docker.sh"
KRITA_REPO_URL="${KRITA_REPO_URL:-git@github.com:ImpulseMirror/krita.git}"
# Branch: first argument overrides env/default
KRITA_BRANCH="${1:-${KRITA_BRANCH:-master}}"
# Use master for deps unless set; registry typically has deps for master only
DEPS_BRANCH="${DEPS_BRANCH:-master}"
ANDROID_ABI="${KDECI_ANDROID_ABI:-arm64-v8a}"

mkdir -p "$SERVER_ROOT"
cd "$SERVER_ROOT"

# Always log to a timestamped file (and still show output)
BUILD_LOG="${SERVER_ROOT}/build-$(date +%Y-%m-%d_%H-%M-%S).log"
exec > >(tee "$BUILD_LOG") 2>&1
echo "Logging to $BUILD_LOG"

# --- Clone or update repo (your fork's branch); skip if synced from local (SKIP_GIT_UPDATE=1) ---
if [[ -n "${SKIP_GIT_UPDATE:-}" ]]; then
  if [[ ! -d "$REPO_DIR/.git" ]]; then
    echo "Error: SKIP_GIT_UPDATE is set but $REPO_DIR is not a git repo. Sync the repo first." >&2
    exit 1
  fi
  echo "Using existing repo (SKIP_GIT_UPDATE); no fetch/pull."
elif [[ ! -d "$REPO_DIR/.git" ]]; then
  echo "Cloning Krita from ${KRITA_REPO_URL} (branch ${KRITA_BRANCH})..."
  git clone --depth 1 --branch "$KRITA_BRANCH" "$KRITA_REPO_URL" "$REPO_DIR"
  ( cd "$REPO_DIR" && git submodule update --init --recursive --depth 1 )
else
  echo "Updating Krita to latest ${KRITA_BRANCH}..."
  ( cd "$REPO_DIR" && git fetch origin "$KRITA_BRANCH" && ( git checkout "$KRITA_BRANCH" 2>/dev/null || git checkout -B "$KRITA_BRANCH" "origin/$KRITA_BRANCH" 2>/dev/null || git checkout -B "$KRITA_BRANCH" FETCH_HEAD ) && git pull --ff-only origin "$KRITA_BRANCH" )
  ( cd "$REPO_DIR" && git submodule update --init --recursive )
fi

cd "$REPO_DIR"
REPO_ROOT="$(pwd)"

# --- Ensure Docker image exists (linux/amd64 for NDK host tools) ---
DOCKER_DIR="$REPO_ROOT/packaging/android/docker"
if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
  echo "Building Docker image $IMAGE_NAME (one-time, linux/amd64)..."
  ( cd "$DOCKER_DIR" && docker build --platform linux/amd64 -t "$IMAGE_NAME" -f Dockerfile . )
elif [[ "$(docker image inspect --format '{{.Architecture}}' "$IMAGE_NAME" 2>/dev/null)" != "amd64" ]]; then
  echo "Re-building $IMAGE_NAME for linux/amd64..."
  docker rmi "$IMAGE_NAME" 2>/dev/null || true
  ( cd "$DOCKER_DIR" && docker build --platform linux/amd64 -t "$IMAGE_NAME" -f Dockerfile . )
fi

# --- If _build and _install exist, run APK step only ---
if [[ -d _build ]] && [[ -d _install ]]; then
  echo "Using existing _build and _install; running APK packaging..."
  "$REPO_ROOT/$SCRIPT_IN_REPO"
  APK_DIR="$REPO_ROOT/_build/krita_build_apk/build/outputs/apk/debug"
  if [[ -d "$APK_DIR" ]]; then
    APK=$(find "$APK_DIR" -name "*.apk" 2>/dev/null | head -1)
    [[ -n "$APK" ]] && echo "" && echo "APK: $APK" && echo "Install: adb install -r $APK"
  fi
  exit 0
fi

# --- Full build inside Docker (first run or after clean) ---
echo "No _build/_install found. Running full Android build in Docker (this can take a long time)..."
mkdir -p "$REPO_ROOT/_ci_cache"
export ANDROID_ABI
export KDECI_ANDROID_ABI="$ANDROID_ABI"
export KDECI_ANDROID_SDK_ROOT=/opt/android-sdk
export KDECI_ANDROID_NDK_ROOT=/opt/android-sdk/ndk/27.1.12297006
export KDECI_CACHE_PATH=/krita/_ci_cache
export KDECI_GITLAB_SERVER="${KDECI_GITLAB_SERVER:-https://invent.kde.org/}"
export KDECI_PACKAGE_PROJECT="${KDECI_PACKAGE_PROJECT:-teams/ci-artifacts/krita-android-${ANDROID_ABI}}"
export KDECI_SKIP_ECM_ANDROID_TOOLCHAIN=True
# Use path inside container (repo is mounted at /krita)
export CMAKE_TOOLCHAIN_FILE=/krita/krita-deps-management/tools/android-toolchain-krita.cmake
export CI_COMMIT_SHORT_SHA="${CI_COMMIT_SHORT_SHA:-$(git rev-parse --short HEAD 2>/dev/null || echo 'local')}"

docker run --rm --platform linux/amd64 \
  -v "$REPO_ROOT":/krita \
  -e KDECI_ANDROID_ABI \
  -e KDECI_ANDROID_SDK_ROOT \
  -e KDECI_ANDROID_NDK_ROOT \
  -e KDECI_CACHE_PATH \
  -e KDECI_GITLAB_SERVER \
  -e KDECI_PACKAGE_PROJECT \
  -e KDECI_SKIP_ECM_ANDROID_TOOLCHAIN \
  -e CMAKE_TOOLCHAIN_FILE \
  -e ANDROID_ABI \
  -e CI_COMMIT_SHORT_SHA \
  -e GRADLE_OPTS=-Xmx8g \
  -e KRITA_INSTALL_PREFIX=/krita/_install \
  -e ANDROID_SDK_ROOT=/opt/android-sdk \
  -e ANDROID_NDK_ROOT=/opt/android-sdk/ndk/27.1.12297006 \
  -w /krita \
  "$IMAGE_NAME" \
  bash -c '
    set -e
    cd /krita
    source /opt/conda/etc/profile.d/conda.sh
    conda activate krita || ( conda create -n krita python=3.10 -y && conda activate krita )
    pip install -q -r krita-deps-management/requirements.txt
    python krita-deps-management/tools/replace-branch-in-seed-file.py \
      krita-deps-management/latest/krita-deps.yml -p -o branch-corrected-deps.yml -d '"$DEPS_BRANCH"' -s master 2>/dev/null || \
      python krita-deps-management/tools/replace-branch-in-seed-file.py \
      krita-deps-management/latest/krita-deps.yml -p -o branch-corrected-deps.yml -d '"$DEPS_BRANCH"'
    python krita-deps-management/tools/generate-deps-file.py -s branch-corrected-deps.yml -o .kde-ci.yml
    python -u krita-deps-management/ci-utilities/run-ci-build.py \
      --project krita --branch '"$KRITA_BRANCH"' --platform Android/'"$ANDROID_ABI"'/Qt5/Shared \
      --only-build --skip-publishing
    # Stub qmlimportscanner after _install exists (it segfaults in Docker/headless; QTBUG-81477, QTBUG-55259)
    if [ -f /krita/_install/bin/qmlimportscanner ]; then mv /krita/_install/bin/qmlimportscanner /krita/_install/bin/qmlimportscanner.real; printf '\''#!/bin/sh\necho \"[]\"\n'\'' > /krita/_install/bin/qmlimportscanner; chmod +x /krita/_install/bin/qmlimportscanner; fi
    cd _build
    # Strip qml-import-paths from deployment json so androiddeployqt does not run qmlimportscanner
    for f in krita-deployment.json.in1 krita-deployment.json.in2 krita-deployment.json android_deployment_settings.json; do
      [ -f "$f" ] && grep -v qml-import-paths "$f" > "$f.tmp" && mv "$f.tmp" "$f" || true
    done
    make create-apk
  '

APK_DIR="$REPO_ROOT/_build/krita_build_apk/build/outputs/apk/debug"
if [[ -d "$APK_DIR" ]]; then
  APK=$(find "$APK_DIR" -name "*.apk" 2>/dev/null | head -1)
  [[ -n "$APK" ]] && echo "" && echo "APK: $APK" && echo "Install: adb install -r $APK"
else
  echo "APK dir not found at $APK_DIR; check logs above."
  exit 1
fi
