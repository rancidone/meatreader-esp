/* clangd_stubs/machine/endian.h
 *
 * Minimal stand-in for the Xtensa toolchain sysroot header of the same name.
 * The real file lives in the ESP-IDF Xtensa toolchain and is only accessible
 * during an actual cross-compilation build.
 *
 * This stub lets clangd (running host clang) parse ESP-IDF headers without
 * the target sysroot present. Definitions match the ESP32's fixed byte order
 * (always little-endian) and the newlib conventions.
 *
 * Do NOT include other sysroot headers here — that would recreate the
 * dependency on the missing sysroot and defeat the purpose of the stub.
 */

#ifndef __MACHINE_ENDIAN_H__
#define __MACHINE_ENDIAN_H__

#define _LITTLE_ENDIAN  1234
#define _BIG_ENDIAN     4321
#define _PDP_ENDIAN     3412
#define _BYTE_ORDER     _LITTLE_ENDIAN

/* Quad-word word order for a little-endian machine. */
#define _QUAD_HIGHWORD  1
#define _QUAD_LOWWORD   0

/* BSD-visible names (always expose them for clangd). */
#define LITTLE_ENDIAN   _LITTLE_ENDIAN
#define BIG_ENDIAN      _BIG_ENDIAN
#define PDP_ENDIAN      _PDP_ENDIAN
#define BYTE_ORDER      _BYTE_ORDER

/* Byte-swap: clang supports __builtin_bswap* unconditionally. */
#define __bswap16(_x)   __builtin_bswap16((_x))
#define __bswap32(_x)   __builtin_bswap32((_x))
#define __bswap64(_x)   __builtin_bswap64((_x))

/* Network/host byte-order conversion (ESP32 = little-endian). */
#define __htonl(_x)     __bswap32(_x)
#define __htons(_x)     __bswap16(_x)
#define __ntohl(_x)     __bswap32(_x)
#define __ntohs(_x)     __bswap16(_x)

#endif /* __MACHINE_ENDIAN_H__ */
