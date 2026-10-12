# podman build -t hft . (or docker); demos, reports in ./out: podman run --rm -v "$PWD/out:/hft/out" hft
# Toolchain for writing strategies (also .devcontainer): podman build --target build -t hft-dev .

FROM ubuntu:24.04 AS build
ARG VCPKG_COMMIT=001713e802f5cf619a64461b918161be6a0634f0
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ cmake ninja-build git curl zip unzip tar pkg-config ca-certificates libssl-dev linux-libc-dev \
    && rm -rf /var/lib/apt/lists/*
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout -q "$VCPKG_COMMIT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
WORKDIR /src
# Dependencies first, so editing the sources does not rebuild them.
COPY vcpkg.json CMakePresets.json ./
RUN "$VCPKG_ROOT/vcpkg" install --x-install-root=/opt/vcpkg_installed --triplet x64-linux
COPY . .
RUN cmake --preset release -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed \
    && cmake --build build/release \
    && ctest --test-dir build/release --output-on-failure -j"$(nproc)"

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libssl3t64 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /hft
COPY --from=build /src/build/release/apps/ build/release/apps/
COPY --from=build /src/scripts/ scripts/
COPY --from=build /src/config/ config/
COPY --from=build /src/data/sample/ data/sample/
COPY --from=build /src/docs/STRATEGIES.md /src/README.md ./
RUN find build/release/apps -mindepth 1 -maxdepth 1 ! -type f -exec rm -rf {} +
ENV PATH=/hft/scripts:/hft/build/release/apps:$PATH HFT_BIN=/hft/build/release/apps
CMD ["scripts/demo/all.sh", "/hft/out"]
