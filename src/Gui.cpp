#include "Gui.h"

#include "FileDialog.h"
#include "LrcWriter.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

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

// ---------- userinfo.lol brand palette (pink -> purple ramp), same as
// upstream usersync. Matches the website's
// --gradient: linear-gradient(135deg, #e879f9, #a855f7).
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

// Primary CTA: gradient fill, white text, soft hover lift.
bool gradient_button(const char* label, ImVec2 size = ImVec2(0, 0), bool disabled = false) {
    ImGui::PushID(label);
    ImGuiStyle& style = ImGui::GetStyle();

    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x <= 0) size.x = label_size.x + style.FramePadding.x * 4.0f;
    if (size.y <= 0) size.y = label_size.y + style.FramePadding.y * 2.0f;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (disabled) ImGui::BeginDisabled();
    ImGui::InvisibleButton("##gradbtn", size);
    const bool hovered = !disabled && ImGui::IsItemHovered();
    const bool active  = !disabled && ImGui::IsItemActive();
    const bool clicked = !disabled && ImGui::IsItemClicked();
    if (disabled) ImGui::EndDisabled();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(pos.x + size.x, pos.y + size.y);

    if (disabled) {
        dl->AddRectFilled(pos, br, IM_COL32(50, 50, 54, 255), style.FrameRounding);
    } else {
        fill_ig_gradient(dl, pos, br);
        if (hovered) {
            dl->AddRectFilled(pos, br,
                              IM_COL32(255, 255, 255, active ? 12 : 28),
                              style.FrameRounding);
        }
    }
    dl->AddRect(pos, br, IM_COL32(255, 255, 255, disabled ? 20 : 40), style.FrameRounding, 0, 1.0f);

    const ImVec2 text_pos(
        pos.x + (size.x - label_size.x) * 0.5f,
        pos.y + (size.y - label_size.y) * 0.5f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos,
                disabled ? IM_COL32(255, 255, 255, 90) : IM_COL32(255, 255, 255, 255), label);

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

// Section header: small gradient stripe + uppercase label + thin separator.
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

void form_row(const char* label, float label_col_w = 110.0f) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_col_w);
}

void form_text(const char* label, char* buf, size_t buf_size) {
    ImGui::PushID(label);
    form_row(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##v", buf, buf_size);
    ImGui::PopID();
}

// Form row: label on the left, InputText + Browse button on the right.
bool form_browse(const char* label, char* buf, size_t buf_size, const char* btn_label,
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

std::string default_lrc_path_for(const std::string& audio) {
    std::filesystem::path p(audio.empty() ? "output.mp3" : audio);
    p.replace_extension(".lrc");
    return p.string();
}

// mm:ss.cc, used for both the transport clock and the tapped-line list.
std::string fmt_clock(double t) {
    if (t < 0) t = 0;
    const int total_cs = static_cast<int>(std::lround(t * 100.0));
    const int mm = total_cs / 6000;
    const int ss = (total_cs / 100) % 60;
    const int cs = total_cs % 100;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%02d", mm, ss, cs);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------

Gui::Gui(App& app) : app_(app) {}

void Gui::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "usersync_forked_imgui.ini";
    // Deliberately NOT ImGuiConfigFlags_NavEnableKeyboard: ImGui's nav system
    // binds Enter/Space to "activate the focused widget", which would fight
    // with this app's global Enter=tap / Space=play hotkeys (e.g. Enter
    // firing whatever button happens to have nav focus instead of tapping).

    apply_style();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    std::snprintf(out_path_buf_, sizeof(out_path_buf_), "%s", "");
}

void Gui::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Gui::apply_style() {
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

    const ImVec4 PINK_LIGHT   = rgb(0xF0, 0xAB, 0xFC);
    const ImVec4 PINK         = rgb(0xE8, 0x79, 0xF9);
    const ImVec4 PINK_DARK    = rgb(0xD9, 0x46, 0xEF);
    const ImVec4 PURPLE       = rgb(0xA8, 0x55, 0xF7);
    const ImVec4 BG           = rgb(0x0D, 0x00, 0x15);
    const ImVec4 BG2          = rgb(0x11, 0x00, 0x18);
    const ImVec4 BG3          = rgb(0x16, 0x00, 0x1F);

    c[ImGuiCol_Text]                 = rgb(255, 255, 255);
    c[ImGuiCol_TextDisabled]         = rgb(255, 255, 255, 102);
    c[ImGuiCol_WindowBg]             = BG;
    c[ImGuiCol_ChildBg]              = BG2;
    c[ImGuiCol_PopupBg]              = BG;
    c[ImGuiCol_Border]               = rgb(0xD9, 0x46, 0xEF, 46);
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = BG3;
    c[ImGuiCol_FrameBgHovered]       = rgb(0xD9, 0x46, 0xEF, 26);
    c[ImGuiCol_FrameBgActive]        = rgb(0xD9, 0x46, 0xEF, 51);

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

    c[ImGuiCol_Button]               = rgb(0xD9, 0x46, 0xEF, 26);
    c[ImGuiCol_ButtonHovered]        = rgb(0xD9, 0x46, 0xEF, 64);
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

    (void)PINK_LIGHT;
}

void Gui::sync_player_to_audio_path() {
    if (audio_path_buf_[0] == 0) return;
    if (player_.is_loaded() && player_.loaded_path() == audio_path_buf_) return;
    player_load_error_.clear();
    if (!player_.load(audio_path_buf_, player_load_error_)) {
        // Keep the error around so the Sync tab can show it.
    }
}

void Gui::set_status(const std::string& msg, bool is_error) {
    status_msg_     = msg;
    status_is_error_ = is_error;
}

void Gui::do_tap() {
    if (!player_.is_loaded()) return;
    if (app_.finished()) return;
    app_.tap(player_.position_seconds());
}

bool Gui::do_save_lrc(std::string& err) {
    if (out_path_buf_[0] == 0) {
        const auto def = default_lrc_path_for(audio_path_buf_);
        std::snprintf(out_path_buf_, sizeof(out_path_buf_), "%s", def.c_str());
    }
    app_.meta().title  = title_buf_;
    app_.meta().artist = artist_buf_;
    app_.meta().album  = album_buf_;
    const auto lrc = format_line_lrc(app_.lines(), app_.meta());
    return write_file(out_path_buf_, lrc, err);
}

bool Gui::do_save_project(std::string& err) {
    std::string path = project_path_;
    if (!save_project_dialog(path, path.empty() ? "song.usfp" : path)) return false;
    app_.meta().title  = title_buf_;
    app_.meta().artist = artist_buf_;
    app_.meta().album  = album_buf_;
    if (!app_.save_project(path, audio_path_buf_, err)) return false;
    project_path_ = path;
    return true;
}

bool Gui::do_load_project(std::string& err) {
    std::string path;
    if (!open_project_dialog(path)) return false;
    std::string audio;
    if (!app_.load_project(path, audio, err)) return false;
    project_path_ = path;
    std::snprintf(audio_path_buf_, sizeof(audio_path_buf_), "%s", audio.c_str());
    std::snprintf(title_buf_, sizeof(title_buf_), "%s", app_.meta().title.c_str());
    std::snprintf(artist_buf_, sizeof(artist_buf_), "%s", app_.meta().artist.c_str());
    std::snprintf(album_buf_, sizeof(album_buf_), "%s", app_.meta().album.c_str());
    // Rebuild the editable lyrics textbox from the loaded lines so Setup
    // still shows exactly what's being synced.
    lyrics_buf_.clear();
    for (size_t i = 0; i < app_.lines().size(); ++i) {
        lyrics_buf_ += app_.lines()[i].text;
        if (i + 1 < app_.lines().size()) lyrics_buf_ += '\n';
    }
    sync_player_to_audio_path();
    return true;
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
    ImGui::Begin("##usersync_forked_root", nullptr, root_flags);
    ImGui::PopStyleVar(3);

    ImGui::SetWindowFontScale(1.08f);

    // Global hotkeys, suppressed while any text field has focus:
    //   Space     = play/pause
    //   Enter     = tap the current line
    //   Backspace = undo the last tap
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool typing = io.WantTextInput;
        if (!typing && player_.is_loaded()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) player_.toggle();
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                do_tap();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) app_.untap_last();
        }
    }

    draw_title_bar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild("##body_pad", ImVec2(0, 0), false);
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.08f);

    const float footer_h = 60.0f;
    const float body_h   = std::max(160.0f,
        ImGui::GetContentRegionAvail().y - footer_h - ImGui::GetStyle().ItemSpacing.y);

    ImGui::BeginChild("##body", ImVec2(0, body_h), false);
    ImGui::SetWindowFontScale(1.08f);
    if (ImGui::BeginTabBar("##main_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Setup"))  { draw_setup_tab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Sync"))   { draw_sync_tab();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Editor")) { draw_editor_tab(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    draw_footer();

    ImGui::EndChild();
    ImGui::End();
}

void Gui::draw_title_bar() {
    const float h = 56.0f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + ImGui::GetContentRegionAvail().x, a.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    fill_ig_gradient(dl, a, b);

    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - 12.0f), b,
                                IM_COL32(0, 0, 0, 0),  IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 60));

    ImGui::SetCursorScreenPos(ImVec2(a.x + 18.0f, a.y + 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
    ImGui::SetWindowFontScale(1.65f);
    ImGui::Text("usersync-forked");
    ImGui::SetWindowFontScale(0.95f);
    ImGui::SetCursorScreenPos(ImVec2(a.x + 18.0f, a.y + 32.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 200));
    ImGui::TextUnformatted("tap-to-sync per-line .lrc generator");
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.08f);

    const char* tag = "manual tap sync  ::  first-word timing";
    const ImVec2 ts = ImGui::CalcTextSize(tag);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(b.x - ts.x - 22.0f, a.y + (h - ts.y) * 0.5f),
                IM_COL32(255, 255, 255, 200), tag);

    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y),
                IM_COL32(10, 10, 12, 255), 1.0f);

    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
    ImGui::Dummy(ImVec2(0, 2.0f));
}

// ---------------------------------------------------------------------------
// Setup tab
// ---------------------------------------------------------------------------
void Gui::draw_setup_tab() {
    section_header("AUDIO + OUTPUT");

    if (form_browse("Audio file", audio_path_buf_, sizeof(audio_path_buf_), "Browse...",
                    [](std::string& p) { return open_audio_file_dialog(p); })) {
        sync_player_to_audio_path();
        if (out_path_buf_[0] == 0) {
            const auto def = default_lrc_path_for(audio_path_buf_);
            std::snprintf(out_path_buf_, sizeof(out_path_buf_), "%s", def.c_str());
        }
    }
    if (!player_load_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0x6A, 0x6A, 0xFF));
        ImGui::TextWrapped("%s", player_load_error_.c_str());
        ImGui::PopStyleColor();
    }

    form_browse("Output .lrc", out_path_buf_, sizeof(out_path_buf_), "Save as...",
               [](std::string& p) { return save_lrc_file_dialog(p); });

    ImGui::Spacing();
    section_header("METADATA (optional)");
    form_text("Title",  title_buf_,  sizeof(title_buf_));
    form_text("Artist", artist_buf_, sizeof(artist_buf_));
    form_text("Album",  album_buf_,  sizeof(album_buf_));

    ImGui::Spacing();
    section_header("LYRICS");
    ImGui::TextDisabled("One line per .lrc line. Paste the whole song, then hit Apply below");
    ImGui::TextDisabled("to start (or restart) a tap-sync session over the Sync tab.");

    ImGui::Spacing();
    if (ImGui::Button("Load .txt...")) {
        std::string path;
        if (open_lyrics_file_dialog(path) && !path.empty()) {
            std::ifstream f(path, std::ios::binary);
            if (f) {
                std::stringstream ss;
                ss << f.rdbuf();
                lyrics_buf_ = ss.str();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) lyrics_buf_.clear();
    ImGui::SameLine();
    const size_t line_count = std::count(lyrics_buf_.begin(), lyrics_buf_.end(), '\n')
                              + (lyrics_buf_.empty() ? 0 : 1);
    ImGui::TextDisabled("%zu chars, ~%zu lines", lyrics_buf_.size(), line_count);

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
    if (lyrics_buf_.capacity() < 32) lyrics_buf_.reserve(32);
    ImGui::InputTextMultiline(
        "##lyrics", lyrics_buf_.data(), lyrics_buf_.capacity() + 1,
        ImVec2(-FLT_MIN, std::max(120.0f, ImGui::GetContentRegionAvail().y - 46.0f)),
        ImGuiInputTextFlags_CallbackResize, Cb::resize, &lyrics_buf_);

    const int tapped = app_.tapped_count();
    if (tapped > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xFF, 0xCC, 0x70, 0xFF));
        ImGui::TextWrapped("Applying will reset all %d tapped timestamp(s) in the current session.", tapped);
        ImGui::PopStyleColor();
    }
    if (gradient_button("APPLY LYRICS -> START TAP SYNC", ImVec2(-FLT_MIN, 30.0f), lyrics_buf_.empty())) {
        app_.set_lyrics(lyrics_buf_);
    }
}

// ---------------------------------------------------------------------------
// Sync tab -- the actual tap-along interface
// ---------------------------------------------------------------------------
void Gui::draw_sync_tab() {
    if (app_.lines().empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Paste lyrics and hit \"Apply lyrics\" on the Setup tab first.");
        return;
    }
    if (!player_.is_loaded()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Pick an audio file on the Setup tab first.");
        return;
    }

    // --- transport ---
    section_header("PLAYBACK");
    if (gradient_button(player_.is_playing() ? "PAUSE" : "PLAY", ImVec2(90.0f, 30.0f))) {
        player_.toggle();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(70.0f, 30.0f))) player_.stop();
    ImGui::SameLine();
    ImGui::TextDisabled("%s / %s", fmt_clock(player_.position_seconds()).c_str(),
                        fmt_clock(player_.duration_seconds()).c_str());

    float pos = static_cast<float>(player_.position_seconds());
    const float dur = static_cast<float>(std::max(0.001, player_.duration_seconds()));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##seek", &pos, 0.0f, dur, "")) {
        player_.seek_seconds(pos);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- tap zone ---
    if (app_.finished()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x9B, 0xE8, 0xA6, 0xFF));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextWrapped("All lines tapped!");
        ImGui::SetWindowFontScale(1.08f);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Head to the Editor tab to nudge any line, then save the .lrc.");
    } else {
        ImGui::TextDisabled("NOW SINGING (tap when this word starts):");
        ImGui::SetWindowFontScale(1.9f);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(app_.lines()[app_.cursor()].text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetWindowFontScale(1.08f);

        // Peek at the next couple of lines, dimmed.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 110));
        for (int i = app_.cursor() + 1, shown = 0; i < static_cast<int>(app_.lines().size()) && shown < 2; ++i) {
            if (app_.lines()[i].text.empty()) continue;
            ImGui::TextWrapped("%s", app_.lines()[i].text.c_str());
            ++shown;
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        if (gradient_button("TAP  (Enter)", ImVec2(-FLT_MIN, 46.0f))) do_tap();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Enter = tap current line   |   Space = play/pause   |   Backspace = undo last tap");

    ImGui::Spacing();
    const int total  = static_cast<int>(app_.lines().size());
    const int tapped = app_.tapped_count();
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%d / %d lines tapped", tapped, std::max(1, total));
    gradient_progress_bar(total > 0 ? static_cast<float>(tapped) / total : 0.0f,
                          ImVec2(-FLT_MIN, 20.0f), overlay);

    ImGui::Spacing();
    ImGui::Separator();

    // --- scrolling line list ---
    ImGui::BeginChild("##sync_lines", ImVec2(0, 0), true);
    for (size_t i = 0; i < app_.lines().size(); ++i) {
        const auto& l = app_.lines()[i];
        const bool is_current = static_cast<int>(i) == app_.cursor() && !app_.finished();
        if (l.text.empty()) { ImGui::Spacing(); continue; }

        if (is_current) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xF0, 0xAB, 0xFC, 0xFF));
            ImGui::Text(">  ");
            ImGui::SameLine(0.0f, 0.0f);
        } else if (l.tapped()) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0x9B, 0xE8, 0xA6, 0xFF));
            ImGui::Text("%s  ", fmt_clock(l.t_start).c_str());
            ImGui::SameLine(0.0f, 0.0f);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 90));
            ImGui::Text("--:--.--  ");
            ImGui::SameLine(0.0f, 0.0f);
        }
        ImGui::TextUnformatted(l.text.c_str());
        ImGui::PopStyleColor();

        if (is_current && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(0.3f);
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Editor tab -- nudge tapped timestamps, jump-play, retap-from-here
// ---------------------------------------------------------------------------
void Gui::draw_editor_tab() {
    section_header("LINE EDITOR");

    if (app_.lines().empty()) {
        ImGui::TextDisabled("Nothing to edit yet -- apply lyrics on the Setup tab first.");
        return;
    }

    ImGui::TextDisabled("Nudge a line's timestamp, jump playback to it to check the sync,");
    ImGui::TextDisabled("or send \"retap from here\" back to the Sync tab to redo the rest.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##editor", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner |
                          ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",       ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Line",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time",    ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Nudge",   ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < app_.lines().size(); ++i) {
            auto& line = app_.lines()[i];
            if (line.text.empty()) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetColumnWidth());
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopTextWrapPos();

            ImGui::TableSetColumnIndex(2);
            if (line.tapped()) {
                ImGui::TextUnformatted(fmt_clock(line.t_start).c_str());
            } else {
                ImGui::TextDisabled("--:--.--");
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::BeginDisabled(!line.tapped());
            if (ImGui::SmallButton("-500")) line.t_start = std::max(0.0, line.t_start - 0.5);
            ImGui::SameLine();
            if (ImGui::SmallButton("-100")) line.t_start = std::max(0.0, line.t_start - 0.1);
            ImGui::SameLine();
            if (ImGui::SmallButton("+100")) line.t_start += 0.1;
            ImGui::SameLine();
            if (ImGui::SmallButton("+500")) line.t_start += 0.5;
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(!line.tapped() || !player_.is_loaded());
            if (ImGui::SmallButton("Jump+Play")) {
                player_.seek_seconds(line.t_start);
                player_.play();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Retap here")) {
                app_.set_cursor(static_cast<int>(i));
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// Footer -- status line + save actions
// ---------------------------------------------------------------------------
void Gui::draw_footer() {
    ImGui::BeginChild("footer", ImVec2(0, 0), true);
    ImGui::SetWindowFontScale(1.08f);

    const float avail   = ImGui::GetContentRegionAvail().x;
    const float right_w = std::min(avail * 0.55f, 460.0f);
    const float left_w  = avail - right_w - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("##foot_l", ImVec2(left_w, 0), false);
    ImGui::SetWindowFontScale(1.08f);
    if (!status_msg_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, status_is_error_
                              ? IM_COL32(0xFF, 0x6A, 0x6A, 0xFF)
                              : IM_COL32(0x9B, 0xE8, 0xA6, 0xFF));
        ImGui::TextWrapped("%s", status_msg_.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%d / %d lines tapped%s", app_.tapped_count(),
                            static_cast<int>(app_.lines().size()),
                            out_path_buf_[0] ? (std::string("  ->  ") + out_path_buf_).c_str() : "");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##foot_r", ImVec2(0, 0), false);
    ImGui::SetWindowFontScale(1.08f);

    if (ImGui::Button("Load project...", ImVec2(140.0f, 28.0f))) {
        std::string err;
        if (do_load_project(err)) set_status("Loaded " + project_path_, false);
        else if (!err.empty())    set_status(err, true);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(app_.lines().empty());
    if (ImGui::Button("Save project...", ImVec2(140.0f, 28.0f))) {
        std::string err;
        if (do_save_project(err)) set_status("Saved " + project_path_, false);
        else if (!err.empty())    set_status(err, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (gradient_button("SAVE  .LRC", ImVec2(150.0f, 28.0f), app_.tapped_count() == 0)) {
        std::string err;
        if (do_save_lrc(err)) set_status("Wrote " + std::string(out_path_buf_), false);
        else                  set_status(err, true);
    }

    ImGui::EndChild();

    ImGui::EndChild();
}

}  // namespace usersync
