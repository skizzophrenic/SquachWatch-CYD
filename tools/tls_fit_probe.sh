#!/bin/sh
# Would a TLS handshake fit on this board? Answered off the board.
#
# The open question was whether an HTTPS client is affordable here, and the
# experiment everyone reaches for first is to print
# heap_caps_get_largest_free_block() around a handshake on the hardware. This
# script answers more of it than that would, without flashing anything, by
# reading the framework this project actually links against:
#
#   1. HOW MUCH, AND IN WHAT PIECES. It compiles one file whose arrays are
#      sized by mbedTLS's own macros and structs, then reads the sizes back out
#      of the object with nm. Those are the exact byte counts a session
#      allocates -- not an estimate.
#   2. FROM WHICH HEAP. It disassembles esp_mem.c.obj out of the SHIPPED
#      libmbedcrypto.a and reads the capability constant baked into the call to
#      heap_caps_calloc. That settles the one thing sdkconfig alone cannot:
#      whether CONFIG_SPIRAM_USE_MALLOC=y lets any of it land in PSRAM.
#      0x804 is MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, and it means no.
#
# What it cannot tell you is whether the internal heap happens to have those
# blocks free at the moment you want them, with the frame buffer, both radios
# and the sniffer already in it. That part still needs the board -- but it needs
# it knowing the target is two blocks of 16.3 kB and about 3 kB of small ones.
#
#   sh tools/tls_fit_probe.sh
#
# Measured 2026-09-26 against framework-arduinoespressif32 3.20014.231204:
# in 16717, out 16717, handshake 2280, context 544, config 232, x509_crt 344,
# and 0x804.
set -e

SDK="${SDK:-$HOME/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3}"
TC="${TC:-$HOME/.platformio/packages/toolchain-xtensa-esp32s3/bin}"
CC="$TC/xtensa-esp32s3-elf-gcc"
NM="$TC/xtensa-esp32s3-elf-gcc-nm"
AR="$TC/xtensa-esp32s3-elf-ar"
OD="$TC/xtensa-esp32s3-elf-objdump"
# qio_opi: the memory variant platformio.ini's crowpanel7 env selects.
VARIANT="${VARIANT:-qio_opi}"

for f in "$CC" "$NM" "$AR" "$OD"; do
    [ -x "$f" ] || { echo "not found: $f" >&2; exit 1; }
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== the sdkconfig lines that decide it"
grep -E 'CONFIG_MBEDTLS_(SSL_MAX_CONTENT_LEN|ASYMMETRIC_CONTENT_LEN|INTERNAL_MEM_ALLOC|EXTERNAL_MEM_ALLOC|DYNAMIC_BUFFER|SSL_VARIABLE_BUFFER_LENGTH)|CONFIG_SPIRAM_(USE_MALLOC|MALLOC_ALWAYSINTERNAL)' \
     "$SDK/sdkconfig" || true

echo
echo "== how many bytes, and in what pieces"
cat > "$TMP/probe.c" <<'EOF'
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_internal.h"
/* Each array's size IS the number of bytes one TLS session needs for that
   piece, so nm reads the answer straight out of the object. */
char SZ_in_buffer[MBEDTLS_SSL_IN_BUFFER_LEN];
char SZ_out_buffer[MBEDTLS_SSL_OUT_BUFFER_LEN];
char SZ_handshake[sizeof(mbedtls_ssl_handshake_params)];
char SZ_ssl_context[sizeof(mbedtls_ssl_context)];
char SZ_ssl_config[sizeof(mbedtls_ssl_config)];
char SZ_x509_crt[sizeof(mbedtls_x509_crt)];
EOF
: > "$TMP/inc.rsp"
for d in $(find "$SDK/include" -maxdepth 4 -name include -type d) \
         "$SDK/include/mbedtls/mbedtls/include" "$SDK/$VARIANT/include"; do
    echo "-I$d" >> "$TMP/inc.rsp"
done
"$CC" -c "$TMP/probe.c" -o "$TMP/probe.o" \
      -DMBEDTLS_CONFIG_FILE='"mbedtls/esp_config.h"' "@$TMP/inc.rsp"
# nm prints the size in hex; printf %d with a 0x argument is portable where
# awk's strtonum is not (BSD awk has no strtonum).
"$NM" -S --size-sort "$TMP/probe.o" | while read -r addr size type name; do
    printf '%-18s %8d bytes\n' "$name" "$(printf '%d' "0x$size")"
done

echo
echo "== which heap mbedTLS asks for (the third argument to heap_caps_calloc)"
"$AR" x --output="$TMP" "$SDK/lib/libmbedcrypto.a" esp_mem.c.obj
"$OD" -dr "$TMP/esp_mem.c.obj" | sed -n '/iram1.0.literal/,/^$/p'
cat <<'EOF'
The first literal is the capability mask. 0x00000804 is
MALLOC_CAP_INTERNAL (1<<11) | MALLOC_CAP_8BIT (1<<2), from
esp_heap_caps.h -- so every mbedTLS allocation is internal by
capability and CONFIG_SPIRAM_USE_MALLOC cannot serve any of it.
EOF
