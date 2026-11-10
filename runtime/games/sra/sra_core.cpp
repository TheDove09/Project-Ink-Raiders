// SPDX-License-Identifier: GPL-3.0-or-later
#include "sra_game.h"

#include "nxrt/services.h"

namespace {

// Raiders is built with SDK 21.4.4 and advertises a 32 MiB system resource
// pool. The framework's neutral main-thread defaults are retained until the
// NUP's NPDM metadata is parsed and verified locally.
void title_defaults(nxrt::services::TitleProfile& p) {
    p.program_id = nxrt::games::sra::data_info().program_id;
    p.name = nxrt::games::sra::data_info().title;
    p.display_version = nxrt::games::sra::data_info().version;
    p.supported_languages = nxrt::games::sra::data_info().languages;
    p.system_resource_size = 0x02000000;
    // Account save data. Raiders calls nn::fs::MountSaveData(name, uid) at startup without EnsureSaveData and aborts when it is
    // missing; the runtime creates it on first open from these sizes (fs.cpp open_save). The real control.nacp values are NOT
    // known: the NUP carries only ExeFS and RomFS (no control section), and no public profile lists them. 64 MiB / 64 MiB is the
    // value the framework's other Switch 2 title uses: an unverified placeholder that only caps the save volume's quota.
    // [title] account_save_size / account_save_journal in the ini override it.
    p.account_save_size = 0x4000000;
    p.account_save_journal = 0x4000000;
    p.firmware = "21.4.4";
    p.user_nickname = "Player";
}

}  // namespace

namespace nxrt::games::sra {

const game::Info& core_info() {
    static const game::Info info = [] {
        game::Info g = data_info();
        g.title_defaults = &title_defaults;
        // sead::GameFrameworkOunce's NVN device setup (main 1.1.0, f_71031b83f0) queries nvnDeviceGetInteger(API_MAJOR/MINOR)
        // and returns without creating its device singleton unless major == 231 (cmp at 0x71031b86a4) and minor >= 39
        // (cmp/b.lt at 0x71031b86b0). With the framework's SDK 20.5 minor (30) the singleton stays null and the first
        // reader, the RomConfig parser f_71031bb800, faults on it.
        g.nvn_api_minor = 39;
        // Raiders' menus draw fewer than 16 things per frame, so the loading heuristic left them uncapped (300-460 fps): the game steps
        // its animation once per frame, so they ran 5-7x too fast. Every frame is paced to the display instead.
        g.unpaced_loading = false;
        // A keyboard key bound to a pad button (Enter = A) must not also reach the game's USB-keyboard device: with both, the difficulty
        // menu ignored the A press while the D-pad still worked (a real pad's A always worked).
        g.hid_keyboard = false;
        return g;
    }();
    return info;
}

}  // namespace nxrt::games::sra

void nxrt_register_game_sra_core() { nxrt::game::set_current(&nxrt::games::sra::core_info()); }
