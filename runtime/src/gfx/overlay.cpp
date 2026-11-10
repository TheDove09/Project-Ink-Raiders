// SPDX-License-Identifier: GPL-3.0-or-later
// Dear ImGui overlay (declarations in include/nxrt/overlay.h). ImGui and its Vulkan backend are pinned in cmake/gfx_deps.cmake.
#include "nxrt/overlay.h"

#include <atomic>
#include <mutex>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "nxrt/config.h"
#include "nxrt/log.h"
#include "nxrt/nvn.h"
#include "nxrt/platform.h"
#include "extras/overlay_composite.h"
#include "fps_stats.h"
#include "game_font.h"

#include <cstring>
#include <string>

namespace nxrt::gfx {
namespace {
constexpr const char* kTag = "overlay";

// [gfx] menu_theme = splat (default): the settings menu in the game's colours. Deep ink-teal panels like the game's own option screens,
// lime for what is selected or switched on, orange for what the cursor is over, cream text, rounded shapes.
void apply_splat_style(ImGuiStyle& s) {
    auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
    const ImVec4 panel = rgb(16, 58, 56, 0.97f), panel2 = rgb(24, 82, 78), panel3 = rgb(34, 104, 98);
    const ImVec4 lime = rgb(206, 238, 40), lime_dim = rgb(160, 190, 30), orange = rgb(245, 110, 64), orange_dim = rgb(214, 88, 50);
    const ImVec4 cream = rgb(244, 238, 218), muted = rgb(150, 180, 172), ink = rgb(10, 36, 34);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = cream;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = rgb(12, 46, 44, 0.6f);
    c[ImGuiCol_PopupBg] = rgb(14, 52, 50, 0.98f);
    c[ImGuiCol_Border] = rgb(60, 140, 130, 0.6f);
    c[ImGuiCol_FrameBg] = panel2;
    c[ImGuiCol_FrameBgHovered] = panel3;
    c[ImGuiCol_FrameBgActive] = rgb(44, 120, 112);
    c[ImGuiCol_TitleBg] = ink;
    c[ImGuiCol_TitleBgActive] = rgb(12, 44, 42);
    c[ImGuiCol_TitleBgCollapsed] = ink;
    c[ImGuiCol_MenuBarBg] = ink;
    c[ImGuiCol_ScrollbarBg] = rgb(8, 30, 28, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = panel3;
    c[ImGuiCol_ScrollbarGrabHovered] = orange_dim;
    c[ImGuiCol_ScrollbarGrabActive] = orange;
    c[ImGuiCol_CheckMark] = lime;
    c[ImGuiCol_SliderGrab] = lime_dim;
    c[ImGuiCol_SliderGrabActive] = lime;
    c[ImGuiCol_Button] = panel3;
    c[ImGuiCol_ButtonHovered] = orange_dim;
    c[ImGuiCol_ButtonActive] = orange;
    c[ImGuiCol_Header] = rgb(40, 112, 104);
    c[ImGuiCol_HeaderHovered] = orange_dim;
    c[ImGuiCol_HeaderActive] = orange;
    c[ImGuiCol_Separator] = rgb(60, 140, 130, 0.5f);
    c[ImGuiCol_SeparatorHovered] = orange_dim;
    c[ImGuiCol_SeparatorActive] = orange;
    c[ImGuiCol_ResizeGrip] = rgb(60, 140, 130, 0.4f);
    c[ImGuiCol_ResizeGripHovered] = orange_dim;
    c[ImGuiCol_ResizeGripActive] = orange;
    c[ImGuiCol_Tab] = rgb(20, 70, 66);
    c[ImGuiCol_TabHovered] = orange_dim;
    c[ImGuiCol_TabSelected] = orange;
    c[ImGuiCol_TabSelectedOverline] = lime;
    c[ImGuiCol_TabDimmed] = rgb(16, 56, 54);
    c[ImGuiCol_TabDimmedSelected] = rgb(140, 70, 44);
    c[ImGuiCol_PlotHistogram] = lime_dim;
    c[ImGuiCol_PlotHistogramHovered] = lime;
    c[ImGuiCol_TextSelectedBg] = rgb(206, 238, 40, 0.35f);
    c[ImGuiCol_NavCursor] = lime;
    s.WindowRounding = 14.0f;
    s.ChildRounding = 10.0f;
    s.FrameRounding = 9.0f;
    s.PopupRounding = 10.0f;
    s.ScrollbarRounding = 9.0f;
    s.GrabRounding = 9.0f;
    s.TabRounding = 9.0f;
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.GrabMinSize = 14.0f;
    s.WindowBorderSize = 2.0f;
    s.FrameBorderSize = 0.0f;
    s.TabBorderSize = 0.0f;
    s.SeparatorTextBorderSize = 2.0f;
}

ImGuiKey map_key(int sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return static_cast<ImGuiKey>(ImGuiKey_A + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return static_cast<ImGuiKey>(ImGuiKey_1 + (sc - SDL_SCANCODE_1));
    if (sc == SDL_SCANCODE_0) return ImGuiKey_0;
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (sc - SDL_SCANCODE_F1));
    switch (sc) {
        case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
        case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
        case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
        case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
        case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
        case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
        case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
        case SDL_SCANCODE_HOME: return ImGuiKey_Home;
        case SDL_SCANCODE_END: return ImGuiKey_End;
        case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
        case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
        case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
        case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
        case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
        case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
        case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
        case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
        case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
        case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
        case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
        case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
        default: return ImGuiKey_None;
    }
}

const double kFpsChoices[] = {0, 30, 60, 90, 120, 144, 165, 240, 360};
}  // namespace

namespace {
std::atomic<int> g_shader_backlog{0};
}
void set_shader_backlog(int pending) { g_shader_backlog.store(pending < 0 ? 0 : pending, std::memory_order_relaxed); }
int shader_backlog() { return g_shader_backlog.load(std::memory_order_relaxed); }

struct Overlay::Impl {
    Context ctx;
    ImGuiContext* imgui = nullptr;
    ImGuiStyle base_style;            // the style at scale 1; style = base_style scaled by ui_scale (update_scale)
    float ui_scale = 0.f;             // 0: not applied yet
    bool backend_ready = false;
    std::atomic<bool> visible{false};
    std::mutex mu;                    // guards the ImGui context: events arrive on the game's main thread, drawing happens on the presenting thread
    int toggle_sc = SDL_SCANCODE_F1;
    bool restore_relative = false;
    double hint_start_s = -1.0;      // first draw (steady clock seconds): the title-screen hint counts from here
    std::function<void()> screen;    // the start menu while it is up (set_screen)
    std::vector<std::pair<Image, VkSampler>> menu_images;   // add_texture_rgba
    ImFont* font_title = nullptr;    // the game's banner font (NinDev-Treasure_00), when the romfs has it
    ImFont* font_body = nullptr;     // the game's text font (SpMain)
    bool pad_combo = false;         // View + Menu held at the last feed_gamepad (the toggle fires on the press only)
    uint32_t pad_prev = 0;           // pad buttons sent to ImGui at the last feed_gamepad
    std::chrono::steady_clock::time_point last_frame = std::chrono::steady_clock::now();
    uint64_t drawn = 0;

    struct Panel { std::string title; std::function<void()> fn; };
    std::vector<Panel> panels;

    // menu state applied by apply_pending()
    bool want_mode = false;
    PresentMode new_mode = PresentMode::Fifo;
    bool want_fullscreen = false;
    platform::FullscreenMode new_fullscreen = platform::FullscreenMode::Windowed;

    float frame_ms[120] = {};
    int frame_ms_pos = 0;
    int fps_choice = 0;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    bool hdr = false;                 // HDR swapchain: the overlay is drawn into an RGBA8 target and composited (draw)
    extras::OverlayComposite composite;

    // Corner frame-rate counter. set_counter() stores the settings from any thread; the rest is used by the presenting thread only.
    std::atomic<bool> ptr_show{false}, ptr_in_shots{true};
    std::atomic<float> ptr_x{0}, ptr_y{0};
    std::atomic<bool> counter_show{false};
    std::atomic<int> counter_corner{0};
    std::atomic<int> counter_detail{1};
    std::atomic<bool> counter_in_shots{false};
    bool counter_was_on = false;
    bool counter_have_prev = false;
    std::chrono::steady_clock::time_point counter_prev{};
    FpsStats stats;
    double text_s = -1e9;                 // time of the last text refresh, seconds (far past: refresh at once)
    FpsStats::Snapshot snap;              // the snapshot taken at text_s
    std::string counter_lines[4];
    int counter_n = 0;

    void build_ui();
    void update_scale(const VkExtent2D& extent);
    void refresh_counter_text(double now_s);
    void draw_counter(const FrameInfo& f, double now_s);
    // The "Preparing shaders" note: shown once the backlog has stood for a second and a half, gone a second after it is empty.
    bool note_enabled = true;
    bool note_read = false;
    double note_since_s = -1.0, note_empty_s = -1.0;
    int note_peak = 0;
    bool note_visible(double now_s);
    void draw_note(const FrameInfo& f);
};

void Overlay::Impl::build_ui() {
    Renderer& r = *ctx.renderer;
    // First-use layout, in units of the UI scale (update_scale): the panels (the settings menu) sit at the top left, the diagnostics window
    // at the top right and folded up. The player moves them; nothing is remembered between runs.
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float k = ui_scale > 0.f ? ui_scale : 1.f;
    const float diag_w = std::min(420.f * k, std::max(200.f, disp.x - 32.f * k));
    ImGui::SetNextWindowPos(ImVec2(std::max(16.f * k, disp.x - diag_w - 16.f * k), 16.f * k), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(diag_w, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Diagnostics")) {
        const GpuInfo& g = ctx.device->gpu();
        ImGui::Text("GPU: %s", g.name.c_str());
        ImGui::Text("Vulkan 1.3, driver 0x%08x, validation %s%s", g.driver_version, ctx.device->validation_enabled() ? "on" : "off",
                    ctx.device->headless() ? ", headless" : "");
        const VkExtent2D e = r.extent();
        ImGui::Text("Target: %ux%u, %u images, %u frames in flight", e.width, e.height, r.image_count(), r.frames_in_flight());

        ImGui::SeparatorText("Presentation");
        const PresentMode modes[] = {PresentMode::Fifo, PresentMode::FifoRelaxed, PresentMode::Mailbox, PresentMode::Immediate};
        int cur = static_cast<int>(r.requested_present_mode());
        if (ImGui::BeginCombo("Present mode", present_mode_name(modes[cur]))) {
            for (int i = 0; i < 4; ++i) {
                const bool ok = r.present_mode_supported(modes[i]);
                ImGui::BeginDisabled(!ok);
                if (ImGui::Selectable(present_mode_name(modes[i]), i == cur)) { want_mode = true; new_mode = modes[i]; }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if (r.present_mode() != r.requested_present_mode()) ImGui::TextDisabled("in use: %s (requested mode unavailable)", present_mode_name(r.present_mode()));

        if (ctx.limiter) {
            char label[32];
            const double t = ctx.limiter->target_fps();
            std::snprintf(label, sizeof label, t > 0 ? "%.0f fps" : "unlimited", t);
            if (ImGui::BeginCombo("Frame cap", label)) {
                for (double v : kFpsChoices) {
                    std::snprintf(label, sizeof label, v > 0 ? "%.0f fps" : "unlimited", v);
                    if (ImGui::Selectable(label, v == t)) ctx.limiter->set_target_fps(v);
                }
                ImGui::EndCombo();
            }
            const float hz = platform::has_window() ? platform::display_refresh_hz() : 0.f;
            if (hz > 0) {
                ImGui::TextDisabled("display %.2f Hz; VRR-friendly cap %.1f fps", hz, suggest_vrr_cap(hz));
                if (ImGui::SmallButton("Use VRR cap")) ctx.limiter->set_target_fps(suggest_vrr_cap(hz));
            }
        }

        if (ctx.has_window && platform::has_window()) {
            ImGui::SeparatorText("Window");
            const auto fm = platform::fullscreen_mode();
            if (ImGui::RadioButton("Windowed", fm == platform::FullscreenMode::Windowed)) { want_fullscreen = true; new_fullscreen = platform::FullscreenMode::Windowed; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Borderless", fm == platform::FullscreenMode::Borderless)) { want_fullscreen = true; new_fullscreen = platform::FullscreenMode::Borderless; }
            ImGui::SameLine();
            if (ImGui::RadioButton("Exclusive", fm == platform::FullscreenMode::Exclusive)) { want_fullscreen = true; new_fullscreen = platform::FullscreenMode::Exclusive; }
            bool rel = platform::relative_mouse();
            if (ImGui::Checkbox("Raw mouse (relative mode)", &rel)) platform::set_relative_mouse(rel);
            ImGui::SameLine();
            ImGui::TextDisabled(platform::relative_mouse_is_raw() ? "(unaccelerated)" : "(system scaling!)");
        }

        ImGui::SeparatorText("Timing");
        const RendererStats rs = r.stats();
        const float dt_ms = ImGui::GetIO().DeltaTime * 1000.f;
        frame_ms[frame_ms_pos] = dt_ms;
        frame_ms_pos = (frame_ms_pos + 1) % 120;
        ImGui::Text("CPU frame %.2f ms (%.0f fps)   GPU %.2f ms (avg %.2f)", dt_ms, dt_ms > 0 ? 1000.f / dt_ms : 0.f, rs.gpu_ms_last, rs.gpu_ms_avg);
        ImGui::PlotLines("##ft", frame_ms, 120, frame_ms_pos, nullptr, 0.f, 33.f, ImVec2(0, 48));
        ImGui::Text("frames %llu, swapchains %llu, out-of-date %llu, screenshots %llu", static_cast<unsigned long long>(rs.frames),
                    static_cast<unsigned long long>(rs.swapchain_creates), static_cast<unsigned long long>(rs.out_of_date),
                    static_cast<unsigned long long>(rs.screenshots));
        const ValidationStats vs = ctx.device->validation_stats();
        ImGui::Text("validation: %llu errors, %llu warnings", static_cast<unsigned long long>(vs.errors), static_cast<unsigned long long>(vs.warnings));

        ImGui::SeparatorText("NVN front end");
        ImGui::Text("%zu entry points, %zu implemented; stub calls %llu (%zu distinct)", nvn::entry_count(), nvn::implemented_count(),
                    static_cast<unsigned long long>(nvn::stub_calls_total()), nvn::stubs_called_distinct());
        if (ImGui::TreeNode("Most called stubs")) {
            auto calls = nvn::called_entries();
            int shown = 0;
            for (auto& c : calls) {
                if (c.implemented) continue;
                ImGui::Text("%8llu  %s", static_cast<unsigned long long>(c.calls), c.entry->name);
                if (++shown >= 12) break;
            }
            if (!shown) ImGui::TextDisabled("none called");
            ImGui::TreePop();
        }
    }
    ImGui::End();
    for (auto& p : panels) {
        ImGui::SetNextWindowPos(ImVec2(16.f * k, 16.f * k), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(760.f * k, std::max(240.f, disp.x - 32.f * k)), std::min(680.f * k, std::max(200.f, disp.y - 32.f * k))),
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin(p.title.c_str())) p.fn();
        ImGui::End();
    }
}

// DPI: the UI is laid out for a 1080 pixel high frame at 16 px text and grows with the frame (4K: twice the size; a Windows display scaling
// of 150% shows up as more pixels in the same window, so it is covered too). Never below 1: a 720p window keeps the 16 px text.
void Overlay::Impl::update_scale(const VkExtent2D& extent) {
    // [gfx] menu_scale: the whole menu (text and spacing) times this on top of the resolution scale (default 1.4: the user found 16 px small)
    static const float user_scale = static_cast<float>(std::clamp(config_get_double("gfx", "menu_scale", 1.4), 0.75, 3.0));
    const float want = std::round(std::clamp(static_cast<float>(extent.height) / 1080.f, 1.0f, 3.0f) * user_scale * 20.f) / 20.f;   // steps of 0.05
    if (want == ui_scale) return;
    ui_scale = want;
    ImGuiStyle& style = ImGui::GetStyle();
    style = base_style;
    style.ScaleAllSizes(want);
    style.FontScaleDpi = want;
}

std::unique_ptr<Overlay> Overlay::create(const Context& ctx, std::string* error) {
    if (!ctx.device || !ctx.renderer) {
        if (error) *error = "overlay needs a device and a renderer";
        return nullptr;
    }
    std::unique_ptr<Overlay> o(new Overlay());
    o->impl_ = new Impl();
    Impl& s = *o->impl_;
    s.ctx = ctx;
    s.color_format = ctx.renderer->format();
    if (ctx.renderer->output_kind() != extras::OutputKind::Sdr8) {
        // HDR swapchain: ImGui draws into an RGBA8 target that the composite maps onto the swapchain (see draw)
        if (s.composite.init(*ctx.device)) {
            s.hdr = true;
            s.color_format = VK_FORMAT_R8G8B8A8_UNORM;
        } else {
            NXLOG_W(kTag, "HDR overlay composite failed to initialise: the overlay is drawn straight into the HDR swapchain (colours will look wrong)");
        }
    }
    const std::string key = config_get_str("gfx", "overlay_key", "F1");
    if (const SDL_Scancode sc = SDL_GetScancodeFromName(key.c_str()); sc != SDL_SCANCODE_UNKNOWN) s.toggle_sc = sc;
    else NXLOG_W(kTag, "unknown [gfx] overlay_key '%s': using F1", key.c_str());

    IMGUI_CHECKVERSION();
    s.imgui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;     // no imgui.ini next to the exe
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;   // fed by feed_gamepad (the pads, from play mode)
    ImGui::StyleColorsDark();
    if (config_get_str("gfx", "menu_theme", "splat") == "splat") apply_splat_style(ImGui::GetStyle());
    // [gfx] menu_game_font (default on): the menus use the game's own UI font (SpMain) from the player's romfs, with the scalable default
    // font merged in for the glyphs it lacks; without the romfs font the default font alone, as before.
    bool game_font = false;
    if (config_get_bool("gfx", "menu_game_font", true)) {
        std::string why;
        const std::vector<uint8_t> otf = load_game_font(config_get_str("gfx", "menu_font_name", "SpMain"), &why);
        if (!otf.empty()) {
            void* mem = IM_ALLOC(otf.size());   // the atlas owns and frees it (IM_FREE)
            std::memcpy(mem, otf.data(), otf.size());
            ImFontConfig fc;
            std::snprintf(fc.Name, sizeof fc.Name, "game font");
            if (ImFont* body = io.Fonts->AddFontFromMemoryTTF(mem, static_cast<int>(otf.size()), 0.0f, &fc)) {
                ImFontConfig merge;
                merge.MergeMode = true;
                io.Fonts->AddFontDefaultVector(&merge);
                game_font = true;
                s.font_body = body;
                NXLOG_I(kTag, "menu font: the game's %zu-byte UI font", otf.size());
                // the banner font of the game's menus (the start menu's title and buttons)
                std::string why2;
                const std::vector<uint8_t> title = load_game_font("NinDev-Treasure_00", &why2);
                if (!title.empty()) {
                    void* tm = IM_ALLOC(title.size());
                    std::memcpy(tm, title.data(), title.size());
                    ImFontConfig tc;
                    std::snprintf(tc.Name, sizeof tc.Name, "game banner font");
                    s.font_title = io.Fonts->AddFontFromMemoryTTF(tm, static_cast<int>(title.size()), 0.0f, &tc);
                }
            }
        } else {
            NXLOG_I(kTag, "menu font: the game's font is not available (%s): the default font", why.c_str());
        }
    }
    if (!game_font) io.Fonts->AddFontDefaultVector();          // the scalable default font: stays sharp at every UI scale (update_scale)
    ImGui::GetStyle().FontSizeBase = 16.0f;
    s.base_style = ImGui::GetStyle();
    const VkExtent2D e = ctx.renderer->extent();
    io.DisplaySize = ImVec2(static_cast<float>(e.width), static_cast<float>(e.height));

    VkPipelineRenderingCreateInfoKHR rci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &s.color_format;
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = ctx.device->instance();
    ii.PhysicalDevice = ctx.device->physical();
    ii.Device = ctx.device->device();
    ii.QueueFamily = ctx.device->queue_family();
    ii.Queue = ctx.device->queue();
    ii.DescriptorPoolSize = 16;
    ii.MinImageCount = 2;
    ii.ImageCount = std::max(2u, ctx.renderer->image_count());
    ii.UseDynamicRendering = true;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo = rci;
    ii.CheckVkResultFn = [](VkResult r) { if (r != VK_SUCCESS) NXLOG_E(kTag, "ImGui Vulkan call failed (%d)", static_cast<int>(r)); };
    if (!ImGui_ImplVulkan_Init(&ii)) {
        if (error) *error = "ImGui_ImplVulkan_Init failed";
        ImGui::DestroyContext(s.imgui);
        s.imgui = nullptr;
        return nullptr;
    }
    s.backend_ready = true;
    NXLOG_I(kTag, "overlay ready (toggle key %s)", SDL_GetScancodeName(static_cast<SDL_Scancode>(s.toggle_sc)));
    return o;
}

Overlay::~Overlay() {
    if (!impl_) return;
    if (impl_->ctx.device) impl_->ctx.device->wait_idle();
    impl_->composite.shutdown();
    if (impl_->backend_ready) ImGui_ImplVulkan_Shutdown();
    if (impl_->imgui) ImGui::DestroyContext(impl_->imgui);
    delete impl_;
}

void Overlay::set_visible(bool v) {
    Impl& s = *impl_;
    if (s.visible == v) return;
    s.visible = v;
    if (v) {
        s.restore_relative = platform::has_window() && platform::relative_mouse();
        if (s.restore_relative) platform::set_relative_mouse(false);
    } else {
        if (s.restore_relative && platform::has_window()) platform::set_relative_mouse(true);
        s.restore_relative = false;
        std::lock_guard<std::mutex> lk(s.mu);
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(s.imgui);
        ImGuiIO& io = ImGui::GetIO();   // drop pressed keys and buttons so nothing stays latched
        io.ClearInputKeys();
        io.ClearInputMouse();
        ImGui::SetCurrentContext(prev);
    }
    NXLOG_I(kTag, "overlay %s", v ? "shown" : "hidden");
}

bool Overlay::visible() const { return impl_->visible; }
void Overlay::toggle() { set_visible(!impl_->visible); }

void Overlay::set_screen(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->screen = std::move(fn);
}

void* Overlay::font(const char* which) const {
    if (!which) return nullptr;
    if (std::strcmp(which, "title") == 0) return impl_->font_title;
    if (std::strcmp(which, "body") == 0) return impl_->font_body;
    return nullptr;
}

uint64_t Overlay::add_texture_rgba(int w, int h, const uint8_t* rgba) {
    Impl& s = *impl_;
    if (!s.backend_ready || w <= 0 || h <= 0 || !rgba) return 0;
    Device& dev = *s.ctx.device;
    ImageDesc id;
    id.width = static_cast<uint32_t>(w);
    id.height = static_cast<uint32_t>(h);
    id.format = VK_FORMAT_R8G8B8A8_UNORM;
    id.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    id.name = "menu texture";
    Image img;
    std::string why;
    if (dev.create_image(id, &img, &why) != VK_SUCCESS) {
        NXLOG_W(kTag, "menu texture %dx%d: %s", w, h, why.c_str());
        return 0;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
    Buffer up;
    if (dev.create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::Upload, &up, "menu texture upload") != VK_SUCCESS) {
        dev.destroy_image(img);
        return 0;
    }
    std::memcpy(up.mapped, rgba, static_cast<size_t>(bytes));
    dev.flush_buffer(up);
    const bool ok = dev.submit_once([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy c = {};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, up.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    });
    dev.destroy_buffer(up);
    if (!ok) {
        dev.destroy_image(img);
        return 0;
    }
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;   // the backdrop tiles its grain
    si.maxLod = 0.0f;
    VkSampler smp = VK_NULL_HANDLE;
    if (vkCreateSampler(dev.device(), &si, nullptr, &smp) != VK_SUCCESS) {
        dev.destroy_image(img);
        return 0;
    }
    std::lock_guard<std::mutex> lk(s.mu);
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s.imgui);
    const VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(smp, img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ImGui::SetCurrentContext(prev);
    s.menu_images.push_back({img, smp});   // kept for the process' life (a handful of small textures)
    return reinterpret_cast<uint64_t>(ds);
}

void Overlay::feed_gamepad(uint32_t b, float lx, float ly) {
    Impl& s = *impl_;
    constexpr uint32_t kBack = 1u << 4, kStart = 1u << 6;   // SDL_GAMEPAD_BUTTON_BACK / START
    const bool combo = (b & kBack) && (b & kStart);
    if (combo && !s.pad_combo) toggle();
    s.pad_combo = combo;
    if (!s.visible) {
        s.pad_prev = 0;
        return;
    }
    if (combo) b = 0;                                       // the toggle chord itself does nothing in the menu
    std::lock_guard<std::mutex> lk(s.mu);
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s.imgui);
    ImGuiIO& io = ImGui::GetIO();
    // SDL3 gamepad button numbers -> ImGui gamepad keys (south = the bottom face button: activate; east: back)
    static const struct { int bit; ImGuiKey key; } kMap[] = {
        {0, ImGuiKey_GamepadFaceDown}, {1, ImGuiKey_GamepadFaceRight}, {2, ImGuiKey_GamepadFaceLeft}, {3, ImGuiKey_GamepadFaceUp},
        {4, ImGuiKey_GamepadBack}, {6, ImGuiKey_GamepadStart}, {9, ImGuiKey_GamepadL1}, {10, ImGuiKey_GamepadR1},
        {11, ImGuiKey_GamepadDpadUp}, {12, ImGuiKey_GamepadDpadDown}, {13, ImGuiKey_GamepadDpadLeft}, {14, ImGuiKey_GamepadDpadRight},
    };
    for (const auto& m : kMap) {
        const bool now = (b >> m.bit) & 1u, was = (s.pad_prev >> m.bit) & 1u;
        if (now != was) io.AddKeyEvent(m.key, now);
    }
    s.pad_prev = b;
    auto axis = [&](ImGuiKey neg, ImGuiKey pos, float v) {
        const float d = 0.25f;
        const float a = std::fabs(v) < d ? 0.0f : (std::fabs(v) - d) / (1.0f - d);
        io.AddKeyAnalogEvent(neg, v < -d, v < -d ? a : 0.0f);
        io.AddKeyAnalogEvent(pos, v > d, v > d ? a : 0.0f);
    };
    axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, lx);
    axis(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown, ly);
    ImGui::SetCurrentContext(prev);
}
int Overlay::toggle_scancode() const { return impl_->toggle_sc; }
void Overlay::set_toggle_scancode(int sc) { impl_->toggle_sc = sc; }
uint64_t Overlay::frames_drawn() const { return impl_->drawn; }

void Overlay::set_pointer(bool show, float x, float y, bool in_screenshots) {
    Impl& s = *impl_;
    s.ptr_x.store(x, std::memory_order_relaxed);
    s.ptr_y.store(y, std::memory_order_relaxed);
    s.ptr_in_shots.store(in_screenshots, std::memory_order_relaxed);
    s.ptr_show.store(show, std::memory_order_relaxed);
}

void Overlay::set_counter(const CounterConfig& c) {
    Impl& s = *impl_;
    s.counter_corner.store(std::clamp(c.corner, 0, 3), std::memory_order_relaxed);
    s.counter_detail.store(std::clamp(c.detail, 0, 2), std::memory_order_relaxed);
    s.counter_in_shots.store(c.in_screenshots, std::memory_order_relaxed);
    s.counter_show.store(c.show, std::memory_order_relaxed);   // last: the other settings are in place when it turns on
}

namespace {
bool imgui_wants(std::mutex& mu, ImGuiContext* ctx, bool mouse, bool keyboard) {
    std::lock_guard<std::mutex> lk(mu);
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx);
    const ImGuiIO& io = ImGui::GetIO();
    const bool w = (mouse && io.WantCaptureMouse) || (keyboard && io.WantCaptureKeyboard);
    ImGui::SetCurrentContext(prev);
    return w;
}
}  // namespace

bool Overlay::wants_input() const { return impl_->visible && imgui_wants(impl_->mu, impl_->imgui, true, true); }
bool Overlay::wants_mouse() const { return impl_->visible && imgui_wants(impl_->mu, impl_->imgui, true, false); }
bool Overlay::wants_keyboard() const { return impl_->visible && imgui_wants(impl_->mu, impl_->imgui, false, true); }

void Overlay::add_panel(const std::string& title, std::function<void()> fn) { impl_->panels.push_back({title, std::move(fn)}); }

bool Overlay::handle_event(const input::Event& e) {
    Impl& s = *impl_;
    using input::EventType;
    if ((e.type == EventType::KeyDown || e.type == EventType::KeyUp) && e.u.key.scancode == s.toggle_sc) {
        if (e.type == EventType::KeyDown && !(e.flags & input::kFlagRepeat)) toggle();
        return true;   // the toggle key never reaches the game (press, repeat and release)
    }
    if (!s.visible) return false;
    std::lock_guard<std::mutex> lk(s.mu);
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s.imgui);
    ImGuiIO& io = ImGui::GetIO();
    bool consumed = false;
    switch (e.type) {
        case EventType::KeyDown:
        case EventType::KeyUp: {
            const ImGuiKey k = map_key(e.u.key.scancode);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, e.type == EventType::KeyDown);
            consumed = io.WantCaptureKeyboard;
            break;
        }
        case EventType::Text:
            io.AddInputCharactersUTF8(e.u.text.utf8);
            consumed = io.WantTextInput;
            break;
        case EventType::MouseMotion:
            io.AddMousePosEvent(e.u.motion.x, e.u.motion.y);
            consumed = io.WantCaptureMouse;
            break;
        case EventType::MouseButtonDown:
        case EventType::MouseButtonUp: {
            const int b = e.u.button.button;   // SDL: 1 left, 2 middle, 3 right
            const int ib = b == 1 ? 0 : b == 3 ? 1 : b == 2 ? 2 : b - 1;
            io.AddMousePosEvent(e.u.button.x, e.u.button.y);
            io.AddMouseButtonEvent(ib, e.type == EventType::MouseButtonDown);
            consumed = io.WantCaptureMouse;
            break;
        }
        case EventType::MouseWheel:
            io.AddMouseWheelEvent(e.u.wheel.dx, e.u.wheel.dy);
            consumed = io.WantCaptureMouse;
            break;
        case EventType::WindowFocusLost: io.AddFocusEvent(false); break;
        case EventType::WindowFocusGained: io.AddFocusEvent(true); break;
        default: break;
    }
    ImGui::SetCurrentContext(prev);
    return consumed;
}

void Overlay::apply_pending() {
    apply_present_pending();
    apply_window_pending();
}

void Overlay::apply_present_pending() {
    Impl& s = *impl_;
    if (s.want_mode) {
        s.want_mode = false;
        s.ctx.renderer->set_present_mode(s.new_mode);
    }
}

void Overlay::apply_window_pending() {
    Impl& s = *impl_;
    if (s.want_fullscreen) {
        s.want_fullscreen = false;
        platform::set_fullscreen(s.new_fullscreen);
    }
}

void Overlay::Impl::refresh_counter_text(double now_s) {
    if (now_s - text_s < 0.25) return;    // the text is rebuilt at most every 250 ms
    text_s = now_s;
    snap = stats.snapshot(now_s);
    const int detail = counter_detail.load(std::memory_order_relaxed);
    char buf[64];
    counter_n = 0;
    if (snap.valid) std::snprintf(buf, sizeof buf, "%.0f FPS", snap.fps);
    else std::snprintf(buf, sizeof buf, "-- FPS");
    counter_lines[counter_n++] = buf;
    if (detail >= 1) {
        if (snap.valid) std::snprintf(buf, sizeof buf, "%.1f ms", snap.avg_ms);
        else std::snprintf(buf, sizeof buf, "-- ms");
        counter_lines[counter_n++] = buf;
    }
    if (detail >= 2) {
        if (snap.valid) std::snprintf(buf, sizeof buf, "1%% low %.0f", snap.low1_fps);
        else std::snprintf(buf, sizeof buf, "1%% low --");
        counter_lines[counter_n++] = buf;
        if (snap.valid) std::snprintf(buf, sizeof buf, "max %.1f ms", snap.max_ms);
        else std::snprintf(buf, sizeof buf, "max -- ms");
        counter_lines[counter_n++] = buf;
    }
}

// Draws the counter with the foreground draw list: no window, so it never takes mouse or keyboard input.
void Overlay::Impl::draw_counter(const FrameInfo& f, double now_s) {
    refresh_counter_text(now_s);
    ImFont* font = ImGui::GetFont();
    const float size = std::clamp(std::round(static_cast<float>(f.extent.height) / 45.f), 14.f, 40.f);
    const float pad = size * 0.4f;
    const float gap = size * 0.15f;
    const float margin = size * 0.8f;
    const int n = counter_n;
    float lines_w[4] = {};
    float line_h = 0, text_w = 0;
    for (int i = 0; i < n; ++i) {
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, counter_lines[i].c_str());
        lines_w[i] = sz.x;
        line_h = std::max(line_h, sz.y);
        text_w = std::max(text_w, sz.x);
    }
    const float box_w = text_w + 2.f * pad;
    const float box_h = static_cast<float>(n) * line_h + static_cast<float>(n - 1) * gap + 2.f * pad;
    const int corner = counter_corner.load(std::memory_order_relaxed);
    const bool right = corner == 1 || corner == 3;
    const bool bottom = corner >= 2;
    const float W = static_cast<float>(f.extent.width), H = static_cast<float>(f.extent.height);
    const float x0 = right ? W - margin - box_w : margin;
    const float y0 = bottom ? H - margin - box_h : margin;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + box_w, y0 + box_h), IM_COL32(0, 0, 0, 140), size * 0.3f);
    for (int i = 0; i < n; ++i) {
        const float x = right ? x0 + box_w - pad - lines_w[i] : x0 + pad;
        const float y = y0 + pad + static_cast<float>(i) * (line_h + gap);
        dl->AddText(font, size, ImVec2(x, y), IM_COL32(255, 255, 255, 255), counter_lines[i].c_str());
    }
}

bool Overlay::Impl::note_visible(double now_s) {
    if (!note_read) {
        note_read = true;
        note_enabled = nxrt::config_get_bool("nvn", "compile_note", true);
    }
    const int pending = shader_backlog();
    if (!note_enabled) return false;
    if (pending > 0) {
        if (note_since_s < 0) { note_since_s = now_s; note_peak = 0; }
        note_empty_s = -1.0;
        note_peak = std::max(note_peak, pending);
        return now_s - note_since_s >= 1.5;
    }
    if (note_since_s < 0) return false;
    if (note_empty_s < 0) note_empty_s = now_s;
    if (now_s - note_empty_s >= 1.0) { note_since_s = -1.0; return false; }
    return now_s - note_since_s >= 1.5;
}

void Overlay::Impl::draw_note(const FrameInfo& f) {
    char text[96];
    const int pending = shader_backlog();
    if (pending > 0) std::snprintf(text, sizeof text, "Preparing shaders (%d left)", pending);
    else std::snprintf(text, sizeof text, "Preparing shaders (done)");
    ImFont* font = ImGui::GetFont();
    const float size = std::clamp(std::round(static_cast<float>(f.extent.height) / 60.f), 12.f, 28.f);
    const float pad = size * 0.4f;
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const float W = static_cast<float>(f.extent.width), H = static_cast<float>(f.extent.height);
    const float x0 = (W - sz.x) * 0.5f - pad, y0 = H - size * 0.8f - sz.y - 2.f * pad;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + sz.x + 2.f * pad, y0 + sz.y + 2.f * pad), IM_COL32(0, 0, 0, 120), size * 0.3f);
    dl->AddText(font, size, ImVec2(x0 + pad, y0 + pad), IM_COL32(255, 255, 255, 220), text);
}

void Overlay::draw(const FrameInfo& f) {
    Impl& s = *impl_;
    const bool counter_on = s.counter_show.load(std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    const double now_s = std::chrono::duration<double>(now.time_since_epoch()).count();
    const bool note_on = s.note_visible(now_s) && (s.counter_in_shots.load(std::memory_order_relaxed) || !s.ctx.renderer->capture_pending());
    const bool ptr_on = s.ptr_show.load(std::memory_order_relaxed) && (s.ptr_in_shots.load(std::memory_order_relaxed) || !s.ctx.renderer->capture_pending());
    // the "how to open the settings" hint over the title screen: the first [gfx] menu_hint_s seconds of the run (30; 0 = never), not in screenshots
    static const double hint_s = static_cast<double>(config_get_int("gfx", "menu_hint_s", 30));
    if (s.hint_start_s < 0) s.hint_start_s = now_s;
    const bool screen_on = static_cast<bool>(s.screen);
    const bool hint_on = !screen_on && !s.visible && hint_s > 0 && now_s - s.hint_start_s < hint_s && !s.ctx.renderer->capture_pending();
    if (screen_on) s.hint_start_s = now_s;   // the settings hint counts from when the start menu is gone (the title screen)
    if (!s.visible && !counter_on && !note_on && !ptr_on && !hint_on && !screen_on) {
        s.last_frame = now;
        s.counter_was_on = false;
        return;
    }
    if (counter_on) {
        if (!s.counter_was_on) {          // just switched on: start from an empty history
            s.counter_was_on = true;
            s.counter_have_prev = false;
            s.stats.reset();
            s.text_s = -1e9;
        }
        if (s.counter_have_prev) {
            const double dt_ms = std::chrono::duration<double, std::milli>(now - s.counter_prev).count();
            if (dt_ms <= 1000.0) s.stats.push(now_s, dt_ms);   // over a second: a pause (debugger, window in the background), not a frame
        }
        s.counter_prev = now;
        s.counter_have_prev = true;
    } else {
        s.counter_was_on = false;
    }
    // a screenshot leaves the counter out unless asked for; with the menu hidden there is then nothing to draw
    const bool show_counter = counter_on && (s.counter_in_shots.load(std::memory_order_relaxed) || !s.ctx.renderer->capture_pending());
    if (!s.visible && !show_counter && !note_on && !ptr_on && !hint_on && !screen_on) {
        s.last_frame = now;
        return;
    }
    const bool hdr_frame = s.hdr && f.hdr_output;
    if (s.hdr && !f.hdr_output) return;      // the display left HDR mode: the RGBA8 overlay pipeline does not match the SDR swapchain, so no overlay until the next start
    if (hdr_frame && !s.composite.ensure_target(f.extent)) return;
    std::lock_guard<std::mutex> lk(s.mu);
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = std::clamp(std::chrono::duration<float>(now - s.last_frame).count(), 1.0f / 1000.f, 0.25f);
    s.last_frame = now;
    io.DisplaySize = ImVec2(static_cast<float>(f.extent.width), static_cast<float>(f.extent.height));

    s.update_scale(f.extent);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (screen_on) s.screen();
    else if (s.visible) s.build_ui();
    if (show_counter) s.draw_counter(f, now_s);
    if (note_on) s.draw_note(f);
    if (hint_on) {   // a rounded ink-teal label, bottom left, fading out over its last two seconds
        const float k = std::max(1.f, static_cast<float>(f.extent.height) / 720.f);
        const float left = static_cast<float>(hint_s - (now_s - s.hint_start_s));
        const float a = std::clamp(left / 2.0f, 0.0f, 1.0f);
        const char* text = "PC settings:  View + Menu  (or F1)";
        ImFont* font = ImGui::GetFont();
        const float size = 20.0f * k;
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        const ImVec2 p0(24 * k, static_cast<float>(f.extent.height) - 24 * k - ts.y - 20 * k);
        const ImVec2 p1(p0.x + ts.x + 32 * k, p0.y + ts.y + 20 * k);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(16, 58, 56, static_cast<int>(230 * a)), 14 * k);
        dl->AddRect(p0, p1, IM_COL32(206, 238, 40, static_cast<int>(255 * a)), 14 * k, 0, 2.5f * k);
        dl->AddText(font, size, ImVec2(p0.x + 16 * k, p0.y + 10 * k), IM_COL32(244, 238, 218, static_cast<int>(255 * a)), text);
    }
    if (ptr_on) {   // a plain arrow, white with a dark outline, sized with the frame height
        const float k = std::max(1.f, static_cast<float>(f.extent.height) / 720.f);
        const float x = s.ptr_x.load(std::memory_order_relaxed), y = s.ptr_y.load(std::memory_order_relaxed);
        const ImVec2 pts[7] = {{0, 0}, {0, 16 * k}, {4 * k, 12.5f * k}, {7.5f * k, 19 * k}, {10 * k, 18 * k}, {6.5f * k, 11.5f * k}, {12 * k, 11.5f * k}};
        ImVec2 poly[7];
        for (int i = 0; i < 7; ++i) poly[i] = ImVec2(x + pts[i].x, y + pts[i].y);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddConvexPolyFilled(poly, 7, IM_COL32(255, 255, 255, 255));
        dl->AddPolyline(poly, 7, IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 1.5f * k);
    }
    ImGui::Render();

    // one dynamic-rendering pass that draws the ImGui draw data into view
    auto render_into = [&](VkImageView view, VkAttachmentLoadOp load) {
        VkRenderingAttachmentInfo att = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        att.imageView = view;
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = load;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, f.extent};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &att;
        vkCmdBeginRendering(f.cmd, &ri);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.cmd);
        vkCmdEndRendering(f.cmd);
    };
    if (hdr_frame) {
        // HDR: the overlay goes into the cleared RGBA8 target, then the composite maps it onto the swapchain image (UI white = paper white)
        const extras::HdrState hs = extras::hdr_state();
        s.composite.begin_overlay(f.cmd);
        render_into(s.composite.target_view(), VK_ATTACHMENT_LOAD_OP_CLEAR);
        s.composite.composite(f.cmd, f.view, f.format, f.extent, s.ctx.renderer->output_kind(), hs.paper_white_nits, hs.peak_nits);
    } else {
        render_into(f.view, VK_ATTACHMENT_LOAD_OP_LOAD);
    }
    s.drawn++;
    ImGui::SetCurrentContext(prev);
}

void Overlay::install_on_renderer() {
    impl_->ctx.renderer->set_overlay_hook([this](const FrameInfo& f) { draw(f); });
}

}  // namespace nxrt::gfx
