# Android APK packaging in Docker (macOS host)

Use this when you build Krita for Android on **macOS**: the native build works, but `androiddeployqt` in `_install` is a Linux binary, so the APK step must run in Linux.

## Prerequisites

- Docker installed and running.
- Krita Android build already done on the host: `_build` and `_install` must exist (from `run-ci-build.py`).

## Usage

From the **krita repo root**:

```bash
./packaging/android/docker/build-apk-in-docker.sh
```

First run builds the Docker image (Android SDK + NDK + JDK 17, one-time). Then it runs `cmake --build . --target create-apk` inside the container with your repo mounted.

## Output

The debug APK is written to:

- `_build/krita_build_apk/build/outputs/apk/debug/krita_build_apk-debug.apk`

Install on a device or emulator:

```bash
adb install -r _build/krita_build_apk/build/outputs/apk/debug/krita_build_apk-debug.apk
```

## NDK version

The image installs NDK 27.1.12297006. If Gradle complains about NDK version, edit `Dockerfile` and change the `sdkmanager "ndk;..."` line to the required version, then rebuild the image: `docker build -t krita-android-apk-builder ...`.
