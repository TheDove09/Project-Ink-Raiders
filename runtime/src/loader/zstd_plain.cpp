// SPDX-License-Identifier: GPL-3.0-or-later
// Plain zstd decompression (zbic.h: zstd_decompress_alloc) for the game's *.zs data files, read by the runtime itself (the settings
// menu's font). The same decoder source as zbic.cpp (third_party/zstd_zbic, BSD-3-Clause), built here without ZSTD_ZBIC_SUPPORT, every
// zstd symbol internal (static) so the two copies do not clash.
#include "zbic.h"

#include <cstring>
#include <new>
// The decoder's own includes, here first so their include guards keep them out of the namespace below (its FSE_/HUF_ helpers are not
// covered by the ZSTDLIB_* visibility macros: the namespace keeps them apart from zbic.cpp's copy).
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER) || defined(_WIN32)
#include <intrin.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#include <emmintrin.h>
#include <mmintrin.h>
#endif

#define ZSTD_ZBIC_SUPPORT 0
#define ZSTDLIB_VISIBLE static
#define ZSTDLIB_HIDDEN static
#define ZSTDERRORLIB_VISIBLE static
#define ZSTDERRORLIB_HIDDEN static
#define ZSTD_STATIC_LINKING_ONLY
#undef ZSTD_MULTITHREAD
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
namespace zstd_plain {
#include "../../third_party/zstd_zbic/zstddeclib.c"
}  // namespace zstd_plain
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace nxrt {

uint8_t* zstd_decompress_alloc(const void* src, size_t src_size, size_t max_size, size_t* out_size) {
    if (out_size) *out_size = 0;
    if (!src || src_size < 4) return nullptr;
    const unsigned long long n = ::zstd_plain::ZSTD_getFrameContentSize(src, src_size);
    if (n == ZSTD_CONTENTSIZE_UNKNOWN || n == ZSTD_CONTENTSIZE_ERROR || n == 0 || n > max_size) return nullptr;
    uint8_t* dst = new (::std::nothrow) uint8_t[static_cast<size_t>(n)];
    if (!dst) return nullptr;
    const size_t r = ::zstd_plain::ZSTD_decompress(dst, static_cast<size_t>(n), src, src_size);
    if (::zstd_plain::ZSTD_isError(r) || r != n) {
        delete[] dst;
        return nullptr;
    }
    if (out_size) *out_size = static_cast<size_t>(n);
    return dst;
}

}  // namespace nxrt
