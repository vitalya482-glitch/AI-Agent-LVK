FROM ubuntu:24.04

ARG SDL2_VERSION=2.30.0
ARG SDL2_IMAGE_VERSION=2.8.2
ARG SDL2_TTF_VERSION=2.22.0
ARG SDL2_SHA256=83a97f6e53d0ca78e71502efb9a704db6a5b5abca016a17ce66bdb099fa2f925
ARG SDL2_IMAGE_SHA256=864d86e98d91e36ac2ae536b0f9945fba29045ac814478103ab56ed79d568818
ARG SDL2_TTF_SHA256=4b864e9698439643222b593a15071af88604c74f1d8ba35e1fcc138b0c3932b9

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    file \
    git \
    ninja-build \
    pkg-config \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-ttf-dev \
    libncurses-dev \
    libssl-dev \
    zlib1g-dev \
    libboost-all-dev \
    mingw-w64 \
    perl \
    unzip \
    && mkdir -p /opt/ai-agent/mingw64 /tmp/ai-agent-sdl \
    && curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.zip" -o /tmp/ai-agent-sdl/SDL2.zip \
    && curl -fsSL "https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL2_IMAGE_VERSION}/SDL2_image-devel-${SDL2_IMAGE_VERSION}-mingw.zip" -o /tmp/ai-agent-sdl/SDL2_image.zip \
    && curl -fsSL "https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL2_TTF_VERSION}/SDL2_ttf-devel-${SDL2_TTF_VERSION}-mingw.zip" -o /tmp/ai-agent-sdl/SDL2_ttf.zip \
    && echo "${SDL2_SHA256}  /tmp/ai-agent-sdl/SDL2.zip" | sha256sum -c - \
    && echo "${SDL2_IMAGE_SHA256}  /tmp/ai-agent-sdl/SDL2_image.zip" | sha256sum -c - \
    && echo "${SDL2_TTF_SHA256}  /tmp/ai-agent-sdl/SDL2_ttf.zip" | sha256sum -c - \
    && unzip -q /tmp/ai-agent-sdl/SDL2.zip -d /tmp/ai-agent-sdl \
    && unzip -q /tmp/ai-agent-sdl/SDL2_image.zip -d /tmp/ai-agent-sdl \
    && unzip -q /tmp/ai-agent-sdl/SDL2_ttf.zip -d /tmp/ai-agent-sdl \
    && cp -a "/tmp/ai-agent-sdl/SDL2-${SDL2_VERSION}/x86_64-w64-mingw32/." /opt/ai-agent/mingw64/ \
    && cp -a "/tmp/ai-agent-sdl/SDL2_image-${SDL2_IMAGE_VERSION}/x86_64-w64-mingw32/." /opt/ai-agent/mingw64/ \
    && cp -a "/tmp/ai-agent-sdl/SDL2_ttf-${SDL2_TTF_VERSION}/x86_64-w64-mingw32/." /opt/ai-agent/mingw64/ \
    && rm -rf /tmp/ai-agent-sdl /var/lib/apt/lists/*

WORKDIR /workspace
ENV AGENT_WORKSPACE=/workspace
ENV MINGW_SDL2_ROOT=/opt/ai-agent/mingw64
COPY mingw64.cmake /opt/ai-agent/mingw64.cmake
COPY audit-cpp-sources /usr/local/bin/audit-cpp-sources
COPY build-windows-sdl2 /usr/local/bin/build-windows-sdl2
RUN chmod +x /usr/local/bin/audit-cpp-sources /usr/local/bin/build-windows-sdl2
COPY AGENTS.md /etc/ai-agent/AGENTS.md
