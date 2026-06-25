# Mac emulator + Linux build host (runbook)

**Superseded by [ANDROID_BUILD.md](../../ANDROID_BUILD.md) at repo root** (verified
workflow). This file is kept for tunnel/layout reference only.

**Read ANDROID_BUILD.md before building or deploying.** Emulator is on the Mac.
Build tree is on Linux. Do not edit Gradle/CMake configs to “fix” packaging.

## Layout

| Machine | What | Where |
|---------|------|-------|
| Mac | Android emulator | Android Studio → `emulator-5554` |
| Mac | adb | `127.0.0.1:5037` |
| Mac | SSH tunnel | `autossh -M 0 -N android-build-server` |
| Linux | SSH host | `android-build-server` |
| Linux | Mac adb (forwarded) | `127.0.0.1:15037` |
| Linux | Local adb | `127.0.0.1:5037` (ignore for deploy) |
| Linux | Source | `~/source/krita` |
| Linux | CMake build dir | `~/source/krita/android-build-wd/krita/_build` |
| Linux | Install prefix | `~/source/krita/android-build-wd/krita/_install` |
| Linux | Debug APK dir | `…/_build/krita_build_apk/build/outputs/apk/debug/` |

Mac `~/.ssh/config` for `android-build-server`:

```
RemoteForward 15037 127.0.0.1:5037
```

Linux `~/.bashrc` already sets Android env and:

```shell
alias adb-mac='ADB_SERVER_SOCKET=tcp:127.0.0.1:15037 adb'
```

## Mac — start of session

```shell
# 1) Emulator running in Android Studio
adb devices                    # must show emulator-5554

# 2) Tunnel (leave running)
autossh -M 0 -N android-build-server
```

## Linux — check emulator before anything else

```shell
source ~/.bashrc
adb-mac devices                # must show emulator-5554 device
adb-mac shell getprop ro.product.cpu.abi   # expect arm64-v8a
```

If empty → fix Mac side (emulator, adb, autossh). **Do not debug build until
this works.**

---

## Build APK (this setup)

Uses existing CMake tree under `android-build-wd`. Same as CI
(`build-tools/ci-scripts/build-android-package.py`): compile, then package.

**Do not:** edit `gradle.properties`, `build.gradle`, CMake cache, or run
`gradlew clean`. **Do not** use `androidbuild.sh` (removed; README.android.md
is stale).

### 1) Env (from `~/.bashrc` + CI)

```shell
source ~/.bashrc
BUILD_DIR=~/source/krita/android-build-wd/krita/_build
INSTALL=~/source/krita/android-build-wd/krita/_install
export ANDROID_ABI="${KDECI_ANDROID_ABI:-arm64-v8a}"
export KRITA_INSTALL_PREFIX="$INSTALL"
export KRITA_UNSTABLE_PACKAGE_SUFFIX="-$(git -C ~/source/krita rev-parse --short HEAD)"
export LD_LIBRARY_PATH="$INSTALL/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$BUILD_DIR"
```

### 2) Compile changed code

After C++ edits, rebuild what changed. For ComfyUI / plugin work:

```shell
cmake --build . --target kritacomfyuiremote_static kritacomfyuiremote krita -j"$(nproc)"
```

Full tree (optional; often fails on unit tests — **tests are not needed for APK**):

```shell
cmake --build . -j"$(nproc)"
```

### 3) Package APK

Try CI path first:

```shell
cmake --build . --target create-apk
```

If `:packageDebug` fails with **Java heap space** (OOM at 2 GB), **do not**
change repo or build-tree `gradle.properties`. Run Gradle once with a CLI heap
override (same as `create-apk`, no clean):

```shell
cd "$BUILD_DIR/krita_build_apk"
./gradlew --stop
./gradlew assembleDebug -Dorg.gradle.jvmargs=-Xmx8192m
```

APK name pattern:

```text
krita-arm64-v8a-5.4.0-prealpha-<git-sha>-debug.apk
```

Latest APK:

```shell
ls -t "$BUILD_DIR/krita_build_apk/build/outputs/apk/debug/"krita-arm64-v8a-*-debug.apk | head -1
```

---

## Deploy to Mac emulator

```shell
APK=$(ls -t ~/source/krita/android-build-wd/krita/_build/krita_build_apk/build/outputs/apk/debug/krita-arm64-v8a-*-debug.apk | head -1)
adb-mac install -d -r "$APK"
adb-mac shell am start -n org.krita.debug/org.krita.android.MainActivity
adb-mac logcat
```

---

## Quick checklist

```
Mac:   emulator → adb devices → autossh
Linux: adb-mac devices → compile targets → create-apk (or gradlew + heap CLI) → adb-mac install
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `remote port forwarding failed for listen port 15037` | Forward already active on another SSH session (OK), or kill stale `ssh`/`autossh`. |
| `adb-mac devices` empty | Mac emulator/adb/autossh — not a build problem. |
| Install OK, instant crash | ABI mismatch; rebuild for `arm64-v8a`. |
| `cmake --build . -j` fails on `*Test.cpp` | Ignore for APK; build plugin + `krita` targets only. |
| `create-apk` / `:packageDebug` OOM | `./gradlew assembleDebug -Dorg.gradle.jvmargs=-Xmx8192m` — **no config file edits**. |
| `msgfmt: libgettextsrc …` during build | `export LD_LIBRARY_PATH="$INSTALL/lib:$LD_LIBRARY_PATH"`. |
| `gradlew clean` → `configure` / `File#<init>` error | Never clean; intermediates are required. |
