// SPDX-License-Identifier: GPL-3.0-or-later
// Splatoon Raiders: the whole game module as one nxrt::game::Info. Play-mode hooks come here as they are found.
#include "sra_game.h"

#include "play_game.h"
#include "play_types.h"

namespace nxrt::games::sra {

namespace {

// The frame boundary (recomp/games/sra/main.sites `entryhook 0x71031b8c60`): sead's run loop f_71031b8c20 calls this method of the
// framework through vtable +0xE0 once per main-loop iteration, on the main thread. Play mode pumps the window events and samples the
// keyboard, mouse and pads here; without it no live input reaches the game.
constexpr uint64_t kFrameUpdate = 0x31b8c60;

// Factory keyboard and mouse bindings (play_types.h settings file). There is no in-game input hook yet, so play mode treats every frame
// as a menu frame: everything sits in [bindings.all], the arrows drive the D-pad (menus) and WASD the left stick. Button roles follow the
// Splatoon series' console layout (ZR shoot, ZL swim, R sub weapon, B jump, right stick click special, X map, Y camera, + menu); they are
// NOT verified against Raiders yet, and every key can be rebound in controls.ini or the launcher. Mouse aiming needs a camera hook (later).
// Pads keep the console layout ([padmap] default).
constexpr const char* kSettingsText = R"INI(
[meta]
defaults_rev = 1

[bindings.all]
W = move_forward
S = move_back
A = move_left
D = move_right
Up = dpad_up
Down = dpad_down
Left = dpad_left
Right = dpad_right
I = look_up
K = look_down
J = look_left
L = look_right
E = btn_a
Return = btn_a
Keypad Enter = btn_a
Backspace = btn_b
Space = btn_b
Mouse1 = btn_zr
Left Shift = btn_zl
Mouse2 = btn_r
Q = btn_rstick
Tab = btn_x
F = btn_y
R = btn_l
Escape = btn_plus
C = btn_minus
1 = dpad_up
2 = dpad_down
3 = dpad_left
4 = dpad_right
)INI";

// Revision 1: the first factory bindings. A controls.ini written before them (revision 0, empty [bindings.*] sections) gets them once;
// an input the player already bound keeps the player's choice.
constexpr play::GameDefaultAddition kAdditions[] = {
    {1, play::Ctx::All, "W", play::Act::MoveForward},       {1, play::Ctx::All, "S", play::Act::MoveBack},
    {1, play::Ctx::All, "A", play::Act::MoveLeft},          {1, play::Ctx::All, "D", play::Act::MoveRight},
    {1, play::Ctx::All, "Up", play::Act::DpadUp},           {1, play::Ctx::All, "Down", play::Act::DpadDown},
    {1, play::Ctx::All, "Left", play::Act::DpadLeft},       {1, play::Ctx::All, "Right", play::Act::DpadRight},
    {1, play::Ctx::All, "I", play::Act::LookUp},            {1, play::Ctx::All, "K", play::Act::LookDown},
    {1, play::Ctx::All, "J", play::Act::LookLeft},          {1, play::Ctx::All, "L", play::Act::LookRight},
    {1, play::Ctx::All, "E", play::Act::BtnA},              {1, play::Ctx::All, "Return", play::Act::BtnA},
    {1, play::Ctx::All, "Keypad Enter", play::Act::BtnA},   {1, play::Ctx::All, "Backspace", play::Act::BtnB},
    {1, play::Ctx::All, "Space", play::Act::BtnB},          {1, play::Ctx::All, "Mouse1", play::Act::BtnZR},
    {1, play::Ctx::All, "Left Shift", play::Act::BtnZL},    {1, play::Ctx::All, "Mouse2", play::Act::BtnR},
    {1, play::Ctx::All, "Q", play::Act::BtnRStick},         {1, play::Ctx::All, "Tab", play::Act::BtnX},
    {1, play::Ctx::All, "F", play::Act::BtnY},              {1, play::Ctx::All, "R", play::Act::BtnL},
    {1, play::Ctx::All, "Escape", play::Act::BtnPlus},      {1, play::Ctx::All, "C", play::Act::BtnMinus},
    {1, play::Ctx::All, "1", play::Act::DpadUp},            {1, play::Ctx::All, "2", play::Act::DpadDown},
    {1, play::Ctx::All, "3", play::Act::DpadLeft},          {1, play::Ctx::All, "4", play::Act::DpadRight},
};

const play::GameDefaults& game_defaults() {
    static const play::GameDefaults d = [] {
        play::GameDefaults g;
        g.settings_text = kSettingsText;
        g.defaults_rev = 1;
        g.additions = kAdditions;
        g.addition_count = sizeof kAdditions / sizeof kAdditions[0];
        return g;
    }();
    return d;
}

const play::GamePlay& game_play() {
    static const play::GamePlay t = [] {
        play::GamePlay p;
        p.frame_update = kFrameUpdate;
        return p;
    }();
    return t;
}

}  // namespace

const game::Info& full_info() {
    static const game::Info info = [] {
        game::Info g = core_info();
        g.play = &game_play();
        g.play_defaults = &game_defaults();
        return g;
    }();
    return info;
}

}  // namespace nxrt::games::sra

void nxrt_register_game_sra() { nxrt::game::set_current(&nxrt::games::sra::full_info()); }
