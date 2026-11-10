// SPDX-License-Identifier: GPL-3.0-or-later
// The game's UI font from the romfs, see game_font.h.
#include "game_font.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>

#include "../loader/zbic.h"
#include "nxrt/services.h"

namespace nxrt::gfx {
namespace {

template <typename T> T rd(const uint8_t* p, bool le) {
    T v = 0;
    for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<T>(p[le ? i : sizeof(T) - 1 - i]) << (8 * i);
    return v;
}

bool fail(std::string* why, const std::string& w) {
    if (why) *why = w;
    return false;
}

// The entry whose name ends with `want` ("/SpMain.bfotf") in a SARC archive: its bytes in [*begin, *end).
bool sarc_find(const uint8_t* d, size_t n, const std::string& want, size_t* begin, size_t* end, std::string* why) {
    if (n < 0x14 || std::memcmp(d, "SARC", 4) != 0) return fail(why, "not a SARC archive");
    const bool le = d[6] == 0xFF && d[7] == 0xFE;
    const uint16_t hdr = rd<uint16_t>(d + 4, le);
    const uint32_t data_off = rd<uint32_t>(d + 0x0C, le);
    size_t p = hdr;
    if (p + 12 > n || std::memcmp(d + p, "SFAT", 4) != 0) return fail(why, "SARC without SFAT");
    const uint16_t count = rd<uint16_t>(d + p + 6, le);
    const size_t nodes = p + 12;
    const size_t sfnt = nodes + static_cast<size_t>(count) * 16;
    if (sfnt + 8 > n || std::memcmp(d + sfnt, "SFNT", 4) != 0) return fail(why, "SARC without SFNT");
    const size_t names = sfnt + 8;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = d + nodes + static_cast<size_t>(i) * 16;
        const uint32_t attr = rd<uint32_t>(e + 4, le), s = rd<uint32_t>(e + 8, le), t = rd<uint32_t>(e + 12, le);
        const size_t name_at = names + static_cast<size_t>(attr & 0xFFFF) * 4;
        if (name_at >= n) continue;
        const char* nm = reinterpret_cast<const char*>(d + name_at);
        const size_t len = strnlen(nm, n - name_at);
        const std::string name(nm, len);
        if (name.size() >= want.size() && name.compare(name.size() - want.size(), want.size(), want) == 0) {
            if (static_cast<size_t>(data_off) + t > n || s > t) return fail(why, "SARC entry out of range");
            *begin = data_off + s;
            *end = data_off + t;
            return true;
        }
    }
    return fail(why, want + " not in the archive");
}

// A zstd-compressed romfs file, decompressed (null and *why when it is not there or does not decode).
std::unique_ptr<uint8_t[]> read_romfs_zs(const std::string& rel, size_t* n, std::string* why) {
    const std::string path = nxrt::services::romfs_dir() + "/" + rel;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { fail(why, "no " + path); return nullptr; }
    const std::streamoff sz = f.tellg();
    if (sz <= 0 || sz > (64 << 20)) { fail(why, "unexpected size of " + path); return nullptr; }
    std::vector<uint8_t> zs(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(zs.data()), sz);
    std::unique_ptr<uint8_t[]> out(zstd_decompress_alloc(zs.data(), zs.size(), 128u << 20, n));
    if (!out) fail(why, rel + " does not decompress");
    return out;
}

// Byte offset of block (x, y) of a block-linear surface (Tegra / NVN layout: 64-byte x 8-row GOBs, block_h GOBs per block column).
size_t block_linear_offset(uint32_t x, uint32_t y, uint32_t width_blocks, uint32_t bpp, uint32_t block_h) {
    const size_t gobs_wide = (static_cast<size_t>(width_blocks) * bpp + 63) / 64;
    const size_t xb = static_cast<size_t>(x) * bpp;
    const size_t gob = (y / (8 * block_h)) * 512 * block_h * gobs_wide + (xb / 64) * 512 * block_h + (y % (8 * block_h) / 8) * 512;
    return gob + ((xb % 64) / 32) * 256 + ((y % 8) / 2) * 64 + ((xb % 32) / 16) * 32 + (y % 2) * 16 + (xb % 16);
}

void bc4_block(const uint8_t* b, uint8_t out[16]) {
    const int r0 = b[0], r1 = b[1];
    int pal[8] = {r0, r1};
    if (r0 > r1) for (int i = 0; i < 6; ++i) pal[2 + i] = ((6 - i) * r0 + (i + 1) * r1) / 7;
    else {
        for (int i = 0; i < 4; ++i) pal[2 + i] = ((4 - i) * r0 + (i + 1) * r1) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = static_cast<uint8_t>(pal[(bits >> (3 * i)) & 7]);
}

template <typename T> T rd_le(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

}  // namespace

bool load_layout_masks(const std::string& archive, const std::vector<std::string>& names, std::vector<std::pair<std::string, GameMask>>* out,
                       std::string* why) {
    size_t n = 0;
    std::unique_ptr<uint8_t[]> sarc = read_romfs_zs("Layout/" + archive + ".Nin_Ounce_NVN2.blarc.zs", &n, why);
    if (!sarc) return false;
    size_t b = 0, e = 0;
    if (!sarc_find(sarc.get(), n, "timg/__Combined.bntx", &b, &e, why)) return false;
    const uint8_t* d = sarc.get() + b;
    const size_t dn = e - b;
    // BNTX: "BNTX", then at 0x20 the platform block ("NX  " on Switch 1, "Ounc" on Switch 2): texture count +4, the BRTI pointer array +8
    if (dn < 0x40 || std::memcmp(d, "BNTX", 4) != 0) return fail(why, archive + ": not a BNTX");
    const uint32_t count = rd_le<uint32_t>(d + 0x24);
    const uint64_t infos = rd_le<uint64_t>(d + 0x28);
    for (uint32_t i = 0; i < count && infos + 8ull * i + 8 <= dn; ++i) {
        const uint64_t p = rd_le<uint64_t>(d + infos + 8ull * i);
        if (p + 0x78 > dn || std::memcmp(d + p, "BRTI", 4) != 0) continue;
        const uint32_t fmt = rd_le<uint32_t>(d + p + 0x1C), w = rd_le<uint32_t>(d + p + 0x24), h = rd_le<uint32_t>(d + p + 0x28);
        const uint32_t bh_log2 = rd_le<uint32_t>(d + p + 0x34);
        const uint64_t name_off = rd_le<uint64_t>(d + p + 0x60), mips = rd_le<uint64_t>(d + p + 0x70);
        if (name_off + 2 > dn || mips + 8 > dn) continue;
        std::string name(reinterpret_cast<const char*>(d + name_off + 2), rd_le<uint16_t>(d + name_off));
        if (const size_t s = name.find('^'); s != std::string::npos) name.resize(s);
        bool wanted = false;
        for (const std::string& want : names) wanted |= want == name;
        if (!wanted || (fmt >> 8) != 0x1D || w == 0 || h == 0 || w > 4096 || h > 4096) continue;   // 0x1D: BC4
        const uint64_t base = rd_le<uint64_t>(d + mips);
        const uint32_t wb = (w + 3) / 4, hb = (h + 3) / 4, bh = 1u << std::min<uint32_t>(bh_log2, 5);
        GameMask m;
        m.w = static_cast<int>(w);
        m.h = static_cast<int>(h);
        m.px.assign(static_cast<size_t>(w) * h, 0);
        uint8_t blk[16];
        for (uint32_t by = 0; by < hb; ++by)
            for (uint32_t bx = 0; bx < wb; ++bx) {
                const size_t a = base + block_linear_offset(bx, by, wb, 8, bh);
                if (a + 8 > dn) continue;
                bc4_block(d + a, blk);
                for (int j = 0; j < 16; ++j) {
                    const uint32_t x = bx * 4 + j % 4, y = by * 4 + j / 4;
                    if (x < w && y < h) m.px[static_cast<size_t>(y) * w + x] = blk[j];
                }
            }
        out->emplace_back(name, std::move(m));
    }
    return true;
}

std::vector<uint8_t> load_game_font(const std::string& name, std::string* why) {
    size_t n = 0;
    std::unique_ptr<uint8_t[]> sarc = read_romfs_zs("Font/Font.Nin_Ounce_NVN2.bfarc.zs", &n, why);
    if (!sarc) return {};
    size_t b = 0, e = 0;
    if (!sarc_find(sarc.get(), n, "/" + name + ".bfotf", &b, &e, why) && !sarc_find(sarc.get(), n, "/" + name + ".bfttf", &b, &e, why)) return {};
    if (e - b < 16) { fail(why, "font entry too small"); return {}; }
    const uint8_t* src = sarc.get() + b;
    static const uint8_t kMagic[4] = {0x7F, 0x9A, 0x02, 0x18};   // the header's first word, decoded
    uint8_t key[4];
    for (int i = 0; i < 4; ++i) key[i] = src[i] ^ kMagic[i];
    std::vector<uint8_t> out(e - b - 8);
    for (size_t i = 8; i < e - b; ++i) out[i - 8] = src[i] ^ key[i % 4];
    const bool otf = std::memcmp(out.data(), "OTTO", 4) == 0, ttf = out[0] == 0 && out[1] == 1 && out[2] == 0 && out[3] == 0;
    if (!otf && !ttf) { fail(why, name + ": not an OpenType font after decoding"); return {}; }
    return out;
}

}  // namespace nxrt::gfx
