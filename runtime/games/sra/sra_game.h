// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "nxrt/game.h"

void nxrt_register_game_sra();
void nxrt_register_game_sra_core();
void nxrt_register_game_sra_data();

namespace nxrt::games::sra {
const game::Info& data_info();
const game::Info& core_info();
const game::Info& full_info();
}  // namespace nxrt::games::sra
