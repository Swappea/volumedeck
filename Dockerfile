FROM ubuntu:22.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install base dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    zip \
    unzip \
    python3 \
    clang \
    lld \
    llvm \
    libxml2-dev \
    libssl-dev \
    libbz2-dev \
    zlib1g-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*

# Install MinGW for Windows cross-compilation
RUN apt-get update && apt-get install -y \
    mingw-w64 \
    g++-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*

# Install OSXCross for macOS cross-compilation
WORKDIR /opt
RUN git clone https://github.com/tpoechtrager/osxcross.git
WORKDIR /opt/osxcross

# Download macOS SDK from https://github.com/joseluisq/macosx-sdks/releases
# Using version 12.3 for best OSXCross compatibility
RUN mkdir -p tarballs && \
    cd tarballs && \
    wget https://github.com/joseluisq/macosx-sdks/releases/download/12.3/MacOSX12.3.sdk.tar.xz && \
    echo "3abd261ceb483c44295a6623fdffe5d44fc4ac2c872526576ec5ab5ad0f6e26c  MacOSX12.3.sdk.tar.xz" | sha256sum -c -

# Build OSXCross
RUN UNATTENDED=1 ./build.sh

# Set up environment
ENV PATH="/opt/osxcross/target/bin:${PATH}"
ENV OSXCROSS_MP_INC=1

# Create build workspace
WORKDIR /workspace

# Copy build script
COPY docker-build.sh /usr/local/bin/docker-build.sh
RUN chmod +x /usr/local/bin/docker-build.sh

CMD ["/usr/local/bin/docker-build.sh"]

