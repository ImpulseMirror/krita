# Android build & deploy (this machine)

**Follow this.** Official reference: [Building Krita for Android](https://docs.krita.org/en/untranslatable_pages/building/build_krita_for_android.html).

Mac emulator, Linux build host. Do **not** edit Gradle/CMake to “fix” packaging. Do **not** use `androidbuild.sh` (`README.android.md` is stale).

## Perfect port source of truth

This docker is a **PERFECT PORT** of `krita-ai-diffusion`.

Reference clone: `~/source/krita/temp/krita-ai-diffusion`.

When asked to port, fix, or verify how functionality should behave, that clone is the only source of truth. Do not improvise or invent behavior. Read how the Python source implements the feature, then port the same behavior into C++ for `plugins/dockers/comfyui_remote` so it works in this docker.

## Layout

| What | Path |
|------|------|
| Source | `~/source/krita` |
| Workdir root | `~/source/krita/android-build-wd` |
| Build | `…/android-build-wd/krita/_build` |
| Install prefix | `…/android-build-wd/krita/_install` |
| Packaged APK | `…/android-build-wd/krita/_packaging/*.apk` |
| Mac emulator | `emulator-5554` via `adb-mac` |

`~/.bashrc` sets `KDECI_ANDROID_ABI`, SDK/NDK paths, and:

```shell
alias adb-mac='ADB_SERVER_SOCKET=tcp:127.0.0.1:15037 adb'
```

Mac session: emulator running → `adb devices` → `autossh -M 0 -N android-build-server`.

## Before every build

```shell
source ~/.bashrc
adb-mac devices   # must show emulator-5554
```

If empty, fix Mac (emulator / adb / autossh). Not a build problem.

## Env (every session)

```shell
source ~/.bashrc
export KDECI_WORKDIR_PATH=~/source/krita/android-build-wd
export KDECI_SHARED_INSTALL_PATH=~/source/krita/android-build-wd/krita/_install
export ANDROID_ABI="$KDECI_ANDROID_ABI"
export KRITA_INSTALL_PREFIX="$KDECI_SHARED_INSTALL_PATH"
export LD_LIBRARY_PATH="$KDECI_SHARED_INSTALL_PATH/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

## Full APK (after code changes)

**Must run `make install`.** Building only the `krita` target leaves bundles broken.

```shell
source ~/.bashrc
# …env block above…

cd ~/source/krita/android-build-wd/krita/_build
make -j"$(nproc)" install

cd ~/source/krita
python3 build-tools/ci-scripts/build-android-package.py
```

APK lands in `~/source/krita/android-build-wd/krita/_packaging/`.

## Deploy to Mac emulator

**Deploy = clear logcat + install APK + launch app.** `install -d -r` alone is incomplete — always run `am start` immediately after so you verify the build on device. Agents: never stop at install.

**Clear logcat before every deploy** (`adb-mac logcat -c`). Old buffer pollutes debug reads and wastes model context when agents grep logcat after a repro. Clearing does not touch app data.

**Default: upgrade in place.** Use `adb-mac install -d -r` only. That replaces the APK and **keeps** app data (ComfyUI server URL, styles, plugin settings, welcome state).

**Do not** run `pm clear` or uninstall for routine rebuild/deploy — those wipe `/data/data/org.krita.debug` and you redo setup.

```shell
source ~/.bashrc
adb-mac logcat -c
APK=$(ls -t ~/source/krita/android-build-wd/krita/_packaging/krita-arm64-v8a-*-debug.apk | head -1)
adb-mac install -d -r "$APK"
adb-mac shell am start -n org.krita.debug/org.krita.android.MainActivity
```

If `build-android-package.py` failed but `krita_build_apk` exists, APK may be under `_build/krita_build_apk/build/outputs/apk/debug/` — still **install + start**.

### One-shot: build → deploy → launch (plugin / dock changes)

```shell
source ~/.bashrc
export KDECI_WORKDIR_PATH=~/source/krita/android-build-wd
export KDECI_SHARED_INSTALL_PATH=~/source/krita/android-build-wd/krita/_install
export ANDROID_ABI="$KDECI_ANDROID_ABI"
export KRITA_INSTALL_PREFIX="$KDECI_SHARED_INSTALL_PATH"

cd ~/source/krita/android-build-wd/krita/_build
make -j"$(nproc)" install
cd ~/source/krita/android-build-wd/krita/_build/krita_build_apk
./gradlew assembleDebug -Dorg.gradle.jvmargs=-Xmx8192m
APK=$(ls -t build/outputs/apk/debug/*.apk | head -1)
adb-mac logcat -c
adb-mac install -d -r "$APK"
adb-mac shell am start -n org.krita.debug/org.krita.android.MainActivity
```

### Reset app data (rare)

Only when you **intentionally** want a clean slate (first-install assets, corrupted prefs, debugging migration). **Not** for normal agent deploy after code changes.

```shell
adb-mac logcat -c
adb-mac shell pm clear org.krita.debug   # wipes all app data + configs
adb-mac install -d -r "$APK"
adb-mac shell am start -n org.krita.debug/org.krita.android.MainActivity
```

Uninstall (`adb-mac uninstall org.krita.debug`) has the same effect as `pm clear` — avoid unless you mean to remove the app entirely.

## Fast iter loop (code tweak → emulator)

From the official guide — only after at least one full `make install` + `build-android-package.py` has created `krita_build_apk`:

```shell
source ~/.bashrc
export ANDROID_ABI="$KDECI_ANDROID_ABI"
export KRITA_INSTALL_PREFIX=~/source/krita/android-build-wd/krita/_install
export LD_LIBRARY_PATH="$KRITA_INSTALL_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cd ~/source/krita/android-build-wd/krita/_build/krita_build_apk
make -j"$(nproc)" install -C ..
./gradlew installDebug
adb-mac logcat -c
adb-mac shell am start -n org.krita.debug/org.krita.android.MainActivity
```

## One-time / rare: (re)configure CMake

Only if `_build` is new or options need changing. `BUILD_TESTING=ON` breaks `make install` on unit tests.

```shell
cd ~/source/krita/android-build-wd/krita/_build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DHIDE_SAFE_ASSERTS=OFF \
      -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=~/source/krita/android-build-wd/krita/_install \
      -DCMAKE_TOOLCHAIN_FILE=~/source/krita/krita-deps-management/tools/android-toolchain-krita.cmake \
      -DANDROID_ENABLE_STDIO_FORWARDING=ON \
      ~/source/krita/
```

## Reading logs (Mac emulator from Linux)

Always `source ~/.bashrc` first — use `adb-mac`, not local `adb`.

Deploy steps above run `adb-mac logcat -c` before install so post-deploy reads only contain output from the new build. When debugging without a fresh deploy, clear manually before reproducing.

```shell
# device up?
adb-mac devices

# live stream (Ctrl+C to stop)
adb-mac logcat

# last N lines, then exit
adb-mac logcat -d -t 200

# Krita native output (needs ANDROID_ENABLE_STDIO_FORWARDING=ON at cmake time)
adb-mac logcat -s krita:* ConfigsManager:* krita.MainActivity:*

# anything mentioning the package
adb-mac logcat -d | grep org.krita.debug

# clear buffer, repro bug, read fresh (also done automatically before deploy)
adb-mac logcat -c
# …use the app…
adb-mac logcat -d -t 300

# Krita file log on device (written under external app data)
adb-mac pull /sdcard/Android/data/org.krita.debug/files/krita.log /tmp/krita.log
adb-mac pull /sdcard/Android/data/org.krita.debug/files/krita-sysinfo.log /tmp/krita-sysinfo.log
```

Official guide: https://docs.krita.org/en/untranslatable_pages/building/build_krita_for_android.html#debugging-crashes

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| No resource bundles / empty brushes | You skipped `make install`. Run full APK steps above. |
| `adb-mac devices` empty | Mac emulator / tunnel — see Mac session above. |
| `make install` fails on `*Test.cpp` | Reconfigure with `-DBUILD_TESTING=OFF`. |
| `create-apk` / `:packageDebug` Java heap OOM | `cd …/_build/krita_build_apk && ./gradlew assembleDebug -Dorg.gradle.jvmargs=-Xmx8192m` — no config file edits. Never `gradlew clean`. |
| `msgfmt: libgettextsrc …` during build | `export LD_LIBRARY_PATH="$KRITA_INSTALL_PREFIX/lib:$LD_LIBRARY_PATH"`. |
| Stale bundled assets / plugin `data/` not updating after install | Try force-stop + relaunch first. Use `pm clear` only if you accept losing configs (see **Reset app data** above). |
| Lost ComfyUI settings after deploy | Deploy used `pm clear` or uninstall — use `install -d -r` only for routine updates. |

## Do not

- Deploy without `adb-mac logcat -c` first (stale logcat bloats agent context)
- Stop after `adb-mac install` without `am start` (deploy incomplete)
- `cmake --build . --target krita` then package (missing install step)
- `gradlew assembleDebug` alone without prior `build-android-package.py` / `create-apk`
- Edit `gradle.properties`, `build.gradle`, or CMake cache to fix packaging
- `gradlew clean` (breaks intermediates)
- `pm clear` or uninstall before routine deploy (wipes ComfyUI / plugin configs)
