#!/usr/bin/env bash
# Build hactool 1.4.0 (static against mbedtls 2.28.8) inside localhost/engage-linux-build.
# Result: /w/deps/hactool/hactool
set -euo pipefail

cd /w/deps/hactool

if [ ! -x hactool ]; then
  # sources
  [ -d mbedtls ] || { curl -sL -o mbedtls.tar.gz https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.8.tar.gz; tar xf mbedtls.tar.gz; mv mbedtls-2.28.8 mbedtls; }
  [ -d hactool-src ] || { curl -sL -o hactool.tar.gz https://github.com/SciresM/hactool/archive/refs/tags/1.4.0.tar.gz; tar xf hactool.tar.gz; mv hactool-1.4.0 hactool-src; }

  # mbedtls 2.28.8 static libs. MBEDTLS_CMAC_C is off by default in 2.28 and
  # hactool needs it; enable it in config.h so the library and hactool see the
  # *same* struct layouts (otherwise mbedtls_cipher_context_t offsets mismatch).
  sed -i 's|^//#define MBEDTLS_CMAC_C|#define MBEDTLS_CMAC_C|' mbedtls/include/mbedtls/config.h
  make -C mbedtls clean >/dev/null 2>&1 || true
  make -C mbedtls -j"$(nproc)" lib CFLAGS="-O2 -fPIC -Iinclude -D_FILE_OFFSET_BITS=64" >/dev/null

  # hactool 1.4.0
  cd hactool-src
  INC=/w/deps/hactool/mbedtls/include
  LIB=/w/deps/hactool/mbedtls/library
  SRCS="save.c sha.c aes.c extkeys.c rsa.c npdm.c bktr.c kip.c packages.c pki.c pfs0.c hfs0.c nca0_romfs.c romfs.c utils.c nax0.c nso.c lz4.c nca.c xci.c main.c filepath.c ConvertUTF.c cJSON.c"
  gcc -O2 -I "$INC" -D_BSD_SOURCE -D_POSIX_SOURCE -D_POSIX_C_SOURCE=200112L \
      -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 \
      -o /w/deps/hactool/hactool $SRCS \
      -L "$LIB" -lmbedtls -lmbedx509 -lmbedcrypto -lm -lpthread
fi

/w/deps/hactool/hactool --help >/dev/null 2>&1 || true
echo "hactool built: $(stat -c %s /w/deps/hactool/hactool) bytes"
