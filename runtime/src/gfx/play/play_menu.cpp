// SPDX-License-Identifier: GPL-3.0-or-later
// The start menu ("Bring your own game"), shown before the game's code is loaded (play_menu.h). It asks for nothing the player does
// not already have and ships nothing of the game: with the player's game files it is drawn from them (the game's banner and text
// fonts, and the shape, grain and sticker masks of the difficulty buttons, romfs Layout/BtnFirstDifficulty_00, coloured and layered
// here); without them it is drawn plain (own colours, the default font) and asks for the files.
#include "play_menu.h"

#include <windows.h>
#include <commdlg.h>

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>

#include "../game_font.h"
#include "nxrt/config.h"
#include "nxrt/diag.h"
#include "nxrt/frame_limiter.h"
#include "nxrt/gfx.h"
#include "nxrt/input.h"
#include "nxrt/log.h"
#include "nxrt/overlay.h"
#include "nxrt/platform.h"
#include "nxrt/services.h"

namespace nxrt::play {
namespace {

constexpr const char* kTag = "menu";

struct Rgb { float r, g, b; };
constexpr Rgb kGreen = {0.30f, 0.68f, 0.24f}, kTeal = {0.19f, 0.62f, 0.60f}, kRed = {0.72f, 0.20f, 0.23f};
constexpr ImU32 kCream = IM_COL32(244, 239, 224, 255), kShadow = IM_COL32(12, 30, 24, 150), kOrange = IM_COL32(245, 162, 59, 255);

const gfx::GameMask* find_mask(const std::vector<std::pair<std::string, gfx::GameMask>>& v, const char* name) {
    for (const auto& kv : v)
        if (kv.first == name) return &kv.second;
    return nullptr;
}

uint8_t sample(const gfx::GameMask* m, int x, int y) {   // repeating
    if (!m || m->w <= 0 || m->h <= 0) return 128;
    x %= m->w;
    y %= m->h;
    if (x < 0) x += m->w;
    if (y < 0) y += m->h;
    return m->px[static_cast<size_t>(y) * m->w + x];
}

// A painted banner as the game's buttons are made: the shape mask for coverage, the paint grain darkening the colour a little, two
// sticker shapes in a lighter tint of the same colour.
std::vector<uint8_t> compose_banner(const gfx::GameMask& shape, const gfx::GameMask* grain, const gfx::GameMask* sticker_a,
                                    const gfx::GameMask* sticker_b, Rgb c, int seed) {
    const int w = shape.w, h = shape.h;
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // the shape's edge is dithered (the game smooths it with its own filtering): averaged over a 5x5 neighbourhood, then a
            // soft threshold, for a clean painted edge
            float acc = 0.0f;
            for (int dy = -4; dy <= 4; ++dy)
                for (int dx = -4; dx <= 4; ++dx) {
                    const int xx = std::clamp(x + dx, 0, w - 1), yy = std::clamp(y + dy, 0, h - 1);
                    acc += shape.px[static_cast<size_t>(yy) * w + xx];
                }
            const float avg = acc / (81.0f * 255.0f);
            const float e = std::clamp((avg - 0.62f) / 0.18f, 0.0f, 1.0f);   // the dotted band (about half covered) falls outside
            const float a = e * e * (3.0f - 2.0f * e);
            const float g = sample(grain, x + seed * 97, y + seed * 61) / 255.0f;
            float k = 0.80f + 0.25f * g;
            // stickers: one large on the right, one on the left, scaled to the banner's height
            auto st = [&](const gfx::GameMask* m, float cx, float cy, float size) -> float {
                if (!m) return 0.0f;
                const float u = (x - cx) / size + 0.5f, v = (y - cy) / size + 0.5f;
                if (u < 0 || v < 0 || u >= 1 || v >= 1) return 0.0f;
                return m->px[static_cast<size_t>(v * m->h) * m->w + static_cast<size_t>(u * m->w)] / 255.0f;
            };
            const float s = std::max(st(sticker_a, w * 0.83f, h * 0.55f, h * 0.95f), st(sticker_b, w * 0.12f, h * 0.35f, h * 0.70f));
            k += 0.16f * s;
            const size_t o = (static_cast<size_t>(y) * w + x) * 4;
            out[o + 0] = static_cast<uint8_t>(std::clamp(c.r * k, 0.0f, 1.0f) * 255.0f);
            out[o + 1] = static_cast<uint8_t>(std::clamp(c.g * k, 0.0f, 1.0f) * 255.0f);
            out[o + 2] = static_cast<uint8_t>(std::clamp(c.b * k, 0.0f, 1.0f) * 255.0f);
            out[o + 3] = static_cast<uint8_t>(a * 255.0f);
        }
    return out;
}

// The backdrop: the game's deep ink green with its paint grain, tiled
std::vector<uint8_t> compose_backdrop(const gfx::GameMask& grain) {
    std::vector<uint8_t> out(static_cast<size_t>(grain.w) * grain.h * 4);
    for (size_t i = 0; i < grain.px.size(); ++i) {
        const float k = 0.95f + 0.09f * (grain.px[i] / 255.0f);
        out[i * 4 + 0] = static_cast<uint8_t>(std::clamp(15.0f * k, 0.0f, 255.0f));
        out[i * 4 + 1] = static_cast<uint8_t>(std::clamp(43.0f * k, 0.0f, 255.0f));
        out[i * 4 + 2] = static_cast<uint8_t>(std::clamp(37.0f * k, 0.0f, 255.0f));
        out[i * 4 + 3] = 255;
    }
    return out;
}

bool file_exists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// The game files the port runs from: the ExeFS main module and a non-empty RomFS. Any game (the menu's styled look additionally
// needs Raiders' font and layout archives, and falls back to the plain look without them).
bool game_files_present() {
    const std::string exefs = config_get_str("fs", "exefs", "");
    const std::string romfs = services::romfs_dir();
    std::error_code ec;
    return file_exists(exefs + "/main") && !romfs.empty() && std::filesystem::is_directory(romfs, ec) &&
           std::filesystem::directory_iterator(romfs, ec) != std::filesystem::directory_iterator();
}

std::string pick_file(const wchar_t* title, const wchar_t* filter) {
    wchar_t path[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn = {sizeof ofn};
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(std::size(path));
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, n - 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), n, nullptr, nullptr);
    return out;
}

struct Menu {
    gfx::Overlay* ov = nullptr;
    bool styled = false;                 // the game's fonts and masks are there
    bool have_files = false;
    uint64_t tex_bg = 0, tex_banner[3] = {};
    ImFont* f_title = nullptr;
    ImFont* f_body = nullptr;
    int sel = 1;                         // 0 how to dump, 1 play / select, 2 quit
    bool guide = false;                  // the "How to dump" page is up
    std::string status;                  // a line under the description (errors, progress)
    ImVec2 hit_min[3], hit_max[3];
    float t = 0.0f;

    const char* label(int i) const {
        if (i == 0) return "How to dump";
        if (i == 1) return have_files ? "Play" : "Select game file";
        return "Quit";
    }

    void text_centered(ImDrawList* dl, ImFont* f, float size, ImVec2 c, ImU32 col, const char* s, bool shadow) {
        const ImVec2 ts = (f ? f : ImGui::GetFont())->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        const ImVec2 p(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
        if (shadow) dl->AddText(f, size, ImVec2(p.x + size * 0.05f, p.y + size * 0.07f), kShadow, s);
        dl->AddText(f, size, p, col, s);
    }

    void draw() {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const ImVec2 D = ImGui::GetIO().DisplaySize;
        const float k = D.y / 1080.0f;
        t += ImGui::GetIO().DeltaTime;
        if (tex_bg) dl->AddImage(static_cast<ImTextureID>(tex_bg), ImVec2(0, 0), D, ImVec2(0, 0), ImVec2(D.x / (512.0f * k), D.y / (512.0f * k)));
        else dl->AddRectFilled(ImVec2(0, 0), D, IM_COL32(15, 43, 37, 255));

        text_centered(dl, f_title, 96.0f * k, ImVec2(D.x * 0.5f, D.y * 0.20f), kCream, "Bring your own game", true);

        const float widths[3] = {470.0f, 620.0f, 470.0f}, heights[3] = {150.0f, 196.0f, 150.0f}, rot[3] = {-0.026f, 0.017f, -0.017f};
        const Rgb colours[3] = {kGreen, kTeal, kRed};
        float x = D.x * 0.5f - (widths[0] + widths[1] + widths[2] + 2 * 40.0f) * k * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const bool on = i == sel && !guide;
            const float grow = on ? 1.06f + 0.012f * std::sin(t * 4.0f) : 1.0f;
            const float w = widths[i] * k * grow, h = heights[i] * k * grow;
            const ImVec2 c(x + widths[i] * k * 0.5f, D.y * 0.45f);
            x += (widths[i] + 40.0f) * k;
            const float cs = std::cos(rot[i]), sn = std::sin(rot[i]);
            auto corner = [&](float dx, float dy) { return ImVec2(c.x + dx * cs - dy * sn, c.y + dx * sn + dy * cs); };
            const ImVec2 p0 = corner(-w / 2, -h / 2), p1 = corner(w / 2, -h / 2), p2 = corner(w / 2, h / 2), p3 = corner(-w / 2, h / 2);
            if (tex_banner[i]) {
                dl->AddImageQuad(static_cast<ImTextureID>(tex_banner[i]), p0, p1, p2, p3);
            } else {
                const ImU32 col = IM_COL32(static_cast<int>(colours[i].r * 255), static_cast<int>(colours[i].g * 255), static_cast<int>(colours[i].b * 255), 255);
                dl->AddQuadFilled(p0, p1, p2, p3, col);
            }
            if (on) {   // the cursor: a squid-green blob with an eye at the banner's lower left corner
                const ImVec2 q(p3.x + 6.0f * k, p3.y - 4.0f * k);
                dl->AddCircleFilled(q, 18.0f * k, IM_COL32(95, 211, 180, 255));
                dl->AddCircleFilled(ImVec2(q.x + 4.0f * k, q.y - 3.0f * k), 6.0f * k, IM_COL32(244, 239, 224, 255));
                dl->AddCircleFilled(ImVec2(q.x + 5.0f * k, q.y - 3.0f * k), 3.0f * k, IM_COL32(15, 43, 37, 255));
            }
            hit_min[i] = ImVec2(c.x - w / 2, c.y - h / 2);
            hit_max[i] = ImVec2(c.x + w / 2, c.y + h / 2);
            text_centered(dl, f_title, (i == 1 ? 62.0f : 52.0f) * k * grow, ImVec2(c.x, c.y + 2.0f * k), kCream, label(i), true);
        }

        const float body = 64.0f * k, note = 50.0f * k;
        if (guide) {
            const char* lines[] = {
                "1. Dump the game and its update from your own Switch 2 (a .nup file, or .nsp / .xci).",
                "2. For .nsp / .xci, also dump your console's prod.keys: they open the dump and stay on your PC.",
                "3. Choose Select game file here and pick your dump (then the keys, for .nsp / .xci).",
                "Back: B / Esc",
            };
            float y = D.y * 0.62f;
            for (const char* l : lines) {
                text_centered(dl, f_body, 48.0f * k, ImVec2(D.x * 0.5f, y), l[0] == 'B' ? kOrange : kCream, l, false);
                y += 62.0f * k;
            }
        } else {
            text_centered(dl, f_body, body, ImVec2(D.x * 0.5f, D.y * 0.68f), kCream,
                          have_files ? "Your game files are ready." : "Choose the game file (.nup, .nsp or .xci) you dumped from your own Switch 2.", false);
            if (!status.empty()) text_centered(dl, f_body, note, ImVec2(D.x * 0.5f, D.y * 0.74f), kOrange, status.c_str(), false);
        }
        text_centered(dl, f_body, note, ImVec2(D.x * 0.5f, D.y * 0.865f), kOrange, "No game files are included or downloaded.", false);
        text_centered(dl, f_body, note, ImVec2(D.x * 0.5f, D.y * 0.865f + 60.0f * k), kOrange, "You need to own the game and dump it from your console.", false);
    }
};

// Runs a command line, waiting for it; the exit code (-1 when it did not start).
int run_wait(std::wstring cmd) {
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

bool ends_with_ci(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
    return true;
}

// A Switch 2 .nup (unencrypted): tools\extract-exefs.mjs next to the game (Node.js 24+) extracts the update's ExeFS and RomFS into
// <game folder>\{exefs,romfs} (the installed layout game_main looks for at the next start); [fs] points at them for this run. Empty on
// success, else why.
std::string extract_nup(const std::string& nup) {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::filesystem::path dir = std::filesystem::path(self).parent_path();
    const std::filesystem::path script = dir / "tools" / "extract-exefs.mjs";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec)) return "The .nup extractor (tools\\extract-exefs.mjs) is not next to the game.";
    const std::filesystem::path out = dir;
    // The package's own Node.js (tools\node\node.exe) so players need not install it; else one on PATH.
    const std::filesystem::path bundled = dir / "tools" / "node" / "node.exe";
    const std::wstring node = std::filesystem::is_regular_file(bundled, ec) ? L"\"" + bundled.wstring() + L"\"" : std::wstring(L"node");
    for (const char* part : {"exefs", "romfs"}) {
        const std::wstring cmd = node + L" \"" + script.wstring() + L"\" \"" + std::filesystem::path(nup).wstring() + L"\" \"" + (out / part).wstring() +
                                 L"\" update " + std::filesystem::path(part).wstring();
        const int code = run_wait(cmd);
        if (code == -1) return "Node.js is missing (tools\\node\\node.exe next to the game, or Node.js 24 or newer installed).";
        if (code != 0) return std::string("Extracting the ") + part + " failed (code " + std::to_string(code) + ").";
    }
    config_set("fs", "exefs", (out / "exefs").string().c_str());
    config_set("fs", "romfs", (out / "romfs").string().c_str());
    return {};
}

// Runs nx_extract (tools\nx_extract.exe next to the game) on the player's .nsp / .xci with their keys into <game folder>\dump, and points
// [fs] at the result for this run. Empty on success, else why.
std::string extract_dump(const std::string& dump, const std::string& keys) {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::filesystem::path dir = std::filesystem::path(self).parent_path();
    const std::filesystem::path tool = dir / "tools" / "nx_extract.exe";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(tool, ec)) return "The extractor (tools\\nx_extract.exe) is not installed next to the game.";
    const std::filesystem::path out = dir / "dump";
    const std::string title = config_get_str("process", "program_id", "");
    std::wstring cmd = L"\"" + tool.wstring() + L"\" --input \"" + std::filesystem::path(dump).wstring() + L"\" --keys \"" +
                       std::filesystem::path(keys).wstring() + L"\" --out \"" + out.wstring() + L"\"";
    if (!title.empty()) cmd += L" --title-id " + std::filesystem::path(title.substr(title.rfind('x') + 1)).wstring();
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return "The extractor did not start.";
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code == 2) return "That file is not a readable dump of the game.";
    if (code == 3) return "Those keys do not open this dump (use your own console's prod.keys).";
    if (code != 0) return "Extracting failed (code " + std::to_string(code) + "). Details are in the log.";
    config_set("fs", "exefs", (out / "base" / "exefs").string().c_str());
    config_set("fs", "romfs", (out / "base" / "romfs").string().c_str());
    return {};
}

}  // namespace

bool start_menu(gfx::Renderer* renderer, gfx::Overlay* ov, FrameLimiter* limiter) {
    if (!renderer || !ov || renderer->headless()) return true;
    if (!config_get_bool("launcher", "start_menu", true) || !config_get_str("play", "selftest", "").empty()) return true;
    Menu m;
    m.ov = ov;
    m.have_files = game_files_present();
    m.f_title = static_cast<ImFont*>(ov->font("title"));
    m.f_body = static_cast<ImFont*>(ov->font("body"));
    if (m.have_files) {
        std::vector<std::pair<std::string, gfx::GameMask>> masks;
        std::string why;
        if (gfx::load_layout_masks("BtnFirstDifficulty_00", {"TabCmn_A_02", "TabCmn_B_02", "TabCmn_C_02", "PrintTexture", "StickerPattern_06",
                                                             "StickerPattern_10", "StickerPattern_09"}, &masks, &why)) {
            const gfx::GameMask* grain = find_mask(masks, "PrintTexture");
            const char* shapes[3] = {"TabCmn_A_02", "TabCmn_B_02", "TabCmn_C_02"};
            const char* stick_a[3] = {"StickerPattern_06", "StickerPattern_10", "StickerPattern_09"};
            const Rgb colours[3] = {kGreen, kTeal, kRed};
            for (int i = 0; i < 3; ++i)
                if (const gfx::GameMask* s = find_mask(masks, shapes[i])) {
                    const std::vector<uint8_t> px = compose_banner(*s, grain, find_mask(masks, stick_a[i]), find_mask(masks, stick_a[(i + 1) % 3]), colours[i], i);
                    m.tex_banner[i] = ov->add_texture_rgba(s->w, s->h, px.data());
                }
            if (grain) {
                const std::vector<uint8_t> px = compose_backdrop(*grain);
                m.tex_bg = ov->add_texture_rgba(grain->w, grain->h, px.data());
            }
            m.styled = m.tex_banner[1] != 0;
        } else {
            NXLOG_W(kTag, "start menu: the game's menu textures are not available (%s): plain look", why.c_str());
        }
    }
    NXLOG_I(kTag, "start menu: game files %s, %s look", m.have_files ? "found" : "missing", m.styled ? "the game's" : "plain");
    ov->set_screen([&m] { m.draw(); });

    uint32_t pad_prev = 0;
    float stick_prev = 0.0f;
    bool done = false;
    // [launcher] menu_screenshot = "path.png" (tests): the menu's own frame is saved after 60 frames, then the menu goes on to Play
    const std::string shot = config_get_str("launcher", "menu_screenshot", "");
    int frames = 0;
    bool busy = false;                 // an extraction is running on `worker`
    std::atomic<bool> job_done{false};
    std::string job_err;
    std::thread worker;
    while (!done) {
        if (!shot.empty() && ++frames == 60) renderer->request_screenshot(shot);
        if (!shot.empty() && frames == 70) done = true;
        platform::pump_events(false, 0);
        if (platform::quit_requested()) {
            if (worker.joinable()) worker.join();   // closing mid-extraction: let it finish writing (it holds this frame's locals)
            ov->set_screen(nullptr);
            return false;
        }
        int move = 0;
        bool activate = false, back = false;
        input::Event evs[64];
        const size_t ne = input::queue_drain(evs, 64);
        for (size_t i = 0; i < ne; ++i) {
            const input::Event& e = evs[i];
            if (e.type == input::EventType::KeyDown && !(e.flags & input::kFlagRepeat)) {
                const int sc = e.u.key.scancode;
                if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_A) move = -1;
                else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_D) move = 1;
                else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_KP_ENTER) activate = true;
                else if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE) back = true;
            } else if (e.type == input::EventType::MouseMotion || e.type == input::EventType::MouseButtonDown) {
                const float mx = e.type == input::EventType::MouseMotion ? e.u.motion.x : e.u.button.x;
                const float my = e.type == input::EventType::MouseMotion ? e.u.motion.y : e.u.button.y;
                for (int b = 0; b < 3; ++b)
                    if (!m.guide && mx >= m.hit_min[b].x && mx <= m.hit_max[b].x && my >= m.hit_min[b].y && my <= m.hit_max[b].y) {
                        m.sel = b;
                        if (e.type == input::EventType::MouseButtonDown && e.u.button.button == 1) activate = true;
                    }
                if (m.guide && e.type == input::EventType::MouseButtonDown) back = true;
            }
        }
        {   // pads: D-pad or left stick moves, the bottom face button activates, the right one goes back
            input::GamepadInfo infos[8];
            const size_t np = input::gamepad_list(infos, 8);
            uint32_t buttons = 0;
            float lx = 0.0f;
            for (size_t i = 0; i < np; ++i) {
                platform::GamepadRaw raw;
                if (!platform::gamepad_poll(infos[i].id, &raw)) continue;
                buttons |= raw.buttons;
                const float x = raw.axes[0] / 32767.0f;
                if (std::fabs(x) > std::fabs(lx)) lx = x;
            }
            const uint32_t pressed = buttons & ~pad_prev;
            pad_prev = buttons;
            if (pressed & (1u << 13)) move = -1;
            if (pressed & (1u << 14)) move = 1;
            if (pressed & 1u) activate = true;
            if (pressed & 2u) back = true;
            if (lx < -0.6f && stick_prev >= -0.6f) move = -1;
            if (lx > 0.6f && stick_prev <= 0.6f) move = 1;
            stick_prev = lx;
        }
        if (m.guide) {
            if (back || activate) m.guide = false;
        } else {
            if (move) m.sel = std::clamp(m.sel + move, 0, 2);
            if (back) m.sel = 2;
            if (busy) activate = false;   // buttons wait while the game files are being extracted
            if (activate) {
                if (m.sel == 0) {
                    m.guide = true;
                } else if (m.sel == 2) {
                    NXLOG_I(kTag, "start menu: quit");
                    ov->set_screen(nullptr);
                    return false;
                } else if (m.have_files) {
                    done = true;
                } else {
                    const std::string dump = pick_file(L"Choose your game dump", L"Switch 2 game dump (*.nup;*.nsp;*.xci)\0*.nup;*.nsp;*.xci\0All files\0*.*\0");
                    const bool nup = ends_with_ci(dump, ".nup");
                    const std::string keys = dump.empty() || nup ? std::string() : pick_file(L"Choose your console's prod.keys", L"Keys (prod.keys)\0*.keys\0All files\0*.*\0");
                    if (!dump.empty() && (nup || !keys.empty()) && !busy) {
                        // On a worker: the extraction takes minutes, and the window keeps drawing (and answering Windows) meanwhile.
                        m.status = "Extracting your game files (about 15 GB, a few minutes)...";
                        busy = true;
                        job_done.store(false);
                        worker = std::thread([&, dump, keys, nup] {
                            job_err = nup ? extract_nup(dump) : extract_dump(dump, keys);
                            job_done.store(true);
                        });
                    }
                }
            }
        }
        if (busy && job_done.load()) {
            worker.join();
            busy = false;
            m.status = job_err;
            NXLOG_I(kTag, "start menu: extraction %s", job_err.empty() ? "done" : job_err.c_str());
            if (job_err.empty() && game_files_present()) done = true;
            else if (job_err.empty()) m.status = "The extracted files are not the expected game (exefs\\main or the RomFS is missing).";
        }
        diag_progress();   // the menu is not a game frame: keep the freeze watchdog quiet while it is up
        if (limiter) limiter->begin_frame_nohook();
        else std::this_thread::sleep_for(std::chrono::milliseconds(16));
        gfx::FrameInfo fi;
        if (renderer->begin_frame(&fi)) renderer->end_frame();
    }
    ov->set_screen(nullptr);
    input::queue_clear();
    NXLOG_I(kTag, "start menu: play");
    return true;
}

}  // namespace nxrt::play
