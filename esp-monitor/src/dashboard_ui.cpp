#include "dashboard_ui.h"

#include <math.h>

#include "lvgl.h"

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr uint32_t kUpdatePeriodMs = 900;

const lv_color_t kBg = lv_color_hex(0x070A0E);
const lv_color_t kPanel = lv_color_hex(0x11171E);
const lv_color_t kPanelSoft = lv_color_hex(0x17212B);
const lv_color_t kGrid = lv_color_hex(0x24323D);
const lv_color_t kText = lv_color_hex(0xEDF7FF);
const lv_color_t kMuted = lv_color_hex(0x788693);
const lv_color_t kCyan = lv_color_hex(0x48DCF8);
const lv_color_t kCyanSoft = lv_color_hex(0x143946);
const lv_color_t kOrange = lv_color_hex(0xFF962F);
const lv_color_t kOrangeSoft = lv_color_hex(0x4B2C12);
const lv_color_t kDanger = lv_color_hex(0xFF5A54);

struct DashboardWidgets {
  lv_obj_t* sync_state;
  lv_obj_t* clock;
  lv_obj_t* cpu_value;
  lv_obj_t* cpu_temp;
  lv_obj_t* cpu_freq;
  lv_obj_t* cpu_bar;
  lv_obj_t* gpu_value;
  lv_obj_t* gpu_bar;
  lv_obj_t* gpu_footer;
  lv_obj_t* mem_value;
  lv_obj_t* mem_bar;
  lv_obj_t* mem_footer;
  lv_obj_t* net_up;
  lv_obj_t* net_down;
  lv_obj_t* status;
  lv_obj_t* freshness;
  lv_obj_t* freshness_bar;
};

DashboardWidgets g{};

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, lv_text_align_t align, const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_text(label, text);
  return label;
}

void set_panel_style(lv_obj_t* obj, lv_color_t accent, int radius = 12) {
  lv_obj_set_style_bg_color(obj, kPanel, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, lv_color_mix(accent, kGrid, LV_OPA_40), 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_pad_all(obj, 10, 0);
}

void set_bar_style(lv_obj_t* bar, lv_color_t accent) {
  lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, kPanelSoft, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

void add_corner_brackets(lv_obj_t* parent, lv_color_t color) {
  const lv_align_t aligns[4] = {
      LV_ALIGN_TOP_LEFT,
      LV_ALIGN_TOP_RIGHT,
      LV_ALIGN_BOTTOM_LEFT,
      LV_ALIGN_BOTTOM_RIGHT,
  };

  for (lv_align_t align : aligns) {
    lv_obj_t* h = lv_obj_create(parent);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, 12, 2);
    lv_obj_set_style_bg_color(h, color, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_80, 0);
    lv_obj_align(h, align, 0, 0);

    lv_obj_t* v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, 2, 12);
    lv_obj_set_style_bg_color(v, color, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_80, 0);
    lv_obj_align(v, align, 0, 0);
  }
}

void build_background(lv_obj_t* screen) {
  lv_obj_remove_style_all(screen);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_bg_color(screen, kBg, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  for (int y = 20; y < kScreenHeight; y += 26) {
    lv_obj_t* line = lv_obj_create(screen);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 300, 1);
    lv_obj_set_style_bg_color(line, kGrid, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_20, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, y);
  }

  for (int x = 20; x < kScreenWidth; x += 42) {
    lv_obj_t* line = lv_obj_create(screen);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 1, 200);
    lv_obj_set_style_bg_color(line, kGrid, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_10, 0);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, x, 30);
  }
}

void build_header(lv_obj_t* screen) {
  lv_obj_t* header = lv_obj_create(screen);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, 304, 28);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t* title = make_label(header, &lv_font_montserrat_14, kText, LV_TEXT_ALIGN_LEFT, "CYBER INDUSTRIAL PANEL");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* sub = make_label(header, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_LEFT, "AIRM2M CORE / LANDSCAPE");
  lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* chip = lv_obj_create(header);
  lv_obj_remove_style_all(chip);
  lv_obj_set_size(chip, 72, 18);
  lv_obj_align(chip, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_radius(chip, 9, 0);
  lv_obj_set_style_bg_color(chip, kCyanSoft, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_80, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_color(chip, kCyan, 0);

  g.sync_state = make_label(chip, LV_FONT_DEFAULT, kCyan, LV_TEXT_ALIGN_CENTER, "ONLINE");
  lv_obj_center(g.sync_state);

  g.clock = make_label(header, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_RIGHT, "14:32:08");
  lv_obj_align(g.clock, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void build_cpu_panel(lv_obj_t* screen) {
  lv_obj_t* panel = lv_obj_create(screen);
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, 148, 176);
  lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 8, 46);
  set_panel_style(panel, kCyan, 14);
  add_corner_brackets(panel, kCyan);

  lv_obj_t* eyebrow = make_label(panel, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_LEFT, "PRIMARY LOAD");
  lv_obj_align(eyebrow, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* title = make_label(panel, LV_FONT_DEFAULT, kCyan, LV_TEXT_ALIGN_LEFT, "CPU");
  lv_obj_set_style_text_letter_space(title, 2, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 16);

  g.cpu_value = make_label(panel, &lv_font_montserrat_14, kText, LV_TEXT_ALIGN_LEFT, "056%");
  lv_obj_set_style_text_letter_space(g.cpu_value, 4, 0);
  lv_obj_align(g.cpu_value, LV_ALIGN_TOP_LEFT, 0, 40);

  lv_obj_t* util = make_label(panel, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_LEFT, "UTIL");
  lv_obj_align(util, LV_ALIGN_TOP_LEFT, 0, 66);

  g.cpu_bar = lv_bar_create(panel);
  lv_obj_set_size(g.cpu_bar, 124, 10);
  lv_obj_align(g.cpu_bar, LV_ALIGN_TOP_LEFT, 0, 90);
  lv_bar_set_range(g.cpu_bar, 0, 100);
  lv_bar_set_value(g.cpu_bar, 56, LV_ANIM_OFF);
  set_bar_style(g.cpu_bar, kOrange);

  g.cpu_temp = make_label(panel, LV_FONT_DEFAULT, kOrange, LV_TEXT_ALIGN_LEFT, "TEMP 75C");
  lv_obj_align(g.cpu_temp, LV_ALIGN_TOP_LEFT, 0, 118);

  g.cpu_freq = make_label(panel, LV_FONT_DEFAULT, kCyan, LV_TEXT_ALIGN_LEFT, "CLK 5.0GHz");
  lv_obj_align(g.cpu_freq, LV_ALIGN_TOP_LEFT, 0, 140);

  lv_obj_t* rail = make_label(panel, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_LEFT, "LOAD RAIL");
  lv_obj_align(rail, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void build_metric_panel(
    lv_obj_t* screen,
    int x,
    int y,
    const char* title,
    lv_color_t accent,
    lv_obj_t** out_value,
    lv_obj_t** out_bar,
    lv_obj_t** out_footer) {
  lv_obj_t* panel = lv_obj_create(screen);
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, 72, 84);
  lv_obj_align(panel, LV_ALIGN_TOP_LEFT, x, y);
  set_panel_style(panel, accent, 12);
  add_corner_brackets(panel, accent);

  lv_obj_t* heading = make_label(panel, LV_FONT_DEFAULT, accent, LV_TEXT_ALIGN_LEFT, title);
  lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);

  *out_value = make_label(panel, &lv_font_montserrat_14, kText, LV_TEXT_ALIGN_LEFT, "32%");
  lv_obj_align(*out_value, LV_ALIGN_TOP_LEFT, 0, 20);

  *out_bar = lv_bar_create(panel);
  lv_obj_set_size(*out_bar, 48, 8);
  lv_obj_align(*out_bar, LV_ALIGN_TOP_LEFT, 0, 46);
  lv_bar_set_range(*out_bar, 0, 100);
  lv_bar_set_value(*out_bar, 32, LV_ANIM_OFF);
  set_bar_style(*out_bar, accent);

  *out_footer = make_label(panel, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_LEFT, "TEMP 61C");
  lv_obj_align(*out_footer, LV_ALIGN_TOP_LEFT, 0, 62);
}

void build_right_strip(lv_obj_t* screen) {
  lv_obj_t* strip = lv_obj_create(screen);
  lv_obj_remove_style_all(strip);
  lv_obj_set_size(strip, 76, 176);
  lv_obj_align(strip, LV_ALIGN_TOP_LEFT, 236, 46);
  set_panel_style(strip, kOrange, 14);
  lv_obj_set_style_bg_color(strip, lv_color_mix(kPanel, kOrangeSoft, LV_OPA_20), 0);
  add_corner_brackets(strip, kOrange);

  lv_obj_t* title = make_label(strip, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_LEFT, "LINK");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  g.status = make_label(strip, LV_FONT_DEFAULT, kOrange, LV_TEXT_ALIGN_LEFT, "BOOST");
  lv_obj_align(g.status, LV_ALIGN_TOP_LEFT, 0, 18);

  g.net_up = make_label(strip, LV_FONT_DEFAULT, kText, LV_TEXT_ALIGN_LEFT, "UP 048");
  lv_obj_align(g.net_up, LV_ALIGN_TOP_LEFT, 0, 48);

  g.net_down = make_label(strip, LV_FONT_DEFAULT, kText, LV_TEXT_ALIGN_LEFT, "DN 106");
  lv_obj_align(g.net_down, LV_ALIGN_TOP_LEFT, 0, 68);

  g.freshness = make_label(strip, LV_FONT_DEFAULT, kCyan, LV_TEXT_ALIGN_LEFT, "FRESH 82%");
  lv_obj_align(g.freshness, LV_ALIGN_TOP_LEFT, 0, 102);

  g.freshness_bar = lv_bar_create(strip);
  lv_obj_set_size(g.freshness_bar, 56, 6);
  lv_obj_align(g.freshness_bar, LV_ALIGN_TOP_LEFT, 0, 126);
  lv_bar_set_range(g.freshness_bar, 0, 100);
  lv_bar_set_value(g.freshness_bar, 82, LV_ANIM_OFF);
  set_bar_style(g.freshness_bar, kCyan);

  lv_obj_t* note = make_label(strip, LV_FONT_DEFAULT, kMuted, LV_TEXT_ALIGN_LEFT, "FAST");
  lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void update_dashboard(lv_timer_t* timer) {
  LV_UNUSED(timer);

  static uint32_t tick = 0;
  tick++;

  const int cpu = 52 + static_cast<int>(15.0f * sinf(tick * 0.41f)) + static_cast<int>((tick % 3) * 2);
  const int gpu = 34 + static_cast<int>(16.0f * sinf(tick * 0.27f + 0.8f));
  const int mem = 56 + static_cast<int>(8.0f * sinf(tick * 0.19f + 0.4f));
  const int freshness = 82 + static_cast<int>(10.0f * sinf(tick * 0.47f));
  const int cpu_temp = 61 + cpu / 5;
  const int gpu_temp = 49 + gpu / 6;
  const int mem_used = 11 + mem / 10;
  const int upload = 46 + static_cast<int>(20.0f * sinf(tick * 0.39f));
  const int download = 98 + static_cast<int>(18.0f * sinf(tick * 0.24f + 0.5f));
  const bool hot = cpu > 76;

  lv_label_set_text_fmt(g.cpu_value, "%03d%%", cpu);
  lv_bar_set_value(g.cpu_bar, cpu, LV_ANIM_ON);
  lv_label_set_text_fmt(g.cpu_temp, "TEMP %dC", cpu_temp);
  lv_label_set_text_fmt(g.cpu_freq, "CLK %d.%dGHz", 4 + (cpu / 45), (cpu / 7) % 10);

  lv_label_set_text_fmt(g.gpu_value, "%02d%%", gpu);
  lv_bar_set_value(g.gpu_bar, gpu, LV_ANIM_ON);
  lv_label_set_text_fmt(g.gpu_footer, "TEMP %dC", gpu_temp);

  lv_label_set_text_fmt(g.mem_value, "%02d%%", mem);
  lv_bar_set_value(g.mem_bar, mem, LV_ANIM_ON);
  lv_label_set_text_fmt(g.mem_footer, "USED %dG", mem_used);

  lv_label_set_text_fmt(g.net_up, "UP %03d", upload);
  lv_label_set_text_fmt(g.net_down, "DN %03d", download);
  lv_label_set_text(g.status, hot ? "HEAT" : "BOOST");
  lv_obj_set_style_text_color(g.status, hot ? kDanger : kOrange, 0);

  lv_label_set_text_fmt(g.freshness, "FRESH %d%%", freshness);
  lv_bar_set_value(g.freshness_bar, freshness, LV_ANIM_ON);

  lv_label_set_text(g.sync_state, freshness < 78 ? "SYNCING" : "ONLINE");
  lv_obj_set_style_text_color(g.sync_state, freshness < 78 ? kOrange : kCyan, 0);
  lv_label_set_text_fmt(g.clock, "14:32:%02d", 8 + static_cast<int>(tick % 50));
}

}  // namespace

void dashboard_ui_create(lv_display_t* display) {
  lv_obj_t* screen = lv_display_get_screen_active(display);

  build_background(screen);
  build_header(screen);
  build_cpu_panel(screen);
  build_metric_panel(screen, 160, 46, "GPU", kCyan, &g.gpu_value, &g.gpu_bar, &g.gpu_footer);
  build_metric_panel(screen, 160, 138, "MEM", kOrange, &g.mem_value, &g.mem_bar, &g.mem_footer);
  build_right_strip(screen);

  lv_timer_create(update_dashboard, kUpdatePeriodMs, nullptr);
  update_dashboard(nullptr);
}
