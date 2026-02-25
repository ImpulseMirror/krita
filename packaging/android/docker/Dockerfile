# Linux image for running Android APK packaging (create-apk + gradle).
# Use from macOS: build Krita on the host, then run this to produce the APK
# because androiddeployqt in _install is a Linux binary.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV ANDROID_SDK_ROOT=/opt/android-sdk

RUN apt-get update && apt-get install -y --no-install-recommends \
    openjdk-17-jdk \
    cmake \
    make \
    python3 \
    python3-pip \
    unzip \
    wget \
    binutils \
    file \
    && rm -rf /var/lib/apt/lists/*

# Miniforge (conda-forge) for a dedicated Python env (avoids "pip as root", no ToS prompt)
ENV CONDA_DIR=/opt/conda
RUN wget -q https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-x86_64.sh -O /tmp/miniforge.sh \
    && bash /tmp/miniforge.sh -b -p ${CONDA_DIR} \
    && rm /tmp/miniforge.sh \
    && ${CONDA_DIR}/bin/conda create -n krita python=3.10 -y \
    && ${CONDA_DIR}/bin/conda clean -afy
ENV PATH="${CONDA_DIR}/bin:${PATH}"

# Android command-line tools
RUN mkdir -p ${ANDROID_SDK_ROOT}/cmdline-tools \
    && wget -q https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip -O /tmp/cmdline-tools.zip \
    && unzip -q /tmp/cmdline-tools.zip -d ${ANDROID_SDK_ROOT}/cmdline-tools \
    && mv ${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools ${ANDROID_SDK_ROOT}/cmdline-tools/latest \
    && rm /tmp/cmdline-tools.zip

ENV PATH="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}"

# Accept licenses and install SDK components (NDK 27.x, build-tools, platform)
RUN yes | sdkmanager --licenses 2>/dev/null || true \
    && sdkmanager "platform-tools" \
    && sdkmanager "platforms;android-35" \
    && sdkmanager "build-tools;35.0.0" \
    && sdkmanager "ndk;27.1.12297006"

# Default: run create-apk then copy APK path to stdout
WORKDIR /krita
