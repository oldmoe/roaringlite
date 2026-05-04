FROM debian:bookworm-slim AS builder

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

# Build CRoaring amalgamation from the submodule
COPY CRoaring /build/CRoaring
RUN mkdir -p /build/roaringlite/src \
  && cd /build/CRoaring && ./amalgamation.sh /build/roaringlite/src

# Copy roaringlite source alongside the freshly built amalgamation
COPY src/libsqlite3roaring.c /build/roaringlite/src/libsqlite3roaring.c

WORKDIR /build/sqlite

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

RUN mkdir -p /out/lib

RUN zig cc -target x86_64-macos $CFLAGS -shared -fvisibility=hidden \
    sqlite3.c -o /out/lib/libsqlite3-x86_64.dylib

RUN zig cc -target aarch64-macos $CFLAGS -shared -fvisibility=hidden \
    sqlite3.c -o /out/lib/libsqlite3-aarch64.dylib

RUN zig cc -target aarch64-linux $CFLAGS -shared -fvisibility=hidden \
    sqlite3.c -o /out/lib/libsqlite3-aarch64.so

RUN zig cc -target x86_64-linux $CFLAGS -shared -fvisibility=hidden \
    sqlite3.c -o /out/lib/libsqlite3-x86_64.so

WORKDIR /build/roaringlite/src

RUN zig cc -target x86_64-macos -O3 -shared -fPIC -fvisibility=hidden \
    -I /build/sqlite \
    -I /build/roaringlite/src \
    libsqlite3roaring.c \
    -o /out/lib/roaringlite-x86_64.dylib

RUN zig cc -target aarch64-macos -O3 -shared -fPIC -fvisibility=hidden \
    -I /build/sqlite \
    -I /build/roaringlite/src \
    libsqlite3roaring.c \
    -o /out/lib/roaringlite-aarch64.dylib

RUN zig cc -target aarch64-linux -O3 -shared -fPIC -fvisibility=hidden \
    -I /build/sqlite \
    -I /build/roaringlite/src \
    libsqlite3roaring.c \
    -o /out/lib/roaringlite-aarch64.so

RUN zig cc -target x86_64-linux -O3 -shared -fPIC -fvisibility=hidden \
    -I /build/sqlite \
    -I /build/roaringlite/src \
    libsqlite3roaring.c \
    -o /out/lib/roaringlite-x86_64.so

FROM debian:bookworm-slim AS test
RUN apt-get update && apt-get install -y --no-install-recommends \
    ruby ruby-dev build-essential libsqlite3-dev \
  && rm -rf /var/lib/apt/lists/* \
  && gem install minitest extralite --no-document

# Build a native libroaring.so against the system sqlite3 headers
COPY --from=builder /build/roaringlite/src/ /build/src/
COPY --from=builder /build/sqlite/sqlite3.h /build/sqlite/sqlite3ext.h /build/sqlite/
RUN mkdir -p /dist \
  && cc -shared -fPIC -O2 \
       -I /build/sqlite \
       -I /build/src \
       /build/src/libsqlite3roaring.c \
       -o /dist/libroaring.so

# Copy all built artifacts so export can reference this stage
COPY --from=builder /out/lib/ /out/lib/

COPY test/ /test/
WORKDIR /test
RUN ruby test_roaring_bitmaps.rb

FROM scratch AS export
COPY --from=test /out/lib/ /
