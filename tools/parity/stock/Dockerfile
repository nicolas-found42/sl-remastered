FROM --platform=linux/arm64 debian:bookworm-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates wget xvfb x11-utils x11-apps xauth mesa-utils xdotool pulseaudio pulseaudio-utils procps \
    libgl1-mesa-dri libgl1 libglx-mesa0 libxcomposite1 \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libav
RUN wget -q https://github.com/AndreRH/hangover/releases/download/hangover-11.16/hangover_11.16_debian12_bookworm_arm64.tar -O /tmp/hangover.tar && \
    echo 'dd7373202a168b7200cc36627a25b588511f3783756c2661bf4c022be8380aa8  /tmp/hangover.tar' | sha256sum -c - && \
    mkdir /tmp/hangover && tar -xf /tmp/hangover.tar -C /tmp/hangover && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends /tmp/hangover/*.deb && \
    rm -rf /tmp/hangover /tmp/hangover.tar && apt-get clean && rm -rf /var/lib/apt/lists/*
ENV WINEARCH=win64 HODLL=wowbox64.dll LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /work/game
