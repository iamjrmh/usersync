#include "Gui.h"

#include "FileDialog.h"
#include "GpuInfo.h"
#include "ModelDownloader.h"
#include "SplitRunner.h"
#include "WhisperXRunner.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

namespace usersync {

namespace {

// ---------- userinfo.lol brand palette (pink -> purple ramp) ----------
// Matches the website's --gradient linear-gradient(135deg, #e879f9, #a855f7).
// The 5-stop interpolator gives smoother in-between colors than 2 stops.
constexpr ImU32 IG_STOP_0 = IM_COL32(0xF0, 0xAB, 0xFC, 0xFF); // pink-light
constexpr ImU32 IG_STOP_1 = IM_COL32(0xE8, 0x79, 0xF9, 0xFF); // pink
constexpr ImU32 IG_STOP_2 = IM_COL32(0xD9, 0x46, 0xEF, 0xFF); // pink-dark
constexpr ImU32 IG_STOP_3 = IM_COL32(0xA8, 0x55, 0xF7, 0xFF); // purple
constexpr ImU32 IG_STOP_4 = IM_COL32(0x7C, 0x3A, 0xED, 0xFF); // purple-dark

ImU32 ig_lerp_stops(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const ImU32 stops[5] = { IG_STOP_0, IG_STOP_1, IG_STOP_2, IG_STOP_3, IG_STOP_4 };
    const float scaled = t * 4.0f;
    const int   i      = std::min(3, static_cast<int>(scaled));
    const float f      = scaled - i;
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(stops[i]);
    const ImVec4 b = ImGui::ColorConvertU32ToFloat4(stops[i + 1]);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        a.x + (b.x - a.x) * f,
        a.y + (b.y - a.y) * f,
        a.z + (b.z - a.z) * f,
        1.0f));
}

void fill_ig_gradient(ImDrawList* dl, ImVec2 a, ImVec2 b, int slices = 24) {
    for (int i = 0; i < slices; ++i) {
        const float t0 = i / static_cast<float>(slices);
        const float t1 = (i + 1) / static_cast<float>(slices);
        const float x0 = a.x + (b.x - a.x) * t0;
        const float x1 = a.x + (b.x - a.x) * t1;
        const ImU32 c0 = ig_lerp_stops(t0);
        const ImU32 c1 = ig_lerp_stops(t1);
        dl->AddRectFilledMultiColor(ImVec2(x0, a.y), ImVec2(x1, b.y),
                                    c0, c1, c1, c0);
    }
}

// Primary CTA: gradient fill, white text, AE-style soft hover lift.
bool gradient_button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushID(label);
    ImGuiStyle& style = ImGui::GetStyle();

    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x <= 0) size.x = label_size.x + style.FramePadding.x * 4.0f;
    if (size.y <= 0) size.y = label_size.y + style.FramePadding.y * 2.0f;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##gradbtn", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(pos.x + size.x, pos.y + size.y);

    // Slight darken on press / brighten on hover via overlay rect.
    fill_ig_gradient(dl, pos, br);
    if (hovered) {
        dl->AddRectFilled(pos, br,
                          IM_COL32(255, 255, 255, active ? 12 : 28),
                          style.FrameRounding);
    }
    dl->AddRect(pos, br, IM_COL32(255, 255, 255, 40), style.FrameRounding, 0, 1.0f);

    const ImVec2 text_pos(
        pos.x + (size.x - label_size.x) * 0.5f,
        pos.y + (size.y - label_size.y) * 0.5f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos,
                IM_COL32(255, 255, 255, 255), label);

    ImGui::PopID();
    return clicked;
}

void gradient_progress_bar(float fraction, ImVec2 size, const char* overlay = nullptr) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = ImGui::GetFrameHeight();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, br, IM_COL32(28, 28, 30, 255), 4.0f);

    if (fraction > 0.0f) {
        const ImVec2 fill_br(pos.x + size.x * fraction, br.y);
        fill_ig_gradient(dl, pos, fill_br);
    }
    dl->AddRect(pos, br, IM_COL32(255, 255, 255, 30), 4.0f, 0, 1.0f);

    if (overlay) {
        const ImVec2 sz = ImGui::CalcTextSize(overlay);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(pos.x + (size.x - sz.x) * 0.5f,
                           pos.y + (size.y - sz.y) * 0.5f),
                    IM_COL32_WHITE, overlay);
    }
    ImGui::Dummy(size);
}

const char* job_state_label(JobState s) {
    switch (s) {
        case JobState::Idle:      return "Idle";
        case JobState::Running:   return "Running";
        case JobState::Done:      return "Done";
        case JobState::Error:     return "Error";
        case JobState::Cancelled: return "Cancelled";
    }
    return "?";
}

std::string default_lrc_path_for(const std::string& audio) {
    std::filesystem::path p(audio);
    p.replace_extension(".lrc");
    return p.string();
}

// Standard `(?)` help-marker idiom from the ImGui demo, with wrapped tooltip
// text so long explanations don't run off the screen.
void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Section header: small gradient stripe + uppercase label + thin separator.
// Tight vertical chrome so sections don't push content around.
void section_header(const char* label) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  h = ImGui::GetTextLineHeight();
    fill_ig_gradient(dl, ImVec2(p.x, p.y + 1.0f),
                     ImVec2(p.x + 3.0f, p.y + h - 1.0f));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 10.0f, p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 222, 255));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

// Begin a form row: label on the left at a fixed column, widget(s) fill the
// rest. The caller is responsible for the actual widget(s) on the right.
void form_row(const char* label, float label_col_w = 110.0f) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_col_w);
}

// Slider with label on the left and (?) help marker on the right.
void form_slider(const char* label, int* v, int lo, int hi, const char* tip) {
    ImGui::PushID(label);
    form_row(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x
                            - ImGui::CalcTextSize("(?)").x
                            - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SliderInt("##v", v, lo, hi);
    help_marker(tip);
    ImGui::PopID();
}

void form_text(const char* label, char* buf, size_t buf_size, const char* tip = nullptr) {
    ImGui::PushID(label);
    form_row(label);
    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x
        - (tip ? ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x : 0.0f));
    ImGui::InputText("##v", buf, buf_size);
    if (tip) help_marker(tip);
    ImGui::PopID();
}

// Form row: label on the left, InputText + Browse button on the right.
// Returns true if the user edited the field or picked a file.
bool form_browse(const char* label, char* buf, size_t buf_size,
                 const char* btn_label,
                 const std::function<bool(std::string&)>& browse) {
    ImGui::PushID(label);
    form_row(label);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float spacing = st.ItemSpacing.x;
    const float btn_w   = ImGui::CalcTextSize(btn_label).x + st.FramePadding.x * 2.0f;
    const float avail   = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(60.0f, avail - btn_w - spacing));
    const bool edited = ImGui::InputText("##v", buf, buf_size);
    ImGui::SameLine(0.0f, spacing);
    bool browsed = false;
    if (ImGui::Button(btn_label)) {
        std::string picked;
        if (browse && browse(picked) && !picked.empty()) {
            const size_t n = std::min(picked.size(), buf_size - 1);
            std::memcpy(buf, picked.data(), n);
            buf[n] = '\0';
            browsed = true;
        }
    }
    ImGui::PopID();
    return edited || browsed;
}

// Preset: a one-click bundle of the model + quality knobs. `models` is a
// preference-ordered list of files; apply_preset picks the first one that
// actually exists in the models/ folder so the preset still does something
// useful even if you don't have the "ideal" weights downloaded.
struct Preset {
    const char* label;
    const char* models[6];   // nullptr-terminated
    int         beam;
    int         best_of;
    int         refine;
    int         iter;        // per-line forced-alignment iterations
    const char* tooltip;
};

constexpr Preset PRESETS[] = {
    // All presets use ggml-large-v3.bin -- empirically the only model that
    // produces good lyric sync. Presets now differ ONLY in how many passes
    // they run, not in which model.
    {"Lazy",    {"models/ggml-large-v3.bin", nullptr}, 1, 1, 1,  0,
     "Single greedy pass. Fast-ish. Use this to verify the pipeline runs."},

    {"Low",     {"models/ggml-large-v3.bin", nullptr}, 1, 1, 1,  0,
     "Single pass with greedy decoding."},

    // Iterative passes are SAFE to enable now -- IterativeAligner refuses
    // to commit a pass whose match ratio < 60% or average word drift > 1.5s.
    // Worst case the iter passes do nothing; best case they tighten timing.

    {"Medium",  {"models/ggml-large-v3.bin", nullptr}, 1, 3, 1,  0,
     "Greedy with retries on confused segments. Quickest sensible default."},

    {"High",    {"models/ggml-large-v3.bin", nullptr}, 5, 5, 2,  3,
     "Beam 5 + 2 window refinements + 3 per-line iterative passes "
     "(when you paste lyrics). ~5x slower than Medium."},

    {"Perfect", {"models/ggml-large-v3.bin", nullptr}, 5, 5, 4,  8,
     "Beam 5, 4 window refinements, 8 per-line iterative passes. Each iter "
     "pass shrinks the per-line audio window so it converges. Safety guards "
     "reject bad passes."},

    {"MAX",     {"models/ggml-large-v3.bin", nullptr}, 8, 8, 6, 15,
     "EVERY KNOB TO 11. Beam 8, best-of 8, 6 window refinements, 15 per-line "
     "iterative passes.\n\n"
     "Will spend a long time per song (10-30 min on RTX 3070 for a 3-minute "
     "track). Pair with pasted lyrics for forced alignment. Safety guards "
     "in the iterative aligner reject passes that would make timing worse, "
     "so cranking iter higher only ever helps or no-ops."},
};

// Smaller variant of the gradient button used in the preset bar.
bool gradient_pill(const char* label, ImVec2 size) {
    ImGui::PushID(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    // InvisibleButton's return value is the standard "clicked" semantic
    // (fires on release, ignored if the cursor drags off mid-press).
    // IsItemClicked() instead fires on mouse-DOWN which sometimes misses
    // legitimate clicks; use the button's own return for reliability.
    const bool clicked = ImGui::InvisibleButton("##pill", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(pos.x + size.x, pos.y + size.y);

    if (hovered) {
        fill_ig_gradient(dl, pos, br);
        dl->AddRectFilled(pos, br,
                          IM_COL32(255, 255, 255, active ? 18 : 32),
                          ImGui::GetStyle().FrameRounding);
    } else {
        dl->AddRectFilled(pos, br, IM_COL32(50, 50, 54, 255),
                          ImGui::GetStyle().FrameRounding);
    }
    dl->AddRect(pos, br,
                hovered ? IM_COL32(255, 255, 255, 70) : IM_COL32(255, 255, 255, 25),
                ImGui::GetStyle().FrameRounding, 0, 1.0f);

    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(pos.x + (size.x - ts.x) * 0.5f,
                       pos.y + (size.y - ts.y) * 0.5f),
                IM_COL32_WHITE, label);
    ImGui::PopID();
    return clicked;
}

}  // namespace

// ---------------------------------------------------------------------------

Gui::Gui(App& app) : app_(app) {}

void Gui::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "usersync_imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    apply_style();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    scan_models();  // populate the Model dropdown from models/ folder
}

void Gui::scan_models() {
    namespace fs = std::filesystem;
    const std::string previous = (selected_model_idx_ >= 0
                                  && selected_model_idx_ < (int)available_models_.size())
                                  ? available_models_[selected_model_idx_]
                                  : std::string(model_path_buf_);

    available_models_.clear();
    selected_model_idx_ = -1;

    std::error_code ec;
    const fs::path dir = "models";
    if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".bin") continue;
            available_models_.push_back("models/" + entry.path().filename().string());
        }
        std::sort(available_models_.begin(), available_models_.end());
    }

    // Try to keep the previously-selected model selected after a refresh.
    for (size_t i = 0; i < available_models_.size(); ++i) {
        if (available_models_[i] == previous) { selected_model_idx_ = (int)i; break; }
    }
    if (selected_model_idx_ == -1 && !available_models_.empty()) selected_model_idx_ = 0;

    if (selected_model_idx_ >= 0) {
        std::snprintf(model_path_buf_, sizeof(model_path_buf_), "%s",
                      available_models_[selected_model_idx_].c_str());
    }
}

void Gui::sync_player_to_audio_path() {
    if (audio_path_buf_[0] == 0) return;
    if (player_.is_loaded() && player_.loaded_path() == audio_path_buf_) return;
    player_load_error_.clear();
    if (!player_.load(audio_path_buf_, player_load_error_)) {
        // Keep the error around so the Sync tab can show it.
    }
}

void Gui::shutdown() {
    // Stop the split worker before tearing down ImGui so the worker
    // thread can't still be calling into app_.append_log() etc.
    split_cancel_.store(true);
    if (split_worker_.joinable()) split_worker_.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Gui::apply_style() {
    // userinfo.lol theme: very dark purple-black base, pink-tinted translucent
    // surfaces, pink/purple accents. Tightened spacing for a denser,
    // more professional feel than the previous over-padded layout.
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 10.0f;
    s.ChildRounding     = 10.0f;
    s.FrameRounding     = 8.0f;
    s.GrabRounding      = 999.0f;
    s.TabRounding       = 8.0f;
    s.PopupRounding     = 10.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowPadding     = ImVec2(12, 10);
    s.FramePadding      = ImVec2(10, 5);
    s.ItemSpacing       = ImVec2(8, 5);
    s.ItemInnerSpacing  = ImVec2(6, 4);
    s.IndentSpacing     = 14.0f;
    s.GrabMinSize       = 14.0f;
    s.ScrollbarSize     = 11.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.CellPadding       = ImVec2(8, 4);

    ImVec4* c = s.Colors;
    auto rgb = [](int r, int g, int b, int a = 255) {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    };

    // userinfo.lol CSS palette:
    //   --pink-light: #f0abfc   --pink: #e879f9     --pink-dark: #d946ef
    //   --purple:     #a855f7   --purple-dark: #7c3aed
    //   --bg:  #0d0015          --bg2: #110018      --bg3: #16001f
    //   --card-border: rgba(217,70,239,0.18)  (= #d946ef @ alpha 46)
    const ImVec4 PINK_LIGHT   = rgb(0xF0, 0xAB, 0xFC);
    const ImVec4 PINK         = rgb(0xE8, 0x79, 0xF9);
    const ImVec4 PINK_DARK    = rgb(0xD9, 0x46, 0xEF);
    const ImVec4 PURPLE       = rgb(0xA8, 0x55, 0xF7);
    const ImVec4 BG           = rgb(0x0D, 0x00, 0x15);
    const ImVec4 BG2          = rgb(0x11, 0x00, 0x18);
    const ImVec4 BG3          = rgb(0x16, 0x00, 0x1F);

    c[ImGuiCol_Text]                 = rgb(255, 255, 255);
    c[ImGuiCol_TextDisabled]         = rgb(255, 255, 255, 102);  // ~0.40 alpha
    c[ImGuiCol_WindowBg]             = BG;
    c[ImGuiCol_ChildBg]              = BG2;
    c[ImGuiCol_PopupBg]              = BG;
    c[ImGuiCol_Border]               = rgb(0xD9, 0x46, 0xEF, 46);  // 0.18 alpha
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = BG3;
    c[ImGuiCol_FrameBgHovered]       = rgb(0xD9, 0x46, 0xEF, 26);  // 0.10 alpha
    c[ImGuiCol_FrameBgActive]        = rgb(0xD9, 0x46, 0xEF, 51);  // 0.20 alpha

    c[ImGuiCol_TitleBg]              = BG;
    c[ImGuiCol_TitleBgActive]        = BG2;
    c[ImGuiCol_TitleBgCollapsed]     = BG;
    c[ImGuiCol_MenuBarBg]            = BG;

    c[ImGuiCol_ScrollbarBg]          = BG;
    c[ImGuiCol_ScrollbarGrab]        = rgb(0xD9, 0x46, 0xEF, 76);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0xD9, 0x46, 0xEF, 128);
    c[ImGuiCol_ScrollbarGrabActive]  = rgb(0xD9, 0x46, 0xEF, 200);

    c[ImGuiCol_CheckMark]            = PINK;
    c[ImGuiCol_SliderGrab]           = PINK_DARK;
    c[ImGuiCol_SliderGrabActive]     = PURPLE;

    c[ImGuiCol_Button]               = rgb(0xD9, 0x46, 0xEF, 26);  // 0.10 alpha pink tint
    c[ImGuiCol_ButtonHovered]        = rgb(0xD9, 0x46, 0xEF, 64);  // 0.25 alpha
    c[ImGuiCol_ButtonActive]         = PINK_DARK;

    c[ImGuiCol_Header]               = rgb(0xD9, 0x46, 0xEF, 26);
    c[ImGuiCol_HeaderHovered]        = rgb(0xD9, 0x46, 0xEF, 64);
    c[ImGuiCol_HeaderActive]         = PINK_DARK;

    c[ImGuiCol_Separator]            = rgb(0xD9, 0x46, 0xEF, 46);
    c[ImGuiCol_SeparatorHovered]     = PINK;
    c[ImGuiCol_SeparatorActive]      = PURPLE;

    c[ImGuiCol_ResizeGrip]           = rgb(0xD9, 0x46, 0xEF, 64);
    c[ImGuiCol_ResizeGripHovered]    = PINK;
    c[ImGuiCol_ResizeGripActive]     = PINK_DARK;

    c[ImGuiCol_Tab]                  = rgb(0xD9, 0x46, 0xEF, 26);
    c[ImGuiCol_TabHovered]           = rgb(0xD9, 0x46, 0xEF, 102);
    c[ImGuiCol_TabActive]            = PINK_DARK;
    c[ImGuiCol_TabUnfocused]         = rgb(0xD9, 0x46, 0xEF, 13);
    c[ImGuiCol_TabUnfocusedActive]   = rgb(0xD9, 0x46, 0xEF, 51);

    c[ImGuiCol_PlotHistogram]        = PINK;
    c[ImGuiCol_PlotHistogramHovered] = PINK_LIGHT;
    c[ImGuiCol_TextSelectedBg]       = rgb(0xD9, 0x46, 0xEF, 100);
}

void Gui::draw() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##usersync_root", nullptr, root_flags);
    ImGui::PopStyleVar(3);

    ImGui::SetWindowFontScale(1.08f);
    poll_log();

    // Global hotkey: spacebar toggles audio playback regardless of which
    // tab you're in. Suppressed while any text-input widget has focus so
    // typing a space into the lyrics box doesn't pause the song.
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool typing = io.WantTextInput;
        if (!typing && ImGui::IsKeyPressed(ImGuiKey_Space, false)
                    && player_.is_loaded()) {
            player_.toggle();
        }
    }

    draw_title_bar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild("##body_pad", ImVec2(0, 0), false);
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.08f);

    draw_preset_bar();

    // Reserve the bottom strip for the footer.
    const float footer_h = 96.0f;
    const float body_h   = std::max(160.0f,
        ImGui::GetContentRegionAvail().y - footer_h - ImGui::GetStyle().ItemSpacing.y);

    // One full-width tab bar so each section gets the whole window instead
    // of fighting for half of it.
    ImGui::BeginChild("##body", ImVec2(0, body_h), false);
    ImGui::SetWindowFontScale(1.08f);
    if (ImGui::BeginTabBar("##main_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Setup"))    { draw_setup_tab();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Split"))    { draw_split_tab();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Quality"))  { draw_quality_tab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Lyrics"))   { draw_lyrics_tab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Preview"))  { draw_preview_tab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Editor"))   { draw_editor_tab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Output"))   { draw_output_tab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Models"))   { draw_models_tab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Log"))      { draw_log_tab();     ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    draw_footer();

    ImGui::EndChild();
    ImGui::End();
}

void Gui::apply_preset(int index) {
    if (index < 0 || index >= static_cast<int>(sizeof(PRESETS) / sizeof(PRESETS[0]))) return;
    const Preset& p = PRESETS[index];

    // Quality knobs always apply, even if no model in the fallback chain
    // is installed.
    beam_size_              = p.beam;
    best_of_                = p.best_of;
    refine_passes_          = p.refine;
    iterative_align_passes_ = p.iter;
    threads_                = 0;     // 0 == auto, the sensible default
    use_gpu_                = true;  // assume the user wants GPU if they picked a preset

    auto select = [&](const std::string& path) {
        for (size_t i = 0; i < available_models_.size(); ++i) {
            if (available_models_[i] == path) {
                selected_model_idx_ = static_cast<int>(i);
                std::snprintf(model_path_buf_, sizeof(model_path_buf_), "%s", path.c_str());
                return true;
            }
        }
        return false;
    };

    // The MAX preset auto-picks by GPU VRAM. Fall through to the standard
    // preference-list walk if VRAM lookup failed or no matching model is
    // installed.
    if (std::strcmp(p.label, "MAX") == 0) {
        const auto best = best_model_for_vram(primary_gpu_vram_bytes(),
                                              available_models_);
        if (!best.empty() && select(best)) return;
    }

    // Walk the preset's preference list and pick the first model that's
    // actually installed in models/.
    for (const char* desired : p.models) {
        if (!desired) break;
        if (select(desired)) return;
    }
}

void Gui::draw_preset_bar() {
    ImGui::BeginChild("preset", ImVec2(0, 70.0f), true);
    ImGui::SetWindowFontScale(1.08f);
    section_header("QUALITY PRESET");

    const int    n       = static_cast<int>(sizeof(PRESETS) / sizeof(PRESETS[0]));
    const float  spacing = ImGui::GetStyle().ItemSpacing.x;
    const float  avail   = ImGui::GetContentRegionAvail().x;
    const float  btn_w   = std::max(70.0f, (avail - spacing * (n - 1)) / n);

    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0.0f, spacing);
        if (gradient_pill(PRESETS[i].label, ImVec2(btn_w, 28.0f))) apply_preset(i);
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::TextUnformatted(PRESETS[i].tooltip);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
}

void Gui::draw_title_bar() {
    const float h = 56.0f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + ImGui::GetContentRegionAvail().x, a.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    fill_ig_gradient(dl, a, b);

    // Subtle inner shadow on the bottom edge of the banner for depth.
    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - 12.0f), b,
                                IM_COL32(0, 0, 0, 0),  IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 60));

    ImGui::SetCursorScreenPos(ImVec2(a.x + 18.0f, a.y + 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
    ImGui::SetWindowFontScale(1.65f);
    ImGui::Text("usersync");
    ImGui::SetWindowFontScale(0.95f);
    ImGui::SetCursorScreenPos(ImVec2(a.x + 18.0f, a.y + 32.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 200));
    ImGui::TextUnformatted("word-accurate .lrc generator");
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.08f);

    // Right-side caption.
    const char* tag = "whisper.cpp  ::  iterative alignment";
    const ImVec2 ts = ImGui::CalcTextSize(tag);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(b.x - ts.x - 22.0f, a.y + (h - ts.y) * 0.5f),
                IM_COL32(255, 255, 255, 200), tag);

    // Hairline below the banner.
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y),
                IM_COL32(10, 10, 12, 255), 1.0f);

    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
    ImGui::Dummy(ImVec2(0, 2.0f));
}

// ---------------------------------------------------------------------------
// Log helper
// ---------------------------------------------------------------------------
namespace {
std::string format_time_now() {
    const auto now = std::chrono::system_clock::now();
    const auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}
}  // namespace

void Gui::poll_log() {
    const auto st = app_.status();
    auto push = [&](const std::string& level, const std::string& text) {
        log_.push_back({ format_time_now(), level, text });
        const size_t kMax = 5000;  // big enough to keep a whole run's output
        if (log_.size() > kMax) {
            log_.erase(log_.begin(), log_.begin() + (log_.size() - kMax));
        }
    };
    if (st.phase != log_last_phase_) {
        if (!st.phase.empty()) push("info", "phase -> " + st.phase);
        log_last_phase_ = st.phase;
    }
    if (st.error != log_last_error_ && !st.error.empty()) {
        push("error", st.error);
        log_last_error_ = st.error;
    }

    // Drain ALL queued messages so nothing fired between frames is lost.
    auto pending = app_.drain_log_buffer();
    for (auto& m : pending) {
        std::string lvl = "info";
        if (m.rfind("[WARN]", 0) == 0 || m.rfind("[WARN ]", 0) == 0) lvl = "warn";
        else if (m.rfind("[ERR", 0) == 0)                              lvl = "error";
        else if (m.rfind("[SEG]", 0) == 0)                             lvl = "seg";
        else if (m.rfind("[WORD]", 0) == 0)                            lvl = "word";
        push(lvl, m);
    }
}

// ---------------------------------------------------------------------------
// Editor helpers
// ---------------------------------------------------------------------------
void Gui::editor_pull_from_app() {
    editor_lines_         = app_.snapshot_lines();
    editor_line_expanded_.assign(editor_lines_.size(), true);  // default: expanded
    editor_loaded_seq_    = static_cast<int>(editor_lines_.size());
    if (editor_out_path_[0] == 0) {
        const auto p = app_.last_lrc_path();
        const std::string src = !p.empty() ? p : std::string{out_path_buf_};
        std::snprintf(editor_out_path_, sizeof(editor_out_path_), "%s", src.c_str());
    }
}

bool Gui::editor_save_to_disk(std::string& err) {
    if (editor_lines_.empty())     { err = "Editor has no lines -- pull from job first."; return false; }
    if (editor_out_path_[0] == 0)  { err = "Set an output .lrc path first."; return false; }

    // Work on a copy so the on-screen editor data isn't disturbed.
    std::vector<Line> out = editor_lines_;

    // Pass 1: within each line, every word's end = next word's start
    // (no within-line gaps). Last word keeps its original end.
    for (auto& line : out) {
        for (size_t i = 0; i + 1 < line.words.size(); ++i) {
            line.words[i].t_end = line.words[i + 1].t_start;
        }
        if (!line.words.empty()) {
            line.t_start = line.words.front().t_start;
            line.t_end   = line.words.back().t_end;
        }
    }

    // Pass 2: between adjacent lines, clamp current line's last word end
    // to next line's first word start (so lines never overlap).
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i].words.empty() || out[i + 1].words.empty()) continue;
        const double next_start = out[i + 1].words.front().t_start;
        if (out[i].words.back().t_end > next_start) {
            out[i].words.back().t_end = next_start;
            out[i].t_end = next_start;
        }
    }

    // Pass 3: insert [MUSIC] marker lines wherever there's a >30s gap
    // between lines. Helps players show instrumental sections cleanly.
    constexpr double kMusicGapSeconds = 30.0;
    std::vector<Line> with_music;
    with_music.reserve(out.size() + 8);
    for (size_t i = 0; i < out.size(); ++i) {
        with_music.push_back(out[i]);
        if (i + 1 >= out.size()) continue;
        if (out[i].words.empty() || out[i + 1].words.empty()) continue;
        const double cur_end    = out[i].words.back().t_end;
        const double next_start = out[i + 1].words.front().t_start;
        const double gap = next_start - cur_end;
        if (gap > kMusicGapSeconds) {
            Line m;
            m.t_start = cur_end + 0.1;
            m.t_end   = next_start - 0.1;
            Word w;
            w.text    = "[MUSIC]";
            w.t_start = m.t_start;
            w.t_end   = m.t_end;
            w.prob    = 1.0f;
            m.words.push_back(std::move(w));
            with_music.push_back(std::move(m));
        }
    }

    LrcMetadata meta;
    meta.title  = title_buf_;
    meta.artist = artist_buf_;
    meta.album  = album_buf_;
    const std::string lrc = format_enhanced_lrc(with_music, meta);
    return write_file(editor_out_path_, lrc, err);
}

void Gui::editor_play_at(double t_sec) {
    sync_player_to_audio_path();
    if (!player_.is_loaded()) return;
    player_.seek_seconds(std::max(0.0, t_sec - 0.3));
    player_.play();
}

// ---------------------------------------------------------------------------
// Project save/load
// ---------------------------------------------------------------------------
// File format (.uspj, plain UTF-8):
//   USERSYNC_PROJECT v1
//   key=value lines...
//   <<<lyrics
//   ...verbatim user lyrics, any number of lines...
//   >>>
//   <<<edited_lines
//   L <t_start> <t_end>
//   W <t_start> <t_end> <prob> <word text>
//   ...
//   >>>
//
// Lyrics and edited_lines blocks are optional. Word text gets the rest
// of the line so spaces inside a token are preserved.

bool Gui::save_project(const std::string& path, std::string& err) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) { err = "could not open for write: " + path; return false; }

    auto kv = [&](const char* k, const std::string& v) {
        f << k << '=' << v << '\n';
    };

    f << "USERSYNC_PROJECT v1\n";
    kv("audio",                  audio_path_buf_);
    kv("out",                    out_path_buf_);
    kv("model",                  model_path_buf_);
    kv("language",               language_buf_);
    kv("title",                  title_buf_);
    kv("artist",                 artist_buf_);
    kv("album",                  album_buf_);
    kv("use_gpu",                use_gpu_       ? "1" : "0");
    kv("translate",              translate_     ? "1" : "0");
    kv("use_whisperx",           use_whisperx_  ? "1" : "0");
    kv("threads",                std::to_string(threads_));
    kv("beam_size",              std::to_string(beam_size_));
    kv("best_of",                std::to_string(best_of_));
    kv("refine_passes",          std::to_string(refine_passes_));
    kv("iterative_align_passes", std::to_string(iterative_align_passes_));

    f << "<<<lyrics\n";
    if (!user_lyrics_.empty()) {
        f << user_lyrics_;
        if (user_lyrics_.back() != '\n') f << '\n';
    }
    f << ">>>\n";

    // Prefer the editor's edited lines; fall back to the app's current
    // alignment so reopening a project still picks up the most recent
    // post-run state even if the user hadn't touched the Editor tab.
    const std::vector<Line>& lines =
        !editor_lines_.empty() ? editor_lines_ : app_.snapshot_lines();
    f << "<<<edited_lines\n";
    f << std::fixed;
    f.precision(6);
    for (const auto& l : lines) {
        f << "L " << l.t_start << ' ' << l.t_end << '\n';
        for (const auto& w : l.words) {
            f << "W " << w.t_start << ' ' << w.t_end << ' '
              << w.prob << ' ' << w.text << '\n';
        }
    }
    f << ">>>\n";

    if (!f.good()) { err = "write failed: " + path; return false; }
    return true;
}

bool Gui::load_project(const std::string& path, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "could not open: " + path; return false; }

    std::string header;
    if (!std::getline(f, header) || header.rfind("USERSYNC_PROJECT", 0) != 0) {
        err = "not a usersync project file";
        return false;
    }

    auto copy_to_buf = [](char* dst, size_t cap, const std::string& s) {
        const size_t n = std::min(s.size(), cap - 1);
        std::memcpy(dst, s.data(), n);
        dst[n] = '\0';
    };
    auto trim_cr = [](std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    };

    std::string new_lyrics;
    std::vector<Line> new_lines;

    std::string line;
    while (std::getline(f, line)) {
        trim_cr(line);

        if (line == "<<<lyrics") {
            while (std::getline(f, line)) {
                trim_cr(line);
                if (line == ">>>") break;
                if (!new_lyrics.empty()) new_lyrics += '\n';
                new_lyrics += line;
            }
            continue;
        }

        if (line == "<<<edited_lines") {
            while (std::getline(f, line)) {
                trim_cr(line);
                if (line == ">>>") break;
                if (line.empty()) continue;
                std::istringstream is(line);
                std::string tag;
                is >> tag;
                if (tag == "L") {
                    new_lines.emplace_back();
                    is >> new_lines.back().t_start >> new_lines.back().t_end;
                } else if (tag == "W") {
                    if (new_lines.empty()) new_lines.emplace_back();
                    Word w;
                    is >> w.t_start >> w.t_end >> w.prob;
                    std::string text;
                    std::getline(is, text);
                    if (!text.empty() && text.front() == ' ') text.erase(text.begin());
                    w.text = std::move(text);
                    new_lines.back().words.push_back(std::move(w));
                }
            }
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);

        if      (k == "audio")                  copy_to_buf(audio_path_buf_, sizeof(audio_path_buf_), v);
        else if (k == "out")                    copy_to_buf(out_path_buf_,   sizeof(out_path_buf_),   v);
        else if (k == "model")                  copy_to_buf(model_path_buf_, sizeof(model_path_buf_), v);
        else if (k == "language")               copy_to_buf(language_buf_,   sizeof(language_buf_),   v);
        else if (k == "title")                  copy_to_buf(title_buf_,      sizeof(title_buf_),      v);
        else if (k == "artist")                 copy_to_buf(artist_buf_,     sizeof(artist_buf_),     v);
        else if (k == "album")                  copy_to_buf(album_buf_,      sizeof(album_buf_),      v);
        else if (k == "use_gpu")                use_gpu_       = (v == "1");
        else if (k == "translate")              translate_     = (v == "1");
        else if (k == "use_whisperx")           use_whisperx_  = (v == "1");
        else if (k == "threads")                threads_                = std::atoi(v.c_str());
        else if (k == "beam_size")              beam_size_              = std::atoi(v.c_str());
        else if (k == "best_of")                best_of_                = std::atoi(v.c_str());
        else if (k == "refine_passes")          refine_passes_          = std::atoi(v.c_str());
        else if (k == "iterative_align_passes") iterative_align_passes_ = std::atoi(v.c_str());
    }

    user_lyrics_ = std::move(new_lyrics);
    if (!new_lines.empty()) {
        editor_lines_         = std::move(new_lines);
        editor_line_expanded_.assign(editor_lines_.size(), true);
        editor_loaded_seq_    = static_cast<int>(editor_lines_.size());
        // Mirror into the app so Preview + Output tabs reflect the loaded
        // state without needing a re-run.
        app_.apply_edited_lines(editor_lines_);
    }
    if (editor_out_path_[0] == 0 && out_path_buf_[0] != 0) {
        std::snprintf(editor_out_path_, sizeof(editor_out_path_),
                      "%s", out_path_buf_);
    }
    return true;
}

// ===========================================================================
// Tabs
// ===========================================================================

void Gui::draw_setup_tab() {
    section_header("PROJECT");
    ImGui::TextDisabled(
        "Save the current song's audio path, lyrics, tags, and any manual edits "
        "as a .uspj file so you can reopen it later and keep finetuning.");
    ImGui::Spacing();
    if (ImGui::Button("Save project...", ImVec2(160.0f, 28.0f))) {
        std::string picked = project_path_;
        if (picked.empty() && audio_path_buf_[0]) {
            picked = default_lrc_path_for(audio_path_buf_);
            // swap .lrc -> .uspj for the suggested filename
            const auto dot = picked.rfind('.');
            if (dot != std::string::npos) picked.replace(dot, std::string::npos, ".uspj");
        }
        if (save_project_dialog(picked, picked)) {
            std::string err;
            if (save_project(picked, err)) {
                project_path_ = picked;
                log_.push_back({format_time_now(), "info", "project saved -> " + picked});
            } else {
                log_.push_back({format_time_now(), "error", "project save: " + err});
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open project...", ImVec2(160.0f, 28.0f))) {
        std::string picked;
        if (open_project_dialog(picked)) {
            std::string err;
            if (load_project(picked, err)) {
                project_path_ = picked;
                log_.push_back({format_time_now(), "info", "project loaded <- " + picked});
            } else {
                log_.push_back({format_time_now(), "error", "project load: " + err});
            }
        }
    }
    if (!project_path_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", project_path_.c_str());
    }
    ImGui::Spacing();

    section_header("FILES");
    if (form_browse("Audio file",
                    audio_path_buf_, sizeof(audio_path_buf_), "Browse...",
                    [](std::string& p) { return open_audio_file_dialog(p); })) {
        if (audio_path_buf_[0] && out_path_buf_[0] == 0) {
            const auto def = default_lrc_path_for(audio_path_buf_);
            std::snprintf(out_path_buf_, sizeof(out_path_buf_), "%s", def.c_str());
        }
    }
    form_browse("Output .lrc",
                out_path_buf_, sizeof(out_path_buf_), "Save as...",
                [&](std::string& p) {
                    const std::string suggested =
                        audio_path_buf_[0] ? default_lrc_path_for(audio_path_buf_) : std::string{};
                    return save_lrc_file_dialog(p, suggested);
                });

    ImGui::Spacing();
    section_header("LANGUAGE & GPU");
    form_text("Language", language_buf_, sizeof(language_buf_),
        "What language is the song in?\n\n"
        "  en = English   ja = Japanese   es = Spanish   ko = Korean\n"
        "  fr = French    de = German     zh = Chinese\n\n"
        "Setting it explicitly is faster AND more accurate. "
        "Use 'auto' only if you genuinely don't know.");

    form_row("Options");
    ImGui::Checkbox("GPU", &use_gpu_);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(
            "Use your graphics card instead of your CPU. WAY faster.\n\n"
            "Requires this build was compiled with a GPU backend "
            "(build.bat clean cuda or vulkan).");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Detected GPUs:");
        ImGui::TextUnformatted(detected_gpus().c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Compiled backend:");
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xE8, 0x79, 0xF9, 0xFF));
        ImGui::TextUnformatted(compiled_backends().c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Translate -> English", &translate_);
    help_marker(
        "Force lyrics to come out in English even if the song isn't English. "
        "Leave OFF unless you want translation.");

    ImGui::Spacing();
    section_header("LRC TAGS (optional)");
    form_text("Title",  title_buf_,  sizeof(title_buf_));
    form_text("Artist", artist_buf_, sizeof(artist_buf_));
    form_text("Album",  album_buf_,  sizeof(album_buf_));
}

// ---------------------------------------------------------------------------
// Split tab -- standalone Demucs stem separation, UVR-style options
// ---------------------------------------------------------------------------
namespace {

constexpr const char* kSplitModels[] = {
    "htdemucs",          // default 4-stem (vocals/drums/bass/other)
    "htdemucs_ft",       // fine-tuned, slower, slightly better SDR
    "htdemucs_6s",       // 6-stem (adds guitar + piano)
    "mdx_extra",         // MDX-Net trained on extra data
    "mdx_extra_q",       // quantized MDX-Net (smaller/faster)
    "mdx",               // baseline MDX-Net
};
constexpr const char* kSplitStems[] = {
    "All stems (4 or 6 depending on model)",
    "Vocals only (vocals + no_vocals)",
};
constexpr const char* kSplitFormats[] = { "wav", "mp3", "flac" };

}  // namespace

void Gui::start_split_job() {
    if (split_running_.load()) return;
    if (split_worker_.joinable()) split_worker_.join();

    SplitJob job;
    job.audio_path  = split_audio_buf_;
    job.out_dir     = split_outdir_buf_;
    job.model       = kSplitModels[std::clamp(split_model_idx_, 0,
                                              (int)IM_ARRAYSIZE(kSplitModels) - 1)];
    job.stems_mode  = (split_stems_idx_ == 0) ? "all" : "vocals";
    job.format      = kSplitFormats[std::clamp(split_format_idx_, 0,
                                              (int)IM_ARRAYSIZE(kSplitFormats) - 1)];
    job.mp3_bitrate = split_mp3_br_;
    job.shifts      = split_shifts_;
    job.overlap     = split_overlap_;
    job.use_gpu     = split_use_gpu_;

    split_cancel_.store(false);
    split_running_.store(true);
    {
        std::lock_guard<std::mutex> lk(split_mu_);
        split_phase_.clear();
        split_fraction_ = 0.0f;
        split_message_.clear();
        split_last_error_.clear();
        split_last_outdir_.clear();
        split_last_stems_.clear();
    }

    const bool auto_promote = split_auto_promote_;
    split_worker_ = std::thread([this, job, auto_promote]() {
        auto on_prog = [this](const char* phase, float f, const std::string& m) {
            {
                std::lock_guard<std::mutex> lk(split_mu_);
                split_phase_ = phase ? phase : "";
                if (f >= 0.0f) split_fraction_ = f;
                if (!m.empty()) split_message_ = m;
            }
            if (!m.empty()) app_.append_log(m);
        };
        SplitResult result = run_split(job, on_prog, split_cancel_);

        // Find the vocals stem so we can auto-promote it.
        std::string vocals_path;
        for (const auto& s : result.stems) {
            const std::string fn =
                std::filesystem::path(s).filename().string();
            if (fn.size() >= 7 && fn.compare(0, 7, "vocals.") == 0) {
                vocals_path = s;
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lk(split_mu_);
            if (result.ok) {
                split_last_outdir_ = result.out_dir;
                split_last_stems_  = result.stems;
                split_fraction_    = 1.0f;
                split_message_     = "done";
                if (auto_promote && !vocals_path.empty()) {
                    split_promote_audio_ = vocals_path;
                }
            } else {
                split_last_error_  = result.error;
            }
        }
        split_running_.store(false);
    });
}

void Gui::draw_split_tab() {
    // Pick up any pending vocals-path the worker stashed. Done on the main
    // thread so we never race the ImGui InputText reads against the worker
    // writing into the audio_path_buf_ char buffer.
    std::string promote;
    {
        std::lock_guard<std::mutex> lk(split_mu_);
        if (!split_promote_audio_.empty()) {
            promote = std::move(split_promote_audio_);
            split_promote_audio_.clear();
        }
    }
    if (!promote.empty()) {
        std::snprintf(audio_path_buf_, sizeof(audio_path_buf_), "%s", promote.c_str());
        // If the output path was empty or pointed at the OLD audio file,
        // suggest a sensible default next to the new vocals stem.
        if (out_path_buf_[0] == 0) {
            const auto def = default_lrc_path_for(audio_path_buf_);
            std::snprintf(out_path_buf_, sizeof(out_path_buf_), "%s", def.c_str());
        }
        app_.append_log("split: auto-set Setup audio to " + promote);
    }

    section_header("STEM SEPARATION (Demucs)");
    ImGui::TextDisabled("Standalone stem splitter. Pick an audio file, output folder, model + options, click START SPLIT.");
    ImGui::TextDisabled("Pre-process step before alignment: produce a clean vocals.wav, then point Setup -> Audio file at it for a major accuracy boost.");
    ImGui::Spacing();

    section_header("INPUT");
    form_browse("Audio file",
                split_audio_buf_, sizeof(split_audio_buf_), "Browse...",
                [](std::string& p) { return open_audio_file_dialog(p); });
    form_browse("Output folder",
                split_outdir_buf_, sizeof(split_outdir_buf_), "Browse...",
                [](std::string& p) { return pick_folder_dialog(p); });

    ImGui::Spacing();
    section_header("MODEL");

    form_row("Model");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##split_model", &split_model_idx_,
                 kSplitModels, IM_ARRAYSIZE(kSplitModels));
    help_marker(
        "htdemucs       - default 4-stem (vocals/drums/bass/other). Fast, great quality.\n"
        "htdemucs_ft    - fine-tuned htdemucs. Slower (~4x), slightly tighter SDR.\n"
        "htdemucs_6s    - 6 stems: adds guitar + piano. ~50% slower.\n"
        "mdx_extra      - MDX-Net trained on extra data. Different artifacts.\n"
        "mdx_extra_q    - quantized MDX-Net. Smaller, faster, slightly lossier.\n"
        "mdx            - baseline MDX-Net (Demucs v3 era).");

    form_row("Stems");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##split_stems", &split_stems_idx_,
                 kSplitStems, IM_ARRAYSIZE(kSplitStems));
    help_marker(
        "All stems - outputs every stem the model produces (4 for htdemucs, 6 for 6s).\n"
        "Vocals only - outputs JUST vocals.wav + no_vocals.wav. Half the disk, "
        "much faster, perfect for karaoke / lyric workflows.");

    form_row("Format");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##split_fmt", &split_format_idx_,
                 kSplitFormats, IM_ARRAYSIZE(kSplitFormats));
    if (split_format_idx_ == 1) {   // mp3
        ImGui::SameLine();
        ImGui::TextDisabled("kbps");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderInt("##split_mp3br", &split_mp3_br_, 64, 320);
    }

    ImGui::Spacing();
    section_header("QUALITY");

    form_slider("Shifts", &split_shifts_, 0, 10,
        "Equivariant stabilization shifts. The model runs once per shift on a "
        "slightly time-shifted copy of the audio, and the results are averaged. "
        "Higher = better SDR, linearly slower.\n\n"
        "  0   = fastest, no stabilization\n"
        "  1   = default, good quality\n"
        "  2-5 = noticeably tighter, ~Nx slower\n"
        "  10  = diminishing returns past this");

    form_row("Overlap");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##split_overlap", &split_overlap_, 0.0f, 0.5f, "%.2f");
    help_marker(
        "Chunk overlap (0.0 - 0.5). The model processes audio in chunks and "
        "blends overlapping regions. Higher = smoother boundaries between "
        "chunks, slightly slower. 0.25 is the Demucs default.");

    form_row("Options");
    ImGui::Checkbox("Use GPU", &split_use_gpu_);
    help_marker(
        "Run Demucs on the GPU (if PyTorch was installed with CUDA). On "
        "a 3070-class GPU a 3-minute song splits in 10-30 seconds depending "
        "on shifts. On CPU it's typically 5-15 minutes.");
    ImGui::SameLine();
    ImGui::Checkbox("Auto-set vocals as Setup audio", &split_auto_promote_);
    help_marker(
        "When the split finishes, automatically writes the produced "
        "vocals.<format> path into the Setup tab's 'Audio file' field so "
        "you can jump straight to the Lyrics tab and hit GENERATE without "
        "copy-pasting paths around.\n\n"
        "Turn off if you want to use the split for something other than "
        "feeding it into the lyrics aligner.");

    ImGui::Spacing();
    ImGui::Dummy(ImVec2(0, 4));

    const bool busy = split_running_.load();
    const bool can_start = !busy
                        && split_audio_buf_[0] != 0
                        && split_outdir_buf_[0] != 0;
    ImGui::BeginDisabled(!can_start);
    if (gradient_button(busy ? "SPLITTING..." : "START SPLIT",
                        ImVec2(-FLT_MIN, 44.0f))) {
        start_split_job();
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 32.0f))) {
            split_cancel_.store(true);
        }
    }

    ImGui::Spacing();
    // Progress + status panel.
    {
        std::lock_guard<std::mutex> lk(split_mu_);
        if (busy || split_fraction_ > 0.0f) {
            char overlay[64];
            std::snprintf(overlay, sizeof(overlay), "%.0f%%",
                          split_fraction_ * 100.0f);
            gradient_progress_bar(split_fraction_, ImVec2(-FLT_MIN, 20.0f), overlay);
            if (!split_message_.empty()) {
                ImGui::TextDisabled("%s", split_message_.c_str());
            }
        }
        if (!split_last_error_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0x6A, 0x6A, 255));
            ImGui::TextWrapped("ERR: %s", split_last_error_.c_str());
            ImGui::PopStyleColor();
        }
        if (!split_last_stems_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x9B, 0xE8, 0xA6, 255));
            ImGui::Text("Done. %zu stem(s) in:", split_last_stems_.size());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("%s", split_last_outdir_.c_str());
            ImGui::Spacing();
            for (const auto& s : split_last_stems_) {
                ImGui::BulletText("%s",
                    std::filesystem::path(s).filename().string().c_str());
            }
        }
    }
}

void Gui::draw_quality_tab() {
    section_header("MODEL");

    ImGui::PushID("model_row");
    form_row("Model");
    {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float refresh_w = ImGui::CalcTextSize("Refresh").x + st.FramePadding.x * 2.0f;
        const float help_w    = ImGui::CalcTextSize("(?)").x;
        const float spacing   = st.ItemSpacing.x;
        const float combo_w   = ImGui::GetContentRegionAvail().x
                              - refresh_w - help_w - spacing * 2.0f;
        ImGui::SetNextItemWidth(combo_w);
        const char* preview = (selected_model_idx_ >= 0
                               && selected_model_idx_ < (int)available_models_.size())
                              ? available_models_[selected_model_idx_].c_str()
                              : "(no models found in models/)";
        if (ImGui::BeginCombo("##model", preview)) {
            if (available_models_.empty()) {
                ImGui::TextDisabled("Drop a ggml-*.bin into 'models/' then click Refresh.");
                ImGui::TextDisabled("Or use the Models tab to download.");
            }
            for (int i = 0; i < (int)available_models_.size(); ++i) {
                const bool sel = (i == selected_model_idx_);
                if (ImGui::Selectable(available_models_[i].c_str(), sel)) {
                    selected_model_idx_ = i;
                    std::snprintf(model_path_buf_, sizeof(model_path_buf_), "%s",
                                  available_models_[i].c_str());
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Refresh")) scan_models();
        help_marker(
            "Whisper model. Only ggml-large-v3.bin is supported now "
            "(empirically the only one that produces good lyric sync). "
            "Download it from the Models tab if you don't have it.");
    }
    ImGui::PopID();

    ImGui::Spacing();
    section_header("ACCURACY KNOBS");

    form_slider("Iter passes", &iterative_align_passes_, 0, 20,
        "*** THE ACCURACY GOLD KNOB *** -- only used with pasted lyrics.\n\n"
        "Each pass re-runs whisper on every lyric line with the line's text "
        "as a PROMPT. Whisper stops fighting WHAT the words are, only solves "
        "WHEN. Each pass shrinks the audio window around the previous best "
        "guesses.\n\n"
        "  0   = disabled\n"
        "  3-5 = solid (few mins on GPU)\n"
        "  10  = recommended for perfection\n"
        "  15+ = squeeze last few ms out\n\n"
        "Cost: N small whisper calls per pass (N = line count).");

    form_slider("Refine passes", &refine_passes_, 1, 6,
        "Overlapping-window consensus passes on the initial transcription. "
        "1 = single pass. 6 = obsessive.");

    form_slider("Beam size", &beam_size_, 1, 8,
        "How many candidate decodings whisper keeps open at once.\n"
        "  1     = greedy / fastest\n"
        "  3-5   = sharper on hard segments\n"
        "  6-8   = diminishing returns");

    form_slider("Best-of", &best_of_, 1, 8,
        "Retry count when whisper is unsure on a segment (only when "
        "Beam size = 1).");

    form_slider("Threads", &threads_, 0, 32,
        "CPU threads. 0 = use all. Ignored if GPU is on.");
}

void Gui::draw_lyrics_tab() {
    section_header("LYRICS (required)");
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 222, 255));
    ImGui::TextWrapped(
        "Paste the song's lyrics below. The pipeline uses whisper to get "
        "word TIMINGS, then maps YOUR text onto whisper's word grid via "
        "Needleman-Wunsch alignment. Output is exactly what you paste, "
        "with whisper's tight per-word timestamps.");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("One line in the box = one line in the .lrc. Blank lines ignored. Punctuation + capitalization preserved.");

    ImGui::Spacing();
    // Note: previously had an inline "Split vocals (Demucs)" checkbox here.
    // That's been promoted to its own full **Split** tab with the complete
    // UVR-style controls (model / shifts / overlap / format). The flow is
    // now: Split tab -> produces vocals.wav -> point Setup tab's "Audio
    // file" at that vocals.wav -> run WhisperX alignment on the clean stem.
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 200, 255));
    ImGui::TextWrapped(
        "Tip: for music, run the Split tab first to get a clean vocals.wav, "
        "then point Setup -> Audio file at that vocals.wav for a major "
        "accuracy boost. WhisperX hears the words much more clearly with "
        "no instrumental in the way.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Checkbox("Use WhisperX (Python, wav2vec2)", &use_whisperx_);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(
            "Route through Python WhisperX instead of whisper.cpp.\n\n"
            "WhisperX uses wav2vec2 + CTC forced alignment -- the industry "
            "standard for sub-50ms word timing.\n\n"
            "Requires:\n"
            "  1. Python 3.9+ on PATH\n"
            "  2. pip install whisperx\n"
            "  3. PyTorch w/ CUDA if you want GPU\n\n"
            "Honest: WhisperX hits 20-50ms. 2-3ms requires manual nudging "
            "in the Editor tab.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("recheck")) refresh_whisperx_status();
    ImGui::SameLine();
    if (ImGui::SmallButton("Install / Setup")) {
        // Spawn setup_whisperx.bat in a console window so the user can see
        // the progress. They confirm + watch the install.
        ShellExecuteA(nullptr, "open", "setup_whisperx.bat", nullptr,
                      nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Launches setup_whisperx.bat in a new console.\n"
                          "Click 'recheck' here once it finishes.");
    }
    const auto& wxs = whisperx_status();
    ImGui::SameLine();
    if (wxs.whisperx_installed) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x9B, 0xE8, 0xA6, 0xFF));
        ImGui::Text("WhisperX %s ready", wxs.whisperx_version.c_str());
        ImGui::PopStyleColor();
    } else if (wxs.python_found) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0xCC, 0x70, 0xFF));
        ImGui::Text("Python OK, not installed -- click Install / Setup");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0x6A, 0x6A, 0xFF));
        ImGui::Text("Python not found -- install Python 3.10+ then click Setup");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    if (ImGui::Button("Load .txt...")) {
        std::string path;
        if (open_lyrics_file_dialog(path) && !path.empty()) {
            std::ifstream f(path, std::ios::binary);
            if (f) {
                std::stringstream ss;
                ss << f.rdbuf();
                user_lyrics_ = ss.str();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) user_lyrics_.clear();
    ImGui::SameLine();
    const size_t line_count = std::count(user_lyrics_.begin(), user_lyrics_.end(), '\n')
                              + (user_lyrics_.empty() ? 0 : 1);
    ImGui::TextDisabled("%zu chars, ~%zu lines", user_lyrics_.size(), line_count);

    struct Cb {
        static int resize(ImGuiInputTextCallbackData* d) {
            if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* s = static_cast<std::string*>(d->UserData);
                s->resize(d->BufTextLen);
                d->Buf = s->data();
            }
            return 0;
        }
    };
    if (user_lyrics_.capacity() < 32) user_lyrics_.reserve(32);
    ImGui::InputTextMultiline(
        "##lyrics", user_lyrics_.data(), user_lyrics_.capacity() + 1,
        ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y - 8),
        ImGuiInputTextFlags_CallbackResize, Cb::resize, &user_lyrics_);
}

// ---------------------------------------------------------------------------
// Footer (compact status + Generate button)
// ---------------------------------------------------------------------------
void Gui::draw_footer() {
    const auto  st   = app_.status();
    const bool  busy = app_.job_in_flight();

    ImGui::BeginChild("footer", ImVec2(0, 0), true);
    ImGui::SetWindowFontScale(1.08f);

    const float avail   = ImGui::GetContentRegionAvail().x;
    const float right_w = std::min(avail * 0.42f, 360.0f);
    const float left_w  = avail - right_w - ImGui::GetStyle().ItemSpacing.x;

    // --- Left: state pill + phase + elapsed + brief message ---
    ImGui::BeginChild("##foot_l", ImVec2(left_w, 0), false);
    ImGui::SetWindowFontScale(1.08f);

    ImU32 pill_col = IM_COL32(80, 70, 100, 255);
    switch (st.state) {
        case JobState::Running:   pill_col = IM_COL32(0xE8, 0x79, 0xF9, 255); break;
        case JobState::Done:      pill_col = IM_COL32(0x6F, 0xCF, 0x97, 255); break;
        case JobState::Error:     pill_col = IM_COL32(0xF8, 0x71, 0x71, 255); break;
        case JobState::Cancelled: pill_col = IM_COL32(0xA8, 0x55, 0xF7, 255); break;
        default: break;
    }
    {
        const char* lbl = job_state_label(st.state);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 ts  = ImGui::CalcTextSize(lbl);
        const float pad_x = 10.0f, pad_y = 4.0f;
        const ImVec2 br(pos.x + ts.x + pad_x * 2, pos.y + ts.y + pad_y * 2);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, br, pill_col, 10.0f);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(pos.x + pad_x, pos.y + pad_y), IM_COL32_WHITE, lbl);
        ImGui::Dummy(ImVec2(ts.x + pad_x * 2, ts.y + pad_y * 2));
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("phase");
    ImGui::SameLine();
    ImGui::Text("%s", st.phase.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("elapsed");
    ImGui::SameLine();
    ImGui::Text("%.1fs", st.elapsed_seconds);

    // One-line summary -- full error text lives in the Log tab.
    if (st.state == JobState::Error) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0x6A, 0x6A, 0xFF));
        ImGui::TextWrapped("ERROR - see Log tab. (%.80s%s)",
                           st.error.c_str(), st.error.size() > 80 ? "..." : "");
        ImGui::PopStyleColor();
    } else if (st.state == JobState::Done) {
        const auto p = app_.last_lrc_path();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x9B, 0xE8, 0xA6, 0xFF));
        ImGui::TextWrapped("wrote %s", p.c_str());
        ImGui::PopStyleColor();
    } else if (!st.message.empty()) {
        ImGui::TextDisabled("%.120s%s", st.message.c_str(),
                            st.message.size() > 120 ? "..." : "");
    } else {
        ImGui::TextDisabled("idle -- set up a file in Setup, then hit GENERATE");
    }
    ImGui::EndChild();

    // --- Right: progress + Generate / Cancel ---
    ImGui::SameLine();
    ImGui::BeginChild("##foot_r", ImVec2(0, 0), false);
    ImGui::SetWindowFontScale(1.08f);
    char overlay[48];
    std::snprintf(overlay, sizeof(overlay), "%.0f%%", st.fraction * 100.0f);
    gradient_progress_bar(st.fraction, ImVec2(-FLT_MIN, 18.0f), overlay);
    ImGui::Spacing();
    if (busy) {
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 28.0f))) app_.cancel_job();
    } else {
        // Disabled until BOTH an audio file is set AND lyrics have been
        // pasted -- whisper-only mode was removed; user lyrics are required.
        ImGui::BeginDisabled(audio_path_buf_[0] == 0 || user_lyrics_.empty());
        if (gradient_button("GENERATE  .LRC", ImVec2(-FLT_MIN, 28.0f))) {
            if (out_path_buf_[0] == 0) {
                const auto def = default_lrc_path_for(audio_path_buf_);
                std::snprintf(out_path_buf_, sizeof(out_path_buf_), "%s", def.c_str());
            }
            TranscribeOptions opts;
            opts.model_path             = model_path_buf_;
            opts.language               = language_buf_;
            opts.threads                = threads_;
            opts.beam_size              = beam_size_;
            opts.best_of                = best_of_;
            opts.refine_passes          = refine_passes_;
            opts.iterative_align_passes = iterative_align_passes_;
            opts.translate              = translate_;
            opts.use_gpu                = use_gpu_;
            opts.split_vocals           = false;  // Split is now its own tab
            LrcMetadata meta;
            meta.title  = title_buf_;
            meta.artist = artist_buf_;
            meta.album  = album_buf_;
            app_.start_job(audio_path_buf_, out_path_buf_, opts, meta,
                           user_lyrics_,        // always required now
                           use_whisperx_);
        }
        ImGui::EndDisabled();
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Log tab (scrollable history, no truncation)
// ---------------------------------------------------------------------------
void Gui::draw_log_tab() {
    section_header("MESSAGE LOG");
    if (ImGui::Button("Clear")) log_.clear();
    ImGui::SameLine();
    if (ImGui::Button("Export...")) {
        // Pick a save path via the standard save dialog (re-using the
        // .lrc one since it filters generically too).
        std::string path;
        if (save_lrc_file_dialog(path, "usersync_log.txt") && !path.empty()) {
            std::ofstream f(path, std::ios::binary);
            if (f) {
                for (const auto& l : log_) {
                    f << "[" << l.ts << "] "
                      << "[" << l.level << "] "
                      << l.text << "\n";
                }
                log_.push_back({ format_time_now(), "info",
                                 "exported " + std::to_string(log_.size())
                                 + " lines to " + path });
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy all")) {
        std::string all;
        for (const auto& l : log_) {
            all += "[" + l.ts + "] [" + l.level + "] " + l.text + "\n";
        }
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu entries (oldest dropped after 5000)", log_.size());

    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), true);
    if (log_.empty()) {
        ImGui::TextDisabled("(no messages yet)");
    } else {
        for (const auto& l : log_) {
            ImU32 col = IM_COL32(220, 220, 222, 255);
            if      (l.level == "error") col = IM_COL32(0xFF, 0x6A, 0x6A, 255);
            else if (l.level == "warn")  col = IM_COL32(0xFF, 0xCC, 0x70, 255);
            else if (l.level == "seg")   col = IM_COL32(0xF0, 0xAB, 0xFC, 255);
            else if (l.level == "word")  col = IM_COL32(0xA8, 0x55, 0xF7, 255);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 140, 255));
            ImGui::Text("[%s]", l.ts.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Manual Editor tab (nudge each word's timing)
// ---------------------------------------------------------------------------
void Gui::draw_editor_tab() {
    section_header("MANUAL WORD EDITOR");

    // Top toolbar.
    if (ImGui::Button("Pull from current job", ImVec2(220.0f, 28.0f))) {
        editor_pull_from_app();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editor_lines_.empty());
    if (ImGui::Button("Save edited .lrc", ImVec2(160.0f, 28.0f))) {
        std::string err;
        if (editor_save_to_disk(err)) {
            log_.push_back({format_time_now(), "info",
                            "editor saved to " + std::string(editor_out_path_)});
        } else {
            log_.push_back({format_time_now(), "error", "editor save: " + err});
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop playback", ImVec2(140.0f, 28.0f))) player_.stop();
    ImGui::SameLine();
    ImGui::BeginDisabled(editor_lines_.empty());
    if (ImGui::Button("Expand all", ImVec2(110.0f, 28.0f))) {
        editor_line_expanded_.assign(editor_lines_.size(), true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Collapse all", ImVec2(120.0f, 28.0f))) {
        editor_line_expanded_.assign(editor_lines_.size(), false);
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::InputText("Output .lrc", editor_out_path_, sizeof(editor_out_path_));

    if (editor_lines_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Click \"Pull from current job\" to load the most recent alignment");
        ImGui::TextDisabled("for manual editing. Each word's start time can then be nudged.");
        return;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Drag the start-time field for fine adjustment, or use the nudge buttons.");
    ImGui::TextDisabled("Each word's end auto-follows the next word's start. Adjacent words clamp so they never overlap.");
    ImGui::TextDisabled("Gaps >30s between lines get a [MUSIC] marker in the saved .lrc.");
    ImGui::Spacing();

    // Apply cascading-clamp to a word's NEW start time. Keeps the word
    // strictly between its previous and next siblings (and between its
    // line and the surrounding lines for first/last words).
    auto cascade_set_start = [&](size_t li, size_t wi, double new_start) {
        auto& line = editor_lines_[li];
        if (line.words.empty()) return;
        auto& w = line.words[wi];

        // Lower bound: previous word's start + 1ms (or prev line's end).
        double lo = 0.0;
        if (wi > 0) {
            lo = line.words[wi - 1].t_start + 0.001;
        } else if (li > 0 && !editor_lines_[li - 1].words.empty()) {
            lo = editor_lines_[li - 1].words.back().t_start + 0.001;
        }
        // Upper bound: next word's start - 1ms (or next line's first word).
        double hi = 1.0e9;
        if (wi + 1 < line.words.size()) {
            hi = line.words[wi + 1].t_start - 0.001;
        } else if (li + 1 < editor_lines_.size() && !editor_lines_[li + 1].words.empty()) {
            hi = editor_lines_[li + 1].words.front().t_start - 0.001;
        }
        if (new_start < lo) new_start = lo;
        if (new_start > hi) new_start = hi;
        w.t_start = new_start;

        // Auto-derive ends from next-word starts everywhere.
        if (wi > 0) line.words[wi - 1].t_end = w.t_start;
        if (wi + 1 < line.words.size()) {
            w.t_end = line.words[wi + 1].t_start;
        } else if (li + 1 < editor_lines_.size() && !editor_lines_[li + 1].words.empty()) {
            w.t_end = editor_lines_[li + 1].words.front().t_start;
        }
        line.t_start = line.words.front().t_start;
        line.t_end   = line.words.back().t_end;

        // Push the edited timing back into the App so the Preview tab's
        // scrolling lyrics and the Output tab's LRC text both reflect the
        // change on the next frame.
        app_.apply_edited_lines(editor_lines_);
    };

    if (ImGui::BeginTable("##editor", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner |
                          ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Word", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Start (s)", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Nudge", ImGuiTableColumnFlags_WidthFixed, 460.0f);
        ImGui::TableSetupColumn("Play", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("End (s)", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Keep the expanded-state vector in sync if the user edited
        // somewhere else and the line count changed.
        if (editor_line_expanded_.size() != editor_lines_.size()) {
            editor_line_expanded_.resize(editor_lines_.size(), true);
        }

        for (size_t li = 0; li < editor_lines_.size(); ++li) {
            auto& line = editor_lines_[li];
            // std::vector<bool> can't be bound to bool& (proxy reference);
            // copy out + write back.
            bool expanded = editor_line_expanded_[li];

            // ===== Line-header row =====
            // Spans all 6 columns visually so the user always knows which
            // line they're scrolled into. Pink-tinted background, expand
            // arrow + line number + reconstructed line text + time range.
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   IM_COL32(0xD9, 0x46, 0xEF, 28));
            ImGui::TableNextColumn();
            ImGui::PushID((int)(li + 1) * 99991);  // unique id for the arrow btn
            const char* arrow = expanded ? "v" : ">";
            if (ImGui::SmallButton(arrow)) {
                expanded = !expanded;
                editor_line_expanded_[li] = expanded;
            }
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xE8, 0x79, 0xF9, 255));
            ImGui::Text("L %zu", li + 1);
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            // Reconstructed line text + time range span the other cols.
            std::string lt;
            for (size_t i = 0; i < line.words.size(); ++i) {
                if (i) lt.push_back(' ');
                lt += line.words[i].text;
            }
            char ts_buf[48];
            std::snprintf(ts_buf, sizeof(ts_buf), "  [%.2fs -> %.2fs]",
                          line.t_start, line.t_end);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 215, 230, 255));
            ImGui::TextUnformatted(lt.c_str());
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 145, 165, 255));
            ImGui::TextUnformatted(ts_buf);
            ImGui::PopStyleColor();
            // Fill remaining cells so the row renders cleanly. When the
            // line is collapsed, show the word count in the Nudge column
            // as a hint.
            ImGui::TableNextColumn();
            if (!expanded) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 145, 165, 255));
                ImGui::Text("(%zu words collapsed)", line.words.size());
                ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();

            // ===== Per-word rows (only when expanded) =====
            if (!expanded) continue;
            for (size_t wi = 0; wi < line.words.size(); ++wi) {
                auto& w = line.words[wi];
                ImGui::TableNextRow();
                ImGui::PushID((int)(li * 10000 + wi));

                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 140, 255));
                ImGui::Text("%zu", li + 1);
                ImGui::PopStyleColor();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(w.text.c_str());

                ImGui::TableNextColumn();
                float tf = (float)w.t_start;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##ts", &tf, 0.001f, 0.0f, 99999.0f, "%.3f")) {
                    cascade_set_start(li, wi, tf);
                }

                ImGui::TableNextColumn();
                auto nudge = [&](double dt) { cascade_set_start(li, wi, w.t_start + dt); };
                if (ImGui::SmallButton("-100ms")) nudge(-0.100);
                ImGui::SameLine();
                if (ImGui::SmallButton("-50ms"))  nudge(-0.050);
                ImGui::SameLine();
                if (ImGui::SmallButton("-10ms"))  nudge(-0.010);
                ImGui::SameLine();
                if (ImGui::SmallButton("-1ms"))   nudge(-0.001);
                ImGui::SameLine();
                if (ImGui::SmallButton("+1ms"))   nudge(+0.001);
                ImGui::SameLine();
                if (ImGui::SmallButton("+10ms"))  nudge(+0.010);
                ImGui::SameLine();
                if (ImGui::SmallButton("+50ms"))  nudge(+0.050);
                ImGui::SameLine();
                if (ImGui::SmallButton("+100ms")) nudge(+0.100);

                ImGui::TableNextColumn();
                if (ImGui::SmallButton(">")) editor_play_at(w.t_start);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", w.t_end);

                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// Output tab (LRC preview + word timeline + word table)
// ---------------------------------------------------------------------------
void Gui::draw_output_tab() {
    if (ImGui::BeginTabBar("##output_subtabs")) {
        if (ImGui::BeginTabItem("LRC text")) {
            const auto preview = app_.last_lrc_preview(16384);
            if (preview.empty()) {
                ImGui::TextDisabled("(no LRC yet -- run a job)");
            } else {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 16, 28, 255));
                ImGui::InputTextMultiline(
                    "##lrc", const_cast<char*>(preview.c_str()), preview.size() + 1,
                    ImVec2(-FLT_MIN, -FLT_MIN),
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Words")) {
            const auto lines = app_.snapshot_lines();
            if (lines.empty()) {
                ImGui::TextDisabled("(no words yet -- run a job)");
            } else {
                const double total = std::max(1.0, lines.back().t_end);
                const ImVec2 avail   = ImGui::GetContentRegionAvail();
                const float  strip_h = 26.0f;
                const ImVec2 origin  = ImGui::GetCursorScreenPos();
                const ImVec2 br(origin.x + avail.x, origin.y + strip_h);
                ImDrawList*  dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(origin, br, IM_COL32(22, 16, 28, 255), 3.0f);
                dl->AddRect(origin, br, IM_COL32(255, 255, 255, 25), 3.0f);
                for (const auto& l : lines)
                    for (const auto& w : l.words) {
                        const float t = (float)(w.t_start / total);
                        const float x = origin.x + 4.0f + (avail.x - 8.0f) * t;
                        const float y = (origin.y + br.y) * 0.5f;
                        dl->AddCircleFilled(ImVec2(x, y), 2.5f, ig_lerp_stops(t));
                    }
                ImGui::Dummy(ImVec2(avail.x, strip_h));
                ImGui::Spacing();
                if (ImGui::BeginTable("words", 4,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner |
                                      ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("start");
                    ImGui::TableSetupColumn("end");
                    ImGui::TableSetupColumn("conf");
                    ImGui::TableSetupColumn("word");
                    ImGui::TableHeadersRow();
                    for (const auto& l : lines)
                        for (const auto& w : l.words) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%7.3f", w.t_start);
                            ImGui::TableNextColumn(); ImGui::Text("%7.3f", w.t_end);
                            ImGui::TableNextColumn();
                            {
                                const ImU32 c = ig_lerp_stops(std::clamp(w.prob, 0.0f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_Text, c);
                                ImGui::Text("%.2f", w.prob);
                                ImGui::PopStyleColor();
                            }
                            ImGui::TableNextColumn(); ImGui::TextUnformatted(w.text.c_str());
                        }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ===========================================================================
// Sync Preview (was draw_sync_tab) -- real preview content for draw_preview_tab
// ===========================================================================
void Gui::draw_preview_tab() {
    section_header("SYNC PREVIEW");

    // Lazy-load the audio file the user picked.
    if (audio_path_buf_[0] == 0) {
        ImGui::TextDisabled("Pick an audio file in the Input panel first.");
        return;
    }
    if (!player_.is_loaded() || player_.loaded_path() != audio_path_buf_) {
        if (ImGui::Button("Load audio for preview", ImVec2(-FLT_MIN, 32.0f))) {
            sync_player_to_audio_path();
        }
        if (!player_load_error_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0x6A, 0x6A, 0xFF));
            ImGui::TextWrapped("Load failed: %s", player_load_error_.c_str());
            ImGui::PopStyleColor();
        }
        if (!player_.is_loaded()) return;
    }

    const auto   lines    = app_.snapshot_lines();
    const double pos      = player_.position_seconds();
    const double duration = std::max(0.001, player_.duration_seconds());

    // Transport controls: Play/Pause, Stop, Reload, autoscroll toggle.
    const bool playing = player_.is_playing();
    if (gradient_button(playing ? "Pause" : "Play", ImVec2(110.0f, 34.0f))) player_.toggle();
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(70.0f, 34.0f)))  player_.stop();
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(80.0f, 34.0f))) {
        player_load_error_.clear();
        player_.load(audio_path_buf_, player_load_error_);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoscroll_lyrics_);

    // Time + scrub slider.
    char tbuf[64];
    std::snprintf(tbuf, sizeof(tbuf), "%02d:%05.2f / %02d:%05.2f",
                  (int)(pos / 60),      std::fmod(pos, 60.0),
                  (int)(duration / 60), std::fmod(duration, 60.0));
    ImGui::Text("%s", tbuf);
    float fpos = static_cast<float>(pos);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##scrub", &fpos, 0.0f, (float)duration, "%.2fs")) {
        player_.seek_seconds(fpos);
    }

    // Compact "keyframe" strip showing all words colored by the gradient,
    // with a vertical playhead at the current position.
    {
        const ImVec2 avail   = ImGui::GetContentRegionAvail();
        const float  strip_h = 22.0f;
        const ImVec2 origin  = ImGui::GetCursorScreenPos();
        const ImVec2 br(origin.x + avail.x, origin.y + strip_h);
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, br, IM_COL32(22, 22, 24, 255), 3.0f);
        dl->AddRect(origin, br, IM_COL32(255, 255, 255, 25), 3.0f);
        for (const auto& line : lines) {
            for (const auto& w : line.words) {
                const float t = static_cast<float>(w.t_start / duration);
                const float x = origin.x + 4.0f + (avail.x - 8.0f) * std::clamp(t, 0.0f, 1.0f);
                const float y = (origin.y + br.y) * 0.5f;
                dl->AddCircleFilled(ImVec2(x, y), 2.5f, ig_lerp_stops(t));
            }
        }
        const float px = origin.x + 4.0f
                       + (avail.x - 8.0f) * std::clamp((float)(pos / duration), 0.0f, 1.0f);
        dl->AddLine(ImVec2(px, origin.y - 2), ImVec2(px, br.y + 2),
                    IM_COL32_WHITE, 2.0f);
        ImGui::Dummy(ImVec2(avail.x, strip_h));
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (lines.empty()) {
        ImGui::TextDisabled("No lyrics yet -- run a job to see synced words here.");
        return;
    }

    // Scrolling lyrics view. Each line is one ImGui line; per-word coloring
    // distinguishes past / current / future. Auto-scroll keeps the active
    // line centered while the user isn't dragging the scrubber.
    ImGui::BeginChild("##lyrics_view", ImVec2(0, 0), true);
    int active_line = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (pos >= lines[i].t_start - 0.05 && pos <= lines[i].t_end + 0.05) {
            active_line = (int)i;
            break;
        }
    }
    // If we're between lines, highlight whichever line just played.
    if (active_line < 0) {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].t_start <= pos) active_line = (int)i;
            else break;
        }
    }

    for (size_t li = 0; li < lines.size(); ++li) {
        const auto& line = lines[li];
        const bool  is_active = ((int)li == active_line);

        // Subtle row highlight behind the active line.
        if (is_active) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float  w = ImGui::GetContentRegionAvail().x;
            const float  h = ImGui::GetTextLineHeightWithSpacing();
            dl->AddRectFilled(ImVec2(p.x - 4, p.y - 2),
                              ImVec2(p.x + w, p.y + h - 2),
                              IM_COL32(0xD9, 0x46, 0xEF, 35), 3.0f);
        }

        // Optional time tag at the start of each line.
        char ts[16];
        std::snprintf(ts, sizeof(ts), "%02d:%05.2f",
                      (int)(line.t_start / 60), std::fmod(line.t_start, 60.0));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 124, 255));
        ImGui::TextUnformatted(ts);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        for (size_t wi = 0; wi < line.words.size(); ++wi) {
            const auto& w = line.words[wi];
            ImU32 col;
            if      (pos >= w.t_end)   col = IM_COL32(150, 150, 154, 255);   // past
            else if (pos >= w.t_start) col = IM_COL32(0xE8, 0x79, 0xF9, 255); // current
            else                        col = IM_COL32(230, 230, 232, 255);   // future
            if (is_active && pos >= w.t_start && pos < w.t_end) {
                col = IM_COL32(0xD9, 0x46, 0xEF, 255);  // hot pink for the live word
            }
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(w.text.c_str());
            ImGui::PopStyleColor();
            if (wi + 1 < line.words.size()) ImGui::SameLine();
        }

        if (is_active && autoscroll_lyrics_) {
            ImGui::SetScrollHereY(0.5f);  // center the active line
        }
    }
    ImGui::EndChild();
}

namespace {

struct CatalogModel {
    const char* filename;
    const char* url;
    const char* size_label;
    const char* tier;        // visual tag
    const char* description;
};

constexpr CatalogModel CATALOG[] = {
    // The only model that consistently produces good lyric sync in practice.
    // Quantized + turbo variants all underperform the full-precision base
    // weights on music; keep the catalog focused.
    {"ggml-large-v3.bin",
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin",
     "~3.1 GB", "ONLY",
     "Full-precision large-v3. The only model that produces consistently "
     "good lyric sync. Needs ~6 GB VRAM with DTW + activations. Required."},
};

ImU32 tier_color(const char* tier) {
    // All within the userinfo.lol pink->purple ramp + a couple muted greys.
    if (std::strstr(tier, "MAX"))         return IM_COL32(0xD9, 0x46, 0xEF, 255); // pink-dark
    if (std::strstr(tier, "BEST"))        return IM_COL32(0x7C, 0x3A, 0xED, 255); // purple-dark
    if (std::strstr(tier, "RECOMMENDED")) return IM_COL32(0xE8, 0x79, 0xF9, 255); // pink
    if (std::strstr(tier, "GOOD"))        return IM_COL32(0xF0, 0xAB, 0xFC, 255); // pink-light
    if (std::strstr(tier, "OK"))          return IM_COL32(0xA8, 0x55, 0xF7, 220); // purple, faded
    if (std::strstr(tier, "DRAFT"))       return IM_COL32(180, 170, 200, 220);    // muted lavender
    return                                       IM_COL32(140, 130, 160, 220);
}

bool model_file_exists(const char* filename) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path("models") / filename, ec);
}

std::string human_bytes(uint64_t b) {
    char buf[32];
    if      (b >= (uint64_t)1 << 30) std::snprintf(buf, sizeof(buf), "%.2f GB", b / 1073741824.0);
    else if (b >= (uint64_t)1 << 20) std::snprintf(buf, sizeof(buf), "%.1f MB", b / 1048576.0);
    else if (b >= (uint64_t)1 << 10) std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
    else                              std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
    return buf;
}

}  // namespace

void Gui::draw_models_tab() {
    section_header("DOWNLOAD MODELS");

    const auto st = downloader_.snapshot();
    const bool busy = downloader_.busy();

    // ----- Active download status bar -----
    if (busy || st.phase == DownloadState::Phase::Done
             || st.phase == DownloadState::Phase::Error
             || st.phase == DownloadState::Phase::Cancelled) {
        ImGui::BeginChild("##active_dl", ImVec2(0, 100.0f), true);
        ImGui::SetWindowFontScale(1.08f);

        ImGui::Text("File: %s", st.filename.c_str());

        float frac = 0.0f;
        char  overlay[96];
        if (st.bytes_total > 0) {
            frac = (float)st.bytes_received / (float)st.bytes_total;
            const double rate = (st.elapsed_seconds > 0.01)
                                ? st.bytes_received / st.elapsed_seconds : 0.0;
            const double rem  = (rate > 0.0)
                                ? (st.bytes_total - st.bytes_received) / rate : 0.0;
            std::snprintf(overlay, sizeof(overlay),
                "%s / %s  --  %.1f MB/s  --  %.0fs left",
                human_bytes(st.bytes_received).c_str(),
                human_bytes(st.bytes_total).c_str(),
                rate / 1048576.0, rem);
        } else {
            std::snprintf(overlay, sizeof(overlay), "%s downloaded (%.1fs)",
                          human_bytes(st.bytes_received).c_str(), st.elapsed_seconds);
        }
        gradient_progress_bar(frac, ImVec2(-FLT_MIN, 22.0f), overlay);

        if (busy) {
            if (ImGui::Button("Cancel download", ImVec2(160.0f, 26.0f))) downloader_.cancel();
        }
        if (st.phase == DownloadState::Phase::Done) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x9B, 0xE8, 0xA6, 0xFF));
            ImGui::Text("Done. Saved to %s", st.out_path.c_str());
            ImGui::PopStyleColor();
            // Refresh dropdown so the new model appears immediately.
            scan_models();
        }
        if (st.phase == DownloadState::Phase::Error) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0x6A, 0x6A, 0xFF));
            ImGui::TextWrapped("ERR: %s", st.error.c_str());
            ImGui::PopStyleColor();
        }
        if (st.phase == DownloadState::Phase::Cancelled) {
            ImGui::TextDisabled("Cancelled. Partial file removed.");
        }
        ImGui::EndChild();
        ImGui::Spacing();
    }

    // ----- Catalog table -----
    ImGui::TextDisabled(
        "Click DOWNLOAD to fetch a model straight into your models/ folder. "
        "Downloads run in the background; the dropdown auto-refreshes when done.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##catalog", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner |
                          ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Action",      ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Tier",        ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Model",       ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& m : CATALOG) {
            ImGui::TableNextRow();
            ImGui::PushID(m.filename);

            // ----- Action column -----
            ImGui::TableNextColumn();
            const bool installed = model_file_exists(m.filename);
            if (installed) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x9B, 0xE8, 0xA6, 0xFF));
                ImGui::Text("Installed");
                ImGui::PopStyleColor();
            } else if (busy) {
                ImGui::TextDisabled("(busy)");
            } else {
                if (ImGui::Button("Download")) {
                    const std::string out = std::string("models/") + m.filename;
                    downloader_.start(m.url, out);
                }
            }

            // ----- Tier column -----
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, tier_color(m.tier));
            ImGui::TextUnformatted(m.tier);
            ImGui::PopStyleColor();

            // ----- Model name + size -----
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.filename);
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", m.size_label);

            // ----- Description -----
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m.description);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}


}  // namespace usersync
