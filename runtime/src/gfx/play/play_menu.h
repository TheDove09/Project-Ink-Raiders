// SPDX-License-Identifier: GPL-3.0-or-later
// The start menu shown before the game's code is loaded: "Bring your own game" (play_menu.cpp). [launcher] start_menu = false skips it;
// it is also skipped when a [play] selftest script runs.
#pragma once

namespace nxrt {
class FrameLimiter;
namespace gfx {
class Renderer;
class Overlay;
}  // namespace gfx

namespace play {
// Runs the menu until the player picks Play (true) or Quit / closes the window (false). Returns true at once when there is no window.
bool start_menu(gfx::Renderer* renderer, gfx::Overlay* ov, FrameLimiter* limiter);
}  // namespace play
}  // namespace nxrt
