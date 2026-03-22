# syntax=docker/dockerfile:1

FROM alpine:edge AS build

RUN apk add --no-cache \
    build-base \
    cmake \
    ca-certificates

WORKDIR /app

COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests

RUN cmake -S . -B build -DINFO_GETTER_BUILD_TESTS=OFF \
    && cmake --build build -j"$(nproc)"

FROM alpine:edge AS runtime

RUN apk add --no-cache ca-certificates libstdc++

WORKDIR /app

COPY --from=build /app/build/info_getter_server /usr/local/bin/info_getter_server

RUN mkdir -p /data

ENV INFO_GETTER_HOST=0.0.0.0
ENV INFO_GETTER_PORT=8080
ENV INFO_GETTER_DB_PATH=/data/info_getter.db

EXPOSE 8080

CMD ["/usr/local/bin/info_getter_server"]
