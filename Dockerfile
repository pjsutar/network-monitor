 # =============================================================================
  # Build Stage
  # =============================================================================
  FROM ubuntu:22.04 AS builder

  # Avoid interactive prompts during package installation
  ENV DEBIAN_FRONTEND=noninteractive

  # Install build dependencies
  RUN apt-get update && apt-get install -y \
      cmake \
      ninja-build \
      g++ \
      python3-pip \
      git \
      && rm -rf /var/lib/apt/lists/*

  # Install Conan
  RUN pip install conan==1.64.0 \
      && conan profile new default --detect \
      && conan profile update settings.compiler.libcxx=libstdc++11 default

  # Set working directory
  WORKDIR /app

  # Copy dependency files first (better layer caching)
  COPY conanfile.py conanprofile.toml ./

  # Install dependencies (this layer is cached if conanfile.py doesn't change)
  RUN mkdir build && cd build \
      && conan install .. --build=missing

  # Copy source code
  COPY CMakeLists.txt ./
  COPY cmake/ cmake/
  COPY inc/ inc/
  COPY src/ src/

  # Build the application
  RUN cd build \
      && cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release \
      && ninja network-monitor-exe

  # =============================================================================
  # Runtime Stage
  # =============================================================================
  FROM ubuntu:22.04 AS runtime

  # Install only runtime dependencies
  RUN apt-get update && apt-get install -y \
      ca-certificates \
      && rm -rf /var/lib/apt/lists/*

  # Create non-root user for security
  RUN useradd --create-home --shell /bin/bash appuser

  # Set working directory
  WORKDIR /app

  # Copy the built executable from builder stage
  COPY --from=builder /app/build/network-monitor-exe .

  # Copy runtime data files
  COPY tests/cacert.pem .
  COPY tests/network-layout.json .

  # Change ownership to non-root user
  RUN chown -R appuser:appuser /app

  # Switch to non-root user
  USER appuser

  # Default environment variables (can be overridden at runtime)
  ENV LTNM_SERVER_URL=ltnm.learncppthroughprojects.com
  ENV LTNM_SERVER_PORT=443
  ENV LTNM_CACERT_PATH=/app/cacert.pem
  ENV LTNM_NETWORK_LAYOUT_FILE_PATH=/app/network-layout.json
  ENV LTNM_TIMEOUT_MS=0

  # Expose the quiet-route server port
  EXPOSE 8042

  # Run the application
  CMD ["./network-monitor-exe"]