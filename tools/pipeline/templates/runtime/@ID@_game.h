// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "nxrt/game.h"

void nxrt_register_game_@ID@();
void nxrt_register_game_@ID@_core();
void nxrt_register_game_@ID@_data();

namespace nxrt::games::@ID@ {
const game::Info& data_info();
const game::Info& core_info();
const game::Info& full_info();
}  // namespace nxrt::games::@ID@
