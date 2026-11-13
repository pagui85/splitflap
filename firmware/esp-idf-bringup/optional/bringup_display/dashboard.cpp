// SPDX-License-Identifier: Apache-2.0
#include "dashboard.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "bringup_ethernet.hpp"
#include "bringup_ota.hpp"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace bringup::display {
namespace {
#include "logo_pixels.hpp"

const lv_image_dsc_t kLogo = [] {
    lv_image_dsc_t logo{};
    logo.header.magic = LV_IMAGE_HEADER_MAGIC;
    logo.header.cf = LV_COLOR_FORMAT_ARGB8888;
    logo.header.w = 48;
    logo.header.h = 48;
    logo.header.stride = 48 * 4;
    logo.data_size = sizeof(kLogoPixels);
    logo.data = kLogoPixels;
    return logo;
}();
constexpr unsigned kCapacity = 72;
lv_obj_t *network_card;
lv_obj_t *memory_card;
lv_obj_t *display_card;
lv_obj_t *marker;
lv_obj_t *coordinate;
lv_obj_t *touch_pad;
lv_obj_t *preview;
lv_obj_t *layout_status;
lv_obj_t *input;
lv_obj_t *row_count;
lv_obj_t *col_count;
lv_obj_t *keys;

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font = &lv_font_montserrat_24)
{
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xe7edf4), 0);
    return obj;
}

lv_obj_t *card(lv_obj_t *parent, const char *title, lv_obj_t **value)
{
    auto *box = lv_obj_create(parent);
    lv_obj_set_size(box, 374, 354);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x182432), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 20, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 20, 0);
    auto *heading = label(box, title);
    lv_obj_set_style_text_color(heading, lv_color_hex(0x82d4bc), 0);
    *value = label(box, "");
    lv_obj_set_width(*value, 334);
    lv_label_set_long_mode(*value, LV_LABEL_LONG_WRAP);
    return box;
}

void update_cards(lv_timer_t *)
{
    char link[64];
    char ip[24];
    ethernet::display_status(link, sizeof(link), ip, sizeof(ip));
    char ota_state[32];
    ota::display_state(ota_state, sizeof(ota_state));
    lv_label_set_text_fmt(network_card, "%s\n%s\n\nUptime  %lu s\nOTA  %s", link, ip,
                          static_cast<unsigned long>(esp_timer_get_time() / 1000000), ota_state);
    lv_label_set_text_fmt(memory_card, "%u KiB internal free\n%u KiB external free\n\n72 module capacity",
                          unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                          unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    const char *touch = std::strcmp(touch_state(), "gt911_polling") == 0 ? "GT911 polling" : "Touch unavailable";
    lv_label_set_text_fmt(display_card, "1280 × 800 RGB565\nDSI scanout active\n\n%s\nSee USB for frame / CPU data", touch);
}

void update_preview(lv_event_t *)
{
    const unsigned rows = lv_spinbox_get_value(row_count);
    const unsigned cols = lv_spinbox_get_value(col_count);
    if (rows * cols > kCapacity) {
        lv_label_set_text(layout_status, "Choose a layout with up to 72 positions.");
        return;
    }
    lv_label_set_text_fmt(layout_status, "%u × %u  •  %u / 72", rows, cols, rows * cols);
    lv_table_set_row_count(preview, rows);
    lv_table_set_column_count(preview, cols);
    const char *text = lv_textarea_get_text(input);
    const size_t length = std::strlen(text);
    for (unsigned col = 0; col < cols; ++col)
        lv_table_set_column_width(preview, col, std::min(72u, 1152u / cols));
    for (unsigned row = 0; row < rows; ++row) {
        for (unsigned col = 0; col < cols; ++col) {
            const size_t index = row * cols + col;
            const char glyph[] = {index < length ? text[index] : ' ', '\0'};
            lv_table_set_cell_value(preview, row, col, glyph);
        }
    }
}

void change_number(lv_event_t *event)
{
    auto *spin = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    const char *button = lv_label_get_text(lv_obj_get_child(lv_event_get_target_obj(event), 0));
    if (button[0] == '+') lv_spinbox_increment(spin);
    else lv_spinbox_decrement(spin);
    update_preview(nullptr);
}

lv_obj_t *layout_control(lv_obj_t *parent, const char *name, int maximum, int initial)
{
    label(parent, name);
    auto *box = lv_spinbox_create(parent);
    lv_spinbox_set_range(box, 1, maximum);
    lv_spinbox_set_digit_format(box, 2, 0);
    lv_spinbox_set_value(box, initial);
    lv_obj_set_width(box, 68);
    for (const char *text : {"-", "+"}) {
        auto *button = lv_button_create(parent);
        lv_obj_set_size(button, 48, 44);
        auto *caption = label(button, text);
        lv_obj_center(caption);
        lv_obj_add_event_cb(button, change_number, LV_EVENT_CLICKED, box);
    }
    return box;
}

void keyboard(lv_event_t *event)
{
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(keys, input);
        lv_obj_remove_flag(keys, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(keys, nullptr);
        lv_obj_add_flag(keys, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_brightness(lv_event_t *event)
{
    auto *slider = lv_event_get_target_obj(event);
    auto *status = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    const int value = lv_slider_get_value(slider);
    if (brightness(value) == ESP_OK) lv_label_set_text_fmt(status, "Brightness  %d%%", value);
    else lv_label_set_text(status, "Backlight command failed");
}
}

void create_dashboard(lv_display_t *display, bool touch_available)
{
    auto *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101821), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xe7edf4), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_24, 0);

    auto *tabs = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(tabs, 64);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(0x101821), 0);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_bar(tabs), lv_color_hex(0x213142), 0);
    lv_obj_set_style_text_color(lv_tabview_get_tab_bar(tabs), lv_color_hex(0xe7edf4), 0);
    auto *overview = lv_tabview_add_tab(tabs, "Controller");
    auto *touch_tab = lv_tabview_add_tab(tabs, "Touch test");
    auto *simulation = lv_tabview_add_tab(tabs, "Preview");

    auto *icon = lv_image_create(overview);
    lv_image_set_src(icon, &kLogo);
    lv_obj_set_pos(icon, 28, 20);
    auto *brand = label(overview, "SPLITFLAP", &lv_font_montserrat_48);
    lv_obj_set_pos(brand, 88, 16);
    lv_obj_set_style_text_color(brand, lv_color_hex(0x82d4bc), 0);
    char build[25]{};
    const auto &app = *esp_app_get_description();
    for (size_t i = 0; i < 12; ++i) std::snprintf(build + i * 2, 3, "%02x", app.app_elf_sha256[i]);
    auto *version = label(overview, "");
    lv_label_set_text_fmt(version, "Firmware %s   •   Build %s", app.version, build);
    lv_obj_set_pos(version, 28, 82);

    auto *cards = lv_obj_create(overview);
    lv_obj_remove_style_all(cards);
    lv_obj_set_size(cards, 1180, 360);
    lv_obj_set_pos(cards, 28, 142);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cards, 20, 0);
    card(cards, "Network", &network_card);
    card(cards, "Memory", &memory_card);
    card(cards, "Display", &display_card);
    auto *light = label(overview, "Brightness  40%");
    lv_obj_set_pos(light, 28, 540);
    auto *slider = lv_slider_create(overview);
    lv_obj_set_size(slider, 370, 26);
    lv_obj_set_pos(slider, 28, 592);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, 40, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, set_brightness, LV_EVENT_RELEASED, light);
    update_cards(nullptr);
    lv_timer_create(update_cards, 1000, nullptr);

    coordinate = label(touch_tab, touch_available ? "Touch within the grid to see raw and mapped coordinates." :
                       "Touch controller unavailable. Check I²C and panel power.");
    lv_obj_set_pos(coordinate, 20, 16);
    touch_pad = lv_obj_create(touch_tab);
    lv_obj_set_pos(touch_pad, 20, 70);
    lv_obj_set_size(touch_pad, 1160, 570);
    lv_obj_set_style_bg_color(touch_pad, lv_color_hex(0x192a3b), 0);
    lv_obj_set_style_border_color(touch_pad, lv_color_hex(0x82d4bc), 0);
    lv_obj_set_style_pad_all(touch_pad, 0, 0);
    lv_obj_remove_flag(touch_pad, LV_OBJ_FLAG_SCROLLABLE);
    marker = lv_obj_create(touch_pad);
    lv_obj_set_size(marker, 28, 28);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(0xffbd75), 0);
    lv_obj_set_style_border_width(marker, 0, 0);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);

    auto *controls = lv_obj_create(simulation);
    lv_obj_remove_style_all(controls);
    lv_obj_set_pos(controls, 20, 12);
    lv_obj_set_size(controls, 1160, 55);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 12, 0);
    row_count = layout_control(controls, "Rows", 8, 3);
    col_count = layout_control(controls, "Columns", 24, 16);
    layout_status = label(controls, "");
    input = lv_textarea_create(simulation);
    lv_obj_set_pos(input, 20, 82);
    lv_obj_set_size(input, 1160, 60);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, kCapacity);
    lv_textarea_set_accepted_chars(input, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?'/-");
    lv_textarea_set_text(input, "SPLITFLAP CONTROLLER");
    preview = lv_table_create(simulation);
    lv_obj_set_pos(preview, 20, 160);
    lv_obj_set_size(preview, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(preview, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(preview, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x101821), LV_PART_ITEMS);
    lv_obj_set_style_border_color(preview, lv_color_hex(0x34475b), LV_PART_ITEMS);
    lv_obj_set_style_border_width(preview, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(preview, LV_BORDER_SIDE_FULL, LV_PART_ITEMS);
    lv_obj_set_style_text_color(preview, lv_color_hex(0xffbd75), LV_PART_ITEMS);
    lv_obj_set_style_text_font(preview, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_set_style_text_align(preview, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(preview, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(preview, 7, LV_PART_ITEMS);
    lv_obj_set_style_min_height(preview, 42, LV_PART_ITEMS);
    keys = lv_keyboard_create(simulation);
    lv_obj_set_size(keys, 1160, 260);
    lv_obj_align(keys, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(keys, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(input, update_preview, LV_EVENT_VALUE_CHANGED, nullptr);
    for (lv_event_code_t code : {LV_EVENT_FOCUSED, LV_EVENT_CLICKED, LV_EVENT_READY, LV_EVENT_CANCEL})
        lv_obj_add_event_cb(input, keyboard, code, nullptr);
    lv_obj_add_event_cb(keys, keyboard, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keys, keyboard, LV_EVENT_CANCEL, nullptr);
    update_preview(nullptr);
}

void update_marker(int raw_x, int raw_y, int x, int y, bool pressed)
{
    if (!marker || !pressed) return;
    lv_area_t area{};
    lv_obj_get_coords(touch_pad, &area);
    if (x < area.x1 || x > area.x2 || y < area.y1 || y > area.y2) return;
    lv_label_set_text_fmt(coordinate, "Raw  %d, %d    •    Screen  %d, %d", raw_x, raw_y, x, y);
    lv_obj_set_pos(marker, std::clamp(x - int(area.x1) - 14, 0, int(lv_obj_get_width(touch_pad)) - 28),
                   std::clamp(y - int(area.y1) - 14, 0, int(lv_obj_get_height(touch_pad)) - 28));
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
}
}
