FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    bc bison build-essential ca-certificates cpio curl file flex gawk git \
    libdrm-dev libelf-dev libncurses-dev libssl-dev patch perl pkg-config \
    python3 rsync unzip wget xz-utils \
    && rm -rf /var/lib/apt/lists/*
ENV FORCE_UNSAFE_CONFIGURE=1
