FROM debian:bookworm-slim AS base

ARG ZIG_VERSION=0.11.0
ARG SQLITE_VERSION=3530000
ARG SQLITE_YEAR=2026

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl xz-utils ca-certificates unzip \
  && rm -rf /var/lib/apt/lists/*

RUN ARCH=$(uname -m) \
  && mkdir /opt/zig \
  && curl -fsSL "https://ziglang.org/download/${ZIG_VERSION}/zig-linux-${ARCH}-${ZIG_VERSION}.tar.xz" \
     | tar -xJ --strip-components=1 -C /opt/zig \
  && ln -s /opt/zig/zig /usr/local/bin/zig

WORKDIR /build
RUN curl -fsSL "https://www.sqlite.org/${SQLITE_YEAR}/sqlite-amalgamation-${SQLITE_VERSION}.zip" \
     -o sqlite.zip \
  && unzip -q sqlite.zip \
  && mv sqlite-amalgamation-${SQLITE_VERSION} sqlite \
  && rm sqlite.zip

COPY CRoaring /build/CRoaring
RUN mkdir -p /build/roaringlite/src \
  && cd /build/CRoaring && ./amalgamation.sh /build/roaringlite/src

COPY src/libsqlite3roaring.c /build/roaringlite/src/libsqlite3roaring.c

RUN mkdir -p /out/lib

ENV CFLAGS="-O3 \
    -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_ENABLE_RTREE \
    -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_ENABLE_MATH_FUNCTIONS \
    -DSQLITE_ENABLE_DBSTAT_VTAB \
    -DSQLITE_MAX_EXPR_DEPTH=0 \
    -DSQLITE_OMIT_DEPRECATED \
    -DSQLITE_DQS=0 \
    -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 \
    -DSQLITE_MAX_MMAP_SIZE=2147483648 \
    -fPIC"


FROM base AS build-macos-x86_64
RUN zig cc -target x86_64-macos $CFLAGS -shared -fvisibility=hidden \
      /build/sqlite/sqlite3.c -o /out/lib/libsqlite3-macos-x86_64.dylib \
 && zig cc -target x86_64-macos -O3 -shared -fPIC -fvisibility=hidden \
      -I /build/sqlite -I /build/roaringlite/src \
      /build/roaringlite/src/libsqlite3roaring.c \
      -o /out/lib/roaringlite-macos-x86_64.dylib

FROM base AS build-macos-aarch64
RUN zig cc -target aarch64-macos $CFLAGS -shared -fvisibility=hidden \
      /build/sqlite/sqlite3.c -o /out/lib/libsqlite3-macos-aarch64.dylib \
 && zig cc -target aarch64-macos -O3 -shared -fPIC -fvisibility=hidden \
      -I /build/sqlite -I /build/roaringlite/src \
      /build/roaringlite/src/libsqlite3roaring.c \
      -o /out/lib/roaringlite-macos-aarch64.dylib

FROM base AS build-linux-x86_64
RUN zig cc -target x86_64-linux $CFLAGS -shared -fvisibility=hidden \
      /build/sqlite/sqlite3.c -o /out/lib/libsqlite3-linux-x86_64.so \
 && zig cc -target x86_64-linux -O3 -shared -fPIC -fvisibility=hidden \
      -I /build/sqlite -I /build/roaringlite/src \
      /build/roaringlite/src/libsqlite3roaring.c \
      -o /out/lib/roaringlite-linux-x86_64.so

FROM base AS build-linux-aarch64
RUN zig cc -target aarch64-linux $CFLAGS -shared -fvisibility=hidden \
      /build/sqlite/sqlite3.c -o /out/lib/libsqlite3-linux-aarch64.so \
 && zig cc -target aarch64-linux -O3 -shared -fPIC -fvisibility=hidden \
      -I /build/sqlite -I /build/roaringlite/src \
      /build/roaringlite/src/libsqlite3roaring.c \
      -o /out/lib/roaringlite-linux-aarch64.so

FROM base AS build-windows-x86_64
RUN zig cc -target x86_64-windows -O3 -shared -fvisibility=hidden \
      -I /build/sqlite -I /build/roaringlite/src \
      /build/roaringlite/src/libsqlite3roaring.c \
      -o /out/lib/roaringlite-windows-x86_64.dll


FROM scratch AS collect
COPY --from=build-macos-x86_64   /out/lib/ /out/lib/
COPY --from=build-macos-aarch64  /out/lib/ /out/lib/
COPY --from=build-linux-x86_64   /out/lib/ /out/lib/
COPY --from=build-linux-aarch64  /out/lib/ /out/lib/
COPY --from=build-windows-x86_64 /out/lib/ /out/lib/


FROM debian:bookworm-slim AS test
RUN apt-get update && apt-get install -y --no-install-recommends \
    ruby ruby-dev build-essential libsqlite3-dev \
  && rm -rf /var/lib/apt/lists/* \
  && gem install minitest extralite --no-document

COPY --from=base /build/roaringlite/src/ /build/src/
COPY --from=base /build/sqlite/sqlite3.h /build/sqlite/sqlite3ext.h /build/sqlite/
RUN mkdir -p /dist \
  && cc -shared -fPIC -O2 \
       -I /build/sqlite \
       -I /build/src \
       /build/src/libsqlite3roaring.c \
       -o /dist/libroaring.so

COPY --from=collect /out/lib/ /out/lib/

COPY test/ /test/
WORKDIR /test
RUN ruby test_roaring_bitmaps.rb

FROM scratch AS export
COPY --from=test /out/lib/ /
