FROM debian:bookworm-slim
RUN dpkg --add-architecture i386 && apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      wine wine32:i386 xvfb x11-utils x11-apps xauth mesa-utils procps libgl1-mesa-dri:i386 libgl1:i386 libglx-mesa0:i386 gstreamer1.0-plugins-base:i386 gstreamer1.0-plugins-good:i386 gstreamer1.0-libav:i386 ca-certificates && \
    apt-get clean && rm -rf /var/lib/apt/lists/*
ENV WINEARCH=win32 WINEPREFIX=/work/wineprefix LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /work/game
