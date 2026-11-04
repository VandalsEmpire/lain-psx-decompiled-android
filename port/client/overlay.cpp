/* In-game overlay: toasts, subtitles and the F1 settings menu, drawn with Dear ImGui
 * on the game window's GL context (PsyCross's). ImGui's OpenGL3 backend saves
 * and restores the GL state it touches, so PsyCross's rendering is unaffected. */
#include "overlay.h"

#include <algorithm>

#include <SDL.h>
#include <float.h>
#include <cmath>
#include <string.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "modrt.h"
#include "mods.h"
#include "packs.h"

#include <string>
#include <utility>
#include <vector>

static bool s_ready;
static char s_text[256];
static Uint64 s_until;
static char s_sub[1024];
static char s_font_path[1024];
static ImFont *s_sub_font;
static std::vector<std::pair<std::string, ImFont *>> s_loaded_fonts;
static const float SUB_FONT_PX = 48.0f; /* rasterised size; scaled to the window */
static bool s_font_dirty;

/* The game's hidden stats (see the Cheats tab), read live from its memory. */
extern "C" short g_stat_tokimeki, g_stat_harumage, g_stat_gakkuri, g_screensaver_count;

/* F1 menu */
static bool s_menu;
static bool s_menu_focus; /* put keyboard/gamepad focus on the first control */
static Settings *s_settings;
static OverlayApplyFn s_apply;
static std::vector<PackOption> s_text_packs, s_voice_packs;
static Uint64 s_mouse_moved_at;  /* last mouse motion (performance counter) */
static int s_rebind = -1;       /* button waiting for a key, or -1 */
static bool s_keys_changed;

#if defined(__ANDROID__)
static void set_menu(bool open);
extern "C" void PsyX_Pad_SetTouchButtons(unsigned short active_low_mask);

struct TouchFinger {
    SDL_FingerID id = 0;
    bool active = false;
    unsigned short mask = 0;
};
static TouchFinger s_touch_fingers[10];
static unsigned short s_touch_mask = 0xffff;

static void clear_touch_controls(void) {
    for (TouchFinger &finger : s_touch_fingers) {
        finger.active = false;
        finger.mask = 0;
    }
    s_touch_mask = 0xffff;
    PsyX_Pad_SetTouchButtons(s_touch_mask);
}

static float clamp_touch(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

static ImVec2 touch_screen_point(float x, float y) {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    return ImVec2(vp->WorkPos.x + x * vp->WorkSize.x,
                  vp->WorkPos.y + y * vp->WorkSize.y);
}

struct TouchLayout {
    ImVec2 dpad[4];       // up, down, left, right
    ImVec2 face[4];       // triangle, square, circle, cross
    ImVec2 l2, l1, r1, r2;
    ImVec2 select, start, menu;
    ImVec2 dpad_center, face_center;
    float dpad_half;
    float face_radius;
    float shoulder_half_w;
    float shoulder_half_h;
    float pill_half_w;
    float pill_half_h;
};

static bool s_touch_edit_layout;
static bool s_layout_drag_active;
static SDL_FingerID s_layout_drag_finger;
static int s_layout_drag_item = -1;

static void reset_touch_layout(Settings *s) {
    static const float defaults[14][2] = {
        {0.150f, 0.674f}, {0.150f, 0.836f}, {0.105f, 0.755f}, {0.195f, 0.755f},
        {0.850f, 0.670f}, {0.803f, 0.755f}, {0.897f, 0.755f}, {0.850f, 0.840f},
        {0.115f, 0.190f}, {0.295f, 0.190f}, {0.705f, 0.190f}, {0.885f, 0.190f},
        {0.435f, 0.910f}, {0.565f, 0.910f},
    };
    memcpy(s->touch_layout, defaults, sizeof defaults);
    s->touch_scale = 1.0f;
    settings_save(s);
}

static TouchLayout touch_layout(void) {
    const float h = ImGui::GetMainViewport()->WorkSize.y;
    const float scale = s_settings ? clamp_touch(s_settings->touch_scale, 0.70f, 1.55f) : 1.0f;
    TouchLayout l{};
    const float (*p)[2] = s_settings ? s_settings->touch_layout : nullptr;
    static const float defaults[14][2] = {
        {0.150f, 0.674f}, {0.150f, 0.836f}, {0.105f, 0.755f}, {0.195f, 0.755f},
        {0.850f, 0.670f}, {0.803f, 0.755f}, {0.897f, 0.755f}, {0.850f, 0.840f},
        {0.115f, 0.190f}, {0.295f, 0.190f}, {0.705f, 0.190f}, {0.885f, 0.190f},
        {0.435f, 0.910f}, {0.565f, 0.910f},
    };
    ImVec2 *points[14] = {
        &l.dpad[0], &l.dpad[1], &l.dpad[2], &l.dpad[3],
        &l.face[0], &l.face[1], &l.face[2], &l.face[3],
        &l.l2, &l.l1, &l.r1, &l.r2, &l.select, &l.start,
    };
    for (int i = 0; i < 14; i++) {
        const float x = p ? p[i][0] : defaults[i][0];
        const float y = p ? p[i][1] : defaults[i][1];
        *points[i] = touch_screen_point(x, y);
    }
    for (int i = 0; i < 4; i++) {
        l.dpad_center.x += l.dpad[i].x * 0.25f;
        l.dpad_center.y += l.dpad[i].y * 0.25f;
        l.face_center.x += l.face[i].x * 0.25f;
        l.face_center.y += l.face[i].y * 0.25f;
    }
    l.dpad_half = clamp_touch(h * 0.052f, 20.0f, 48.0f) * scale;
    l.face_radius = clamp_touch(h * 0.055f, 23.0f, 52.0f) * scale;
    l.shoulder_half_w = clamp_touch(h * 0.070f, 29.0f, 66.0f) * scale;
    l.shoulder_half_h = clamp_touch(h * 0.040f, 17.0f, 32.0f) * scale;
    l.pill_half_w = clamp_touch(h * 0.092f, 39.0f, 82.0f) * scale;
    l.pill_half_h = clamp_touch(h * 0.030f, 14.0f, 25.0f) * scale;
    l.menu = touch_screen_point(0.925f, 0.075f);
    return l;
}

static bool touch_in_rect(const ImVec2 &p, const ImVec2 &center, float half_w, float half_h) {
    return std::abs(p.x - center.x) <= half_w && std::abs(p.y - center.y) <= half_h;
}

static int touch_layout_target_at(float nx, float ny) {
    const TouchLayout l = touch_layout();
    const ImVec2 p = touch_screen_point(nx, ny);
    const ImVec2 *centers[14] = {
        &l.dpad[0], &l.dpad[1], &l.dpad[2], &l.dpad[3],
        &l.face[0], &l.face[1], &l.face[2], &l.face[3],
        &l.l2, &l.l1, &l.r1, &l.r2, &l.select, &l.start,
    };
    const float radii[14] = {
        l.dpad_half * 1.65f, l.dpad_half * 1.65f, l.dpad_half * 1.65f, l.dpad_half * 1.65f,
        l.face_radius * 1.65f, l.face_radius * 1.65f, l.face_radius * 1.65f, l.face_radius * 1.65f,
        l.shoulder_half_w, l.shoulder_half_w, l.shoulder_half_w, l.shoulder_half_w,
        l.pill_half_w * 0.60f, l.pill_half_w * 0.60f,
    };
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < 14; i++) {
        const float dx = p.x - centers[i]->x, dy = p.y - centers[i]->y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= radii[i] * radii[i] && (best < 0 || d2 < best_d2)) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

static void move_touch_layout_item(int item, float nx, float ny) {
    if (!s_settings || item < 0 || item >= 14) return;
    /* Each visible pad element is independently movable over almost the full screen. */
    s_settings->touch_layout[item][0] = clamp_touch(nx, 0.035f, 0.965f);
    s_settings->touch_layout[item][1] = clamp_touch(ny, 0.065f, 0.935f);
}

static void draw_touch_edit_anchor(ImDrawList *draw, ImVec2 p, const char *label, float scale) {
    const float r = 14.0f * scale;
    draw->AddCircleFilled(p, r, IM_COL32(0, 190, 205, 185), 24);
    draw->AddCircle(p, r, IM_COL32(205, 255, 255, 245), 24, 2.0f);
    draw->AddLine(ImVec2(p.x - r * 0.55f, p.y), ImVec2(p.x + r * 0.55f, p.y), IM_COL32(255,255,255,230), 2.0f);
    draw->AddLine(ImVec2(p.x, p.y - r * 0.55f), ImVec2(p.x, p.y + r * 0.55f), IM_COL32(255,255,255,230), 2.0f);
    const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(13.0f * scale, FLT_MAX, 0.0f, label);
    draw->AddRectFilled(ImVec2(p.x - sz.x * 0.5f - 6.0f, p.y - r - sz.y - 8.0f),
                         ImVec2(p.x + sz.x * 0.5f + 6.0f, p.y - r - 3.0f),
                         IM_COL32(0, 0, 0, 190), 5.0f);
    draw->AddText(ImGui::GetFont(), 13.0f * scale,
                  ImVec2(p.x - sz.x * 0.5f, p.y - r - sz.y - 5.0f),
                  IM_COL32(230, 250, 255, 255), label);
}

static unsigned short touch_button_at(float nx, float ny) {
    const TouchLayout l = touch_layout();
    const ImVec2 p = touch_screen_point(nx, ny);
    unsigned short mask = 0;
    const unsigned short dpad_bits[4] = {0x0010, 0x0040, 0x0080, 0x0020};
    for (int i = 0; i < 4; i++) {
        if (touch_in_rect(p, l.dpad[i], l.dpad_half, l.dpad_half)) mask |= dpad_bits[i];
    }
    const unsigned short face_bits[4] = {0x1000, 0x8000, 0x2000, 0x4000};
    for (int i = 0; i < 4; i++) {
        const float dx = p.x - l.face[i].x, dy = p.y - l.face[i].y;
        const float hit_r = l.face_radius * 1.18f;
        if (dx * dx + dy * dy <= hit_r * hit_r) mask |= face_bits[i];
    }
    if (touch_in_rect(p, l.l2, l.shoulder_half_w, l.shoulder_half_h)) mask |= 0x0100;
    if (touch_in_rect(p, l.l1, l.shoulder_half_w, l.shoulder_half_h)) mask |= 0x0400;
    if (touch_in_rect(p, l.r1, l.shoulder_half_w, l.shoulder_half_h)) mask |= 0x0800;
    if (touch_in_rect(p, l.r2, l.shoulder_half_w, l.shoulder_half_h)) mask |= 0x0200;
    if (touch_in_rect(p, l.select, l.pill_half_w, l.pill_half_h)) mask |= 0x0001;
    if (touch_in_rect(p, l.start, l.pill_half_w, l.pill_half_h)) mask |= 0x0008;
    return mask;
}

static void recompute_touch_mask(void) {
    unsigned short mask = 0xffff;
    for (const TouchFinger &finger : s_touch_fingers) {
        if (finger.active) mask &= (unsigned short)~finger.mask;
    }
    s_touch_mask = mask;
    PsyX_Pad_SetTouchButtons(mask);
}

static void draw_touch_pill(ImDrawList *draw, ImVec2 center, float half_w, float half_h,
                            const char *label, unsigned short bit, float font_scale = 0.34f) {
    const bool pressed = bit != 0 && (s_touch_mask & bit) == 0;
    const ImU32 fill = pressed ? IM_COL32(50, 175, 190, 175) : IM_COL32(12, 20, 30, 120);
    const ImU32 edge = pressed ? IM_COL32(125, 235, 240, 235) : IM_COL32(155, 185, 200, 135);
    const float rounding = half_h * 0.82f;
    draw->AddRectFilled(ImVec2(center.x - half_w, center.y - half_h),
                        ImVec2(center.x + half_w, center.y + half_h), fill, rounding);
    draw->AddRect(ImVec2(center.x - half_w, center.y - half_h),
                  ImVec2(center.x + half_w, center.y + half_h), edge, rounding, 0, 1.5f);
    const float font_size = std::max(10.0f, half_h * font_scale * 2.0f);
    const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);
    draw->AddText(ImGui::GetFont(), font_size,
                  ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f),
                  IM_COL32(235, 245, 250, 235), label);
}

static void draw_dpad_arrow(ImDrawList *draw, ImVec2 center, float half, int direction, bool pressed) {
    const ImU32 color = pressed ? IM_COL32(245, 255, 255, 255) : IM_COL32(205, 220, 230, 220);
    const float length = half * 0.55f;
    const float wing = half * 0.27f;
    ImVec2 a, tip, b, c;
    if (direction == 0) { // up
        a = ImVec2(center.x, center.y + length * 0.45f);
        tip = ImVec2(center.x, center.y - length * 0.55f);
        b = ImVec2(center.x - wing, center.y - length * 0.12f);
        c = ImVec2(center.x + wing, center.y - length * 0.12f);
    } else if (direction == 1) { // down
        a = ImVec2(center.x, center.y - length * 0.45f);
        tip = ImVec2(center.x, center.y + length * 0.55f);
        b = ImVec2(center.x - wing, center.y + length * 0.12f);
        c = ImVec2(center.x + wing, center.y + length * 0.12f);
    } else if (direction == 2) { // left
        a = ImVec2(center.x + length * 0.45f, center.y);
        tip = ImVec2(center.x - length * 0.55f, center.y);
        b = ImVec2(center.x - length * 0.12f, center.y - wing);
        c = ImVec2(center.x - length * 0.12f, center.y + wing);
    } else { // right
        a = ImVec2(center.x - length * 0.45f, center.y);
        tip = ImVec2(center.x + length * 0.55f, center.y);
        b = ImVec2(center.x + length * 0.12f, center.y - wing);
        c = ImVec2(center.x + length * 0.12f, center.y + wing);
    }
    draw->AddLine(a, tip, color, 3.0f);
    draw->AddLine(tip, b, color, 3.0f);
    draw->AddLine(tip, c, color, 3.0f);
}

static void draw_dpad_cap(ImDrawList *draw, ImVec2 center, float half, const char *id,
                          unsigned short bit, int direction) {
    const bool pressed = (s_touch_mask & bit) == 0;
    const ImU32 fill = pressed ? IM_COL32(55, 190, 205, 205) : IM_COL32(17, 27, 39, 205);
    const ImU32 edge = pressed ? IM_COL32(135, 245, 250, 245) : IM_COL32(135, 175, 195, 180);
    draw->AddRectFilled(ImVec2(center.x - half, center.y - half),
                        ImVec2(center.x + half, center.y + half), fill, half * 0.25f);
    draw->AddRect(ImVec2(center.x - half, center.y - half),
                  ImVec2(center.x + half, center.y + half), edge, half * 0.25f, 0, 1.6f);
    draw_dpad_arrow(draw, center, half, direction, pressed);
    (void)id;
}

static void draw_face_button(ImDrawList *draw, ImVec2 center, float radius,
                             int shape, unsigned short bit, ImU32 symbol_color) {
    const bool pressed = (s_touch_mask & bit) == 0;
    const ImU32 fill = pressed ? IM_COL32(45, 165, 180, 205) : IM_COL32(13, 23, 35, 188);
    const ImU32 edge = pressed ? IM_COL32(145, 245, 250, 245) : IM_COL32(155, 195, 210, 190);
    draw->AddCircleFilled(center, radius, fill, 32);
    draw->AddCircle(center, radius, edge, 32, 1.8f);
    if (pressed) symbol_color = IM_COL32(235, 255, 255, 255);
    const float r = radius * 0.43f;
    if (shape == 0) { // triangle
        draw->AddTriangle(ImVec2(center.x, center.y - r),
                          ImVec2(center.x - r * 0.88f, center.y + r * 0.70f),
                          ImVec2(center.x + r * 0.88f, center.y + r * 0.70f),
                          symbol_color, 2.7f);
    } else if (shape == 1) { // square
        draw->AddRect(ImVec2(center.x - r * 0.78f, center.y - r * 0.78f),
                      ImVec2(center.x + r * 0.78f, center.y + r * 0.78f),
                      symbol_color, 1.0f, 0, 2.7f);
    } else if (shape == 2) { // circle
        draw->AddCircle(center, r * 0.85f, symbol_color, 28, 2.7f);
    } else { // cross
        draw->AddLine(ImVec2(center.x - r * 0.72f, center.y - r * 0.72f),
                      ImVec2(center.x + r * 0.72f, center.y + r * 0.72f), symbol_color, 3.0f);
        draw->AddLine(ImVec2(center.x + r * 0.72f, center.y - r * 0.72f),
                      ImVec2(center.x - r * 0.72f, center.y + r * 0.72f), symbol_color, 3.0f);
    }
}

static void draw_touch_controls(const ImGuiViewport *vp, bool buttons_enabled) {
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const TouchLayout l = touch_layout();
    const float scale = s_settings ? clamp_touch(s_settings->touch_scale, 0.70f, 1.55f) : 1.0f;

    if (buttons_enabled) {
        const float pad_r = clamp_touch(vp->WorkSize.y * 0.178f, 78.0f, 142.0f) * scale;
        const ImU32 pad_fill = IM_COL32(5, 13, 24, 42);
        const ImU32 pad_edge = IM_COL32(110, 160, 185, 55);
        draw->AddCircleFilled(l.dpad_center, pad_r, pad_fill, 48);
        draw->AddCircle(l.dpad_center, pad_r, pad_edge, 48, 1.5f);
        draw->AddCircleFilled(l.face_center, pad_r, pad_fill, 48);
        draw->AddCircle(l.face_center, pad_r, pad_edge, 48, 1.5f);

        draw_dpad_cap(draw, l.dpad[0], l.dpad_half, "up", 0x0010, 0);
        draw_dpad_cap(draw, l.dpad[1], l.dpad_half, "down", 0x0040, 1);
        draw_dpad_cap(draw, l.dpad[2], l.dpad_half, "left", 0x0080, 2);
        draw_dpad_cap(draw, l.dpad[3], l.dpad_half, "right", 0x0020, 3);

        draw_face_button(draw, l.face[0], l.face_radius, 0, 0x1000, IM_COL32(85, 205, 145, 235)); // triangle
        draw_face_button(draw, l.face[1], l.face_radius, 1, 0x8000, IM_COL32(225, 105, 165, 235)); // square
        draw_face_button(draw, l.face[2], l.face_radius, 2, 0x2000, IM_COL32(235, 95, 95, 235)); // circle
        draw_face_button(draw, l.face[3], l.face_radius, 3, 0x4000, IM_COL32(95, 160, 240, 235)); // cross

        draw_touch_pill(draw, l.l2, l.shoulder_half_w, l.shoulder_half_h, "L2", 0x0100, 0.30f);
        draw_touch_pill(draw, l.l1, l.shoulder_half_w, l.shoulder_half_h, "L1", 0x0400, 0.30f);
        draw_touch_pill(draw, l.r1, l.shoulder_half_w, l.shoulder_half_h, "R1", 0x0800, 0.30f);
        draw_touch_pill(draw, l.r2, l.shoulder_half_w, l.shoulder_half_h, "R2", 0x0200, 0.30f);
        draw_touch_pill(draw, l.select, l.pill_half_w, l.pill_half_h, "SELECT", 0x0001, 0.29f);
        draw_touch_pill(draw, l.start, l.pill_half_w, l.pill_half_h, "START", 0x0008, 0.29f);
    }

    if (s_touch_edit_layout) {
        static const char *const names[14] = {
            "UP", "DOWN", "LEFT", "RIGHT", "TRIANGLE", "SQUARE", "CIRCLE", "CROSS",
            "L2", "L1", "R1", "R2", "SELECT", "START",
        };
        const ImVec2 *points[14] = {
            &l.dpad[0], &l.dpad[1], &l.dpad[2], &l.dpad[3],
            &l.face[0], &l.face[1], &l.face[2], &l.face[3],
            &l.l2, &l.l1, &l.r1, &l.r2, &l.select, &l.start,
        };
        for (int i = 0; i < 14; i++) draw_touch_edit_anchor(draw, *points[i], names[i], scale);
        const ImVec2 info(18.0f, 18.0f);
        const char *hint = "EDIT LAYOUT - drag any button independently; DONE saves";
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(16.0f * scale, FLT_MAX, 0.0f, hint);
        draw->AddRectFilled(info, ImVec2(info.x + size.x + 18.0f * scale,
                                         info.y + size.y + 12.0f * scale), IM_COL32(0, 0, 0, 175), 6.0f);
        draw->AddText(ImGui::GetFont(), 16.0f * scale, ImVec2(info.x + 9.0f * scale, info.y + 6.0f * scale),
                      IM_COL32(225, 250, 255, 255), hint);
        const ImVec2 done = touch_screen_point(0.925f, 0.075f);
        draw_touch_pill(draw, done, clamp_touch(vp->WorkSize.y * 0.080f, 38.0f, 70.0f) * scale,
                        clamp_touch(vp->WorkSize.y * 0.034f, 17.0f, 28.0f) * scale,
                        "DONE", 0, 0.30f);
    } else {
        const float menu_half_w = clamp_touch(vp->WorkSize.y * 0.072f, 32.0f, 62.0f) * scale;
        const float menu_half_h = clamp_touch(vp->WorkSize.y * 0.030f, 14.0f, 25.0f) * scale;
        draw_touch_pill(draw, l.menu, menu_half_w, menu_half_h, "MENU", 0, 0.30f);
    }
}

static bool handle_touch_event(const SDL_Event *e) {
    if (!s_ready || !s_settings) return false;
    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP) return false;
    if (s_menu) return false;

    if (s_touch_edit_layout) {
        if (e->type == SDL_FINGERDOWN) {
            const ImVec2 done = touch_screen_point(0.925f, 0.075f);
            const ImVec2 p = touch_screen_point(e->tfinger.x, e->tfinger.y);
            if (touch_in_rect(p, done, 112.0f, 56.0f)) {
                s_touch_edit_layout = false;
                s_layout_drag_active = false;
                s_layout_drag_item = -1;
                settings_save(s_settings);
                return true;
            }
            const int target = touch_layout_target_at(e->tfinger.x, e->tfinger.y);
            if (target >= 0) {
                s_layout_drag_active = true;
                s_layout_drag_finger = e->tfinger.fingerId;
                s_layout_drag_item = target;
                move_touch_layout_item(target, e->tfinger.x, e->tfinger.y);
            }
            return true; /* swallow all touches while editing */
        }
        if (s_layout_drag_active && e->tfinger.fingerId == s_layout_drag_finger) {
            if (e->type == SDL_FINGERUP) {
                s_layout_drag_active = false;
                s_layout_drag_item = -1;
                settings_save(s_settings);
            } else {
                move_touch_layout_item(s_layout_drag_item, e->tfinger.x, e->tfinger.y);
            }
            return true;
        }
        return true;
    }

    if (e->type == SDL_FINGERDOWN) {
        const TouchLayout l = touch_layout();
        const ImVec2 p = touch_screen_point(e->tfinger.x, e->tfinger.y);
        const float menu_scale = s_settings ? clamp_touch(s_settings->touch_scale, 0.70f, 1.55f) : 1.0f;
        const float menu_w = clamp_touch(ImGui::GetMainViewport()->WorkSize.y * 0.085f, 38.0f, 72.0f) * menu_scale;
        const float menu_h = clamp_touch(ImGui::GetMainViewport()->WorkSize.y * 0.040f, 18.0f, 32.0f) * menu_scale;
        // Keep the MENU button available even when the player hides the controls.
        if (touch_in_rect(p, l.menu, menu_w, menu_h)) {
            clear_touch_controls();
            set_menu(true);
            return true;
        }
        if (!s_settings->touch_controls) return false;
        TouchFinger *slot = nullptr;
        for (TouchFinger &finger : s_touch_fingers) {
            if (finger.active && finger.id == e->tfinger.fingerId) { slot = &finger; break; }
            if (!finger.active && !slot) slot = &finger;
        }
        if (!slot) return false;
        slot->id = e->tfinger.fingerId;
        slot->mask = touch_button_at(e->tfinger.x, e->tfinger.y);
        slot->active = slot->mask != 0;
        recompute_touch_mask();
        return slot->active;
    }

    for (TouchFinger &finger : s_touch_fingers) {
        if (finger.active && finger.id == e->tfinger.fingerId) {
            if (e->type == SDL_FINGERUP) {
                finger.active = false;
                finger.mask = 0;
            } else {
                finger.mask = touch_button_at(e->tfinger.x, e->tfinger.y);
            }
            recompute_touch_mask();
            return true;
        }
    }
    return false;
}
#endif

static bool overlay_init(void) {
    SDL_Window *win = SDL_GL_GetCurrentWindow();
    SDL_GLContext gl = SDL_GL_GetCurrentContext();
    if (!win || !gl) {
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableKeyboard |
                      ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4;
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
#if defined(__ANDROID__)
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 150");
#endif
    io.Fonts->AddFontDefault();
    s_font_dirty = true;
    return true;
}

/* The subtitle font can change while playing (a pack chosen in the menu); ImGui
 * 1.92's dynamic atlas accepts new fonts at any time. */
static void load_sub_font(void) {
    /* Each font file is added to the atlas once, however often packs are switched. */
    s_font_dirty = false;
    s_sub_font = nullptr;
    if (!s_font_path[0]) {
        return;
    }
    for (auto &f : s_loaded_fonts) {
        if (f.first == s_font_path) {
            s_sub_font = f.second;
            return;
        }
    }
    s_sub_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(s_font_path, SUB_FONT_PX);
    if (s_sub_font) {
        s_loaded_fonts.emplace_back(s_font_path, s_sub_font);
    }
}

extern "C" void overlay_set_subtitle(const char *text) {
    SDL_strlcpy(s_sub, text ? text : "", sizeof s_sub);
}

extern "C" void overlay_set_font(const char *ttf_path) {
    SDL_strlcpy(s_font_path, ttf_path ? ttf_path : "", sizeof s_font_path);
    s_font_dirty = true;
}

extern "C" void overlay_menu_init(Settings *settings, OverlayApplyFn apply) {
    s_settings = settings;
    s_apply = apply;
}

extern "C" void overlay_session_shutdown(void) {
    /* The second session must not reuse pointers into the previous ImGui atlas
     * or stale BackendRendererUserData. This was the crash on the second Play. */
    if (s_ready && ImGui::GetCurrentContext()) {
        ImGuiIO &io = ImGui::GetIO();
        if (io.BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
        if (io.BackendPlatformUserData) ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    s_ready = false;
    s_loaded_fonts.clear();
    s_sub_font = nullptr;
    s_font_dirty = true;
    s_menu = false;
    s_menu_focus = false;
    s_rebind = -1;
    s_keys_changed = false;
    s_settings = nullptr;
    s_apply = nullptr;
    s_text[0] = 0;
    s_sub[0] = 0;
    s_font_path[0] = 0;
    s_until = 0;
    s_mouse_moved_at = 0;
    s_text_packs.clear();
    s_voice_packs.clear();
#if defined(__ANDROID__)
    clear_touch_controls();
    s_touch_edit_layout = false;
    s_layout_drag_active = false;
    s_layout_drag_item = -1;
#endif
}

extern "C" int overlay_menu_is_open(void) {
    return s_menu;
}

static void set_menu(bool open) {
    if (open == s_menu || !s_settings) {
        return;
    }
    s_menu = open;
    s_rebind = -1;
#if defined(__ANDROID__)
    if (open) clear_touch_controls();
#endif
    if (open) {
        s_menu_focus = true;
        if (s_ready) {
            ImGui::GetIO().ClearInputKeys(); /* nothing carried over from before */
        }
        /* Packs installed while the game runs show up the next time the menu opens. */
        s_text_packs = find_packs("lang", "Japanese (original)");
        s_voice_packs = find_packs("dub", "Japanese (original)");
    } else {
        settings_save(s_settings);
    }
}

extern "C" int overlay_handle_event(const union SDL_Event *ev) {
    const SDL_Event *e = (const SDL_Event *)ev;
#if defined(__ANDROID__)
    if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        clear_touch_controls();
    }
    if (handle_touch_event(e)) return 1;
    if (s_menu && (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERUP || e->type == SDL_FINGERMOTION) && s_ready) {
        ImGui_ImplSDL2_ProcessEvent(e);
        return 1;
    }
#endif
    if (e->type == SDL_MOUSEMOTION) {
        s_mouse_moved_at = SDL_GetPerformanceCounter();
    }
    if (s_menu && s_rebind >= 0 && e->type == SDL_KEYDOWN && !e->key.repeat) {
        SDL_Scancode sc = e->key.keysym.scancode;
        if (sc != SDL_SCANCODE_ESCAPE && sc != SDL_SCANCODE_F1) {
            SDL_strlcpy(s_settings->keys[s_rebind], SDL_GetScancodeName(sc), sizeof s_settings->keys[0]);
            s_keys_changed = true;
        }
        s_rebind = -1; /* Esc/F1 cancel */
        return 1;
    }
    bool toggle = (e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.scancode == SDL_SCANCODE_F1) ||
                  (e->type == SDL_CONTROLLERBUTTONDOWN && e->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE);
    if (toggle) {
        set_menu(!s_menu);
        return 1;
    }
    if (!s_menu) {
        /* Releases still go to the menu, so a key held when it closed isn't stuck down
         * the next time it opens. */
        if (s_ready && (e->type == SDL_KEYUP || e->type == SDL_MOUSEBUTTONUP || e->type == SDL_CONTROLLERBUTTONUP)) {
            ImGui_ImplSDL2_ProcessEvent(e);
        }
        return 0;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        set_menu(false);
        return 1;
    }
    if (s_ready) {
        ImGui_ImplSDL2_ProcessEvent(e);
    }
    /* Keyboard, mouse and text go to the menu; window and device events still
     * reach PsyCross (resizes, controller hotplug). */
    switch (e->type) {
    case SDL_KEYDOWN: case SDL_KEYUP: case SDL_TEXTINPUT: case SDL_TEXTEDITING:
    case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL:
    case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: case SDL_CONTROLLERAXISMOTION:
        return 1;
    default:
        return 0;
    }
}

static bool pack_combo(const char *label, const std::vector<PackOption> &opts, char *code, size_t cap) {
    const char *shown = opts.empty() ? code : opts[0].label.c_str();
    for (const PackOption &o : opts) {
        if (o.code == code) shown = o.label.c_str();
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, shown)) {
        for (const PackOption &o : opts) {
            if (ImGui::Selectable(o.label.c_str(), o.code == code) && o.code != code) {
                SDL_strlcpy(code, o.code.c_str(), cap);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/* LAIN_MENU_DEBUG: log which control has keyboard/gamepad focus (tests). */
static void note_focus(const char *label) {
    static const char *last;
    if (ImGui::IsItemFocused() && last != label && SDL_getenv("LAIN_MENU_DEBUG")) {
        last = label;
        SDL_Log("menu focus: %s (frame %d)", label, ImGui::GetFrameCount());
    }
}

static void draw_menu(const ImGuiViewport *vp) {
    Settings *s = s_settings;
    int apply = 0;
    float ui = std::min(vp->WorkSize.y / 540.0f, vp->WorkSize.x / 640.0f); /* sized to the window */
    ui = ui < 1.0f ? 1.0f : ui;
    const float width = std::min(600.0f * ui, vp->WorkSize.x * 0.98f);
    ImGui::PushFont(nullptr, 15.0f * ui);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, vp->WorkSize.y * 0.95f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    ImGui::Begin("Settings (F1)", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    ImGui::PushItemWidth(-160.0f * ui);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Settings")) {
            ImGui::SeparatorText("Display");
            bool fs = s->fullscreen != 0;
            if (s_menu_focus) {
                ImGui::SetKeyboardFocusHere();
                s_menu_focus = false;
            }
            if (ImGui::Checkbox("Fullscreen", &fs)) {
                s->fullscreen = fs;
                apply |= OVERLAY_APPLY_VIDEO;
            }
            note_focus("Fullscreen");
            ImGui::SameLine(0, 24.0f * ui);
            bool vsync = s->vsync != 0;
            if (ImGui::Checkbox("VSync", &vsync)) {
                s->vsync = vsync;
                apply |= OVERLAY_APPLY_VIDEO;
            }
            note_focus("VSync");
#if defined(__ANDROID__)
            ImGui::TextDisabled("Smooth motion (60 fps) is disabled on Android to avoid rendering artifacts.");
#else
            bool smooth = s->smooth != 0;
            if (ImGui::Checkbox("Smooth motion (60 fps)", &smooth)) {
                s->smooth = smooth;
                apply |= OVERLAY_APPLY_VIDEO;
            }
            note_focus("Smooth motion");
            ImGui::SameLine();
            ImGui::TextDisabled(s->smooth ? "(in-between frames; game speed unchanged)" : "(original frame rate)");
#endif
            if (ImGui::SliderInt("Render scale", &s->render_scale, 0, 8, s->render_scale ? "%dx" : "Auto")) {
                apply |= OVERLAY_APPLY_VIDEO;
            }
            note_focus("Render scale");
            ImGui::TextDisabled(s->render_scale ? "%d x %d internal; 1x = original pixels" : "window resolution",
                                320 * s->render_scale, 240 * s->render_scale);

            ImGui::SeparatorText("Sound");
            if (ImGui::SliderInt("Volume", &s->volume, 0, 100, "%d%%")) {
                apply |= OVERLAY_APPLY_VOLUME;
            }
            note_focus("Volume");

            ImGui::SeparatorText("Language");
            if (pack_combo("Text / subtitles", s_text_packs, s->text_lang, sizeof s->text_lang)) {
                apply |= OVERLAY_APPLY_TEXT;
            }
            note_focus("Text / subtitles");
            if (pack_combo("Voices", s_voice_packs, s->voice_lang, sizeof s->voice_lang)) {
                apply |= OVERLAY_APPLY_VOICE;
            }
            note_focus("Voices");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
#if defined(__ANDROID__)
            bool touch = s->touch_controls != 0;
            if (ImGui::Checkbox("Show touch controls", &touch)) {
                s->touch_controls = touch;
                settings_save(s);
                if (!touch) clear_touch_controls();
            }
            note_focus("Show touch controls");
            ImGui::Spacing();
            if (ImGui::SliderFloat("Touch control size", &s->touch_scale, 0.70f, 1.55f, "%.2fx")) {
                settings_save(s);
            }
            if (ImGui::Button("Edit touch layout", ImVec2(220.0f * ui, 42.0f * ui))) {
                s_touch_edit_layout = true;
                s_layout_drag_active = false;
                open = false;
            }
            if (ImGui::Button("Reset touch layout", ImVec2(220.0f * ui, 42.0f * ui))) {
                reset_touch_layout(s);
            }
            ImGui::TextWrapped("In layout mode, drag each D-pad direction, face button, shoulder and Start/Select independently. Tap DONE to save.");
#endif
            static const char *const labels[SETTINGS_NUM_KEYS] = {
                "Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle",
                "L1", "L2", "L3", "R1", "R2", "R3", "Start", "Select",
            };
            ImGui::TextDisabled("Click a button, then press a key (Esc cancels).");
            if (ImGui::BeginTable("keys", 4, ImGuiTableFlags_SizingStretchSame)) {
                for (int b = 0; b < SETTINGS_NUM_KEYS; b++) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(labels[b]);
                    ImGui::TableNextColumn();
                    ImGui::PushID(b);
                    const char *shown = s_rebind == b ? "press a key..." : s->keys[b];
                    if (ImGui::Button(shown, ImVec2(-FLT_MIN, 0))) {
                        s_rebind = b;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Default keys")) {
                for (int b = 0; b < SETTINGS_NUM_KEYS; b++) {
                    SDL_strlcpy(s->keys[b], settings_default_key(b), sizeof s->keys[b]);
                }
                s_keys_changed = true;
            }
            ImGui::EndTabItem();
        }
        // Tests/docs: LAIN_MENU_TAB=cheats or LAIN_MENU_TAB=mods opens the menu on that tab.
        static bool tab_forced;
        const char *want_tab = tab_forced ? nullptr : SDL_getenv("LAIN_MENU_TAB");
        const bool force_cheats = want_tab && !strcmp(want_tab, "cheats");
        const bool force_mods = want_tab && !strcmp(want_tab, "mods");
        tab_forced = true;
        if (ImGui::BeginTabItem("Cheats", nullptr, force_cheats ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::TextDisabled("Cheats change how the game runs, not your saves.");
            bool nodes = s->cheat_open_nodes != 0;
            if (ImGui::Checkbox("Open any node", &nodes)) {
                s->cheat_open_nodes = nodes;
                apply |= OVERLAY_APPLY_CHEATS;
            }
            note_focus("Open any node");
            ImGui::SameLine();
            ImGui::TextDisabled("(ignore the progress a node needs)");
            if (ImGui::SliderInt("Turbo (hold Tab)", &s->turbo, 0, 8, s->turbo >= 2 ? "%dx" : "Off")) {
                if (s->turbo == 1) {
                    s->turbo = 0;
                }
                apply |= OVERLAY_APPLY_CHEATS;
            }
            note_focus("Turbo");
            ImGui::TextDisabled("Movies and voices keep their normal speed.");
            bool genome = s->cheat_genome != 0;
            if (ImGui::Checkbox("Genome save title", &genome)) {
                s->cheat_genome = genome;
                apply |= OVERLAY_APPLY_CHEATS;
            }
            note_focus("Genome save title");
            ImGui::SameLine();
            ImGui::TextDisabled("(normally 1 save in 70)");

            // The four stats the game counts but only ever shows in a save's title
            // (docs/findings/README.md, item 1). Read live from the game.
            ImGui::SeparatorText("Lain's hidden stats");
            ImGui::TextDisabled("Since the last load; save titles show one.");
            ImGui::Text("Heart-flutter  %3d%%", g_stat_tokimeki);
            ImGui::SameLine();
            ImGui::TextDisabled("nodes opened");
            ImGui::Text("Dejection      %3d%%", g_stat_gakkuri);
            ImGui::SameLine();
            ImGui::TextDisabled("Lain's reactions");
            ImGui::Text("Harumage       %3d%%", g_stat_harumage);
            ImGui::SameLine();
            ImGui::TextDisabled("empty cells pressed");
            ImGui::Text("Poison waves   %3d ", g_screensaver_count);
            ImGui::SameLine();
            ImGui::TextDisabled("screensavers");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mods", nullptr, force_mods ? ImGuiTabItemFlags_SetSelected : 0)) {
            const std::vector<ModActive> &active = mods_active();
            if (active.empty()) {
                ImGui::TextDisabled("No mods are on.");
            }
            for (const ModActive &m : active) {
                ImGui::BulletText("%s%s%s", m.name.c_str(), m.version.empty() ? "" : "  ", m.version.c_str());
                char detail[128];
                int n = snprintf(detail, sizeof detail, "%d file%s", m.files, m.files == 1 ? "" : "s");
                if (m.overridden) n += snprintf(detail + n, sizeof detail - n, ", %d replaced by later mods", m.overridden);
                if (m.skipped) snprintf(detail + n, sizeof detail - n, ", %d skipped", m.skipped);
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", detail);
            }
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Mods are chosen on the launcher's Mods tab and apply when the game starts.");
            ImGui::PopStyleColor();
            modrt_menu(); /* each mod's settings and log */
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (s_keys_changed) {
        s_keys_changed = false;
        apply |= OVERLAY_APPLY_KEYS;
    }

    ImGui::Separator();
    if (ImGui::Button("Resume")) {
        open = false;
    }
    note_focus("Resume");
    ImGui::SameLine();
    #if defined(__ANDROID__)
    if (ImGui::Button("Return to launcher")) {
        apply |= OVERLAY_APPLY_QUIT;
    }
#else
    if (ImGui::Button("Quit game")) {
        apply |= OVERLAY_APPLY_QUIT;
    }
#endif
    note_focus("Quit game");
    ImGui::SameLine();
    ImGui::TextDisabled("Esc / F1 to close");
    ImGui::PopItemWidth();
    ImGui::End();
    ImGui::PopFont();

    if (!open) {
        set_menu(false);
    }
    if (apply && s_apply) {
        s_apply(apply);
    }
}

/* Bottom-centred subtitle lines with a dark outline, sized to the window. */
static void draw_subtitle(const ImGuiViewport *vp) {
    ImFont *font = s_sub_font ? s_sub_font : ImGui::GetFont();
    float size = vp->WorkSize.y / 18.0f;
    float wrap = vp->WorkSize.x * 0.8f;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    /* Lay out bottom-up so multi-line cues grow upwards. */
    const char *lines[8];
    int n = 0;
    static char buf[1024];
    SDL_strlcpy(buf, s_sub, sizeof buf);
    for (char *p = buf; p && n < 8;) {
        lines[n++] = p;
        p = strchr(p, '\n');
        if (p) {
            *p++ = 0;
        }
    }
    /* A translucent band behind all lines keeps them readable over the game's UI. */
    float total = 0, widest = 0;
    for (int i = 0; i < n; i++) {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, wrap, lines[i]);
        total += sz.y + size * 0.15f;
        widest = sz.x > widest ? sz.x : widest;
    }
    float bottom = vp->WorkPos.y + vp->WorkSize.y * 0.975f;
    float pad = size * 0.3f;
    dl->AddRectFilled(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - widest) * 0.5f - pad, bottom - total - pad),
                      ImVec2(vp->WorkPos.x + (vp->WorkSize.x + widest) * 0.5f + pad, bottom + pad * 0.5f),
                      IM_COL32(0, 0, 0, 150), size * 0.25f);
    float y = bottom;
    for (int i = n - 1; i >= 0; i--) {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, wrap, lines[i]);
        y -= sz.y;
        ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - sz.x) * 0.5f, y);
        float o = size / 16.0f;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx || dy) {
                    dl->AddText(font, size, ImVec2(pos.x + dx * o, pos.y + dy * o), IM_COL32(0, 0, 0, 220),
                                lines[i], nullptr, wrap);
                }
            }
        }
        dl->AddText(font, size, pos, IM_COL32(245, 245, 235, 255), lines[i], nullptr, wrap);
        y -= size * 0.15f;
    }
}

extern "C" void overlay_toast(const char *message, float seconds) {
    SDL_strlcpy(s_text, message, sizeof s_text);
    s_until = SDL_GetPerformanceCounter() + (Uint64)(seconds * (double)SDL_GetPerformanceFrequency());
}

/* The mouse cursor: shown while the F1 menu is open and for a moment after the mouse
 * moves, hidden otherwise so it doesn't sit on the picture (the game ignores the mouse). */
static void update_cursor(void) {
    const Uint64 now = SDL_GetPerformanceCounter();
    const bool recent = s_mouse_moved_at && now - s_mouse_moved_at < SDL_GetPerformanceFrequency() * 5 / 2;
    const int want = (s_menu || recent) ? SDL_ENABLE : SDL_DISABLE;
    if (SDL_ShowCursor(SDL_QUERY) != want) {
        SDL_ShowCursor(want);
    }
}

extern "C" void overlay_draw(void) {
    update_cursor();
    if (s_text[0] && SDL_GetPerformanceCounter() >= s_until) {
        s_text[0] = 0;
    }
#if defined(__ANDROID__)
    const bool show_touch_controls = s_settings && (s_settings->touch_controls || s_touch_edit_layout);
    const bool show_touch_menu = s_settings != nullptr;
#else
    const bool show_touch_controls = false;
    const bool show_touch_menu = false;
#endif
    if (!s_text[0] && !s_sub[0] && !s_menu && !modrt_wants_draw() && !show_touch_menu) {
        return;
    }
    if (!s_ready) {
        s_ready = overlay_init();
        if (!s_ready) {
            return;
        }
    }
    if (s_font_dirty) {
        load_sub_font();
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    if (vp->WorkSize.x < 64.0f || vp->WorkSize.y < 64.0f) {
        /* Minimized (or tiny): fonts would be sized from a zero-height window. */
        ImGui::EndFrame();
        return;
    }
    if (s_sub[0]) {
        draw_subtitle(vp);
    }
    modrt_draw(); /* mod windows and overlays */
#if defined(__ANDROID__)
    if (show_touch_menu && !s_menu) draw_touch_controls(vp, show_touch_controls);
#endif
    if (s_menu) {
        draw_menu(vp);
    }
    if (!s_text[0]) {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 24.0f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(vp->WorkSize.y / 360.0f > 1.5f ? vp->WorkSize.y / 360.0f : 1.5f); /* ~1/25 of the height */
    ImGui::TextUnformatted(s_text);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
