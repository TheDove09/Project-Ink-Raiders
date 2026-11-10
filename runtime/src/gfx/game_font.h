// SPDX-License-Identifier: GPL-3.0-or-later
// The game's own UI font for the runtime's settings menu, read from the player's romfs at run time (nothing of it is shipped):
// romfs/Font/Font.Nin_Ounce_NVN2.bfarc.zs is a zstd-compressed SARC; its scft/<name>.bfotf entries are OpenType fonts behind an
// 8-byte header (a magic and the size) and a 4-byte XOR key, which the magic gives back (decoded magic 7f 9a 02 18).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nxrt::gfx {

// The decoded OpenType bytes of the font `name` (e.g. "SpMain"), or empty when the romfs, the archive or the font is not there
// (why in *why). Reads and decodes on every call: keep the result.
std::vector<uint8_t> load_game_font(const std::string& name, std::string* why = nullptr);

// A one-channel texture of the game's UI (BC4 in the layout archives: shapes, sticker patterns, paint grain), decoded to 8-bit values.
struct GameMask {
    int w = 0, h = 0;
    std::vector<uint8_t> px;              // w * h, row by row
};
// The textures `names` (without the "^s" suffix) of romfs/Layout/<archive>.Nin_Ounce_NVN2.blarc.zs (a zstd SARC whose
// timg/__Combined.bntx holds them, block-linear). Only BC4 textures are decoded; the others are skipped. False when the archive is not
// there (why in *why); the found ones are in *out.
bool load_layout_masks(const std::string& archive, const std::vector<std::string>& names, std::vector<std::pair<std::string, GameMask>>* out,
                       std::string* why = nullptr);

}  // namespace nxrt::gfx
