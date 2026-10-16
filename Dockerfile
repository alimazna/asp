# ASTRA backend container for Railway.
# Builds the Linux AURA/ASTRA engine and hosts astra_server on Railway's PORT.
FROM ubuntu:22.04 AS build

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt ./
COPY src ./src
RUN cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build-linux --target astra_server -j"$(nproc)"

FROM ubuntu:22.04 AS runtime
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/astra
COPY --from=build /build/build-linux/astra_server ./astra_server

# Runtime data directory (Railway volume mount goes here).
ENV ASTRA_DATA_DIR=/opt/astra/data

EXPOSE 8080
ENTRYPOINT ["./astra_server"]
CMD ["--host=0.0.0.0"]