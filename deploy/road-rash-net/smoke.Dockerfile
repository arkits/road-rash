# Ephemeral CI bot runtime. Verified disc files are a read-only runtime mount.
FROM debian:trixie
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make libsdl3-dev libssl-dev git python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt .
COPY cmake cmake
COPY src src
COPY server server
COPY third_party third_party
COPY tools tools
COPY tests tests
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DRR_SDL_HEADLESS=ON \
 && cmake --build /build --target net-bots -j2
ENTRYPOINT ["/build/net-bots"]
