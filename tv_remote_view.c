#include "tv_remote.h"

#include <stdio.h>

const char* const tv_remote_button_names[KEY_COUNT] = {
    "POWER",
    "MUTE",
    "VOL+",
    "CH+",
    "OK",
    "CH-",
    "VOL-",
    "UP",
    "DOWN",
    "LEFT",
    "BACK",
    "RIGHT",
    "NETFLIX"};

const char* const tv_remote_button_labels[KEY_COUNT] = {
    "PWR",
    "MUTE",
    "VOL+",
    "CH+",
    "OK",
    "CH-",
    "VOL-",
    "UP",
    "DOWN",
    "LEFT",
    "BACK",
    "RIGHT",
    "NETFLIX"};

static void draw_text_centered(Canvas* canvas, int x, int y, const char* text) {
    canvas_draw_str_aligned(canvas, x, y, AlignCenter, AlignCenter, text);
}

static void draw_footer(Canvas* canvas, const char* text) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 63, text);
}

static void draw_files_screen(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "TV REMOTE");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 18, "Choose a captured .ir file");

    if(app->file_count == 0) {
        canvas_draw_str(
            canvas, 2, 34, app->status[0] ? app->status : "Copy files to /ext/infrared");
        draw_footer(canvas, "BACK: exit");
        return;
    }

    for(size_t row = 0; row < 5 && app->file_offset + row < app->file_count; row++) {
        size_t index = app->file_offset + row;
        int y = 29 + (int)row * 7;
        if(index == app->file_selection) {
            canvas_draw_box(canvas, 0, y - 6, 128, 8);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y, app->files[index].name);
        if(index == app->file_selection) canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(canvas, app->status[0] ? app->status : "UP/DOWN choose  OK open  BACK exit");
}

static void draw_remote_screen(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "TV REMOTE");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 76, 9, app->files[app->file_selection].name);
    canvas_draw_rframe(canvas, 19, 13, 90, 44, 5);

    for(size_t key = 0; key < KEY_COUNT; key++) {
        bool netflix_key = key == KEY_COUNT - 1;
        int column = (int)(key % 3);
        int row = (int)(key / 3);
        int x = netflix_key ? 44 : 24 + column * 27;
        int y = netflix_key ? 48 : 17 + row * 8;
        int button_width = netflix_key ? 40 : 23;
        if(key == app->key_selection) {
            canvas_draw_rframe(canvas, x - 2, y - 2, button_width + 2, 9, 2);
        } else {
            canvas_draw_rframe(canvas, x - 1, y - 1, button_width, 7, 2);
        }
        canvas_set_font(canvas, FontSecondary);
        draw_text_centered(canvas, x + button_width / 2 - 1, y + 2, tv_remote_button_labels[key]);
        if(app->mappings[key] < 0) {
            canvas_draw_disc(canvas, x + button_width - 2, y + 1, 1);
        }
    }

    draw_footer(canvas, app->status[0] ? app->status : "OK send  HOLD map  BACK files");
}

static void draw_map_screen(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "MAP SIGNAL");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 18, tv_remote_button_names[app->key_selection]);

    for(size_t row = 0; row < 4 && app->signal_offset + row < app->signal_count; row++) {
        size_t index = app->signal_offset + row;
        int y = 30 + (int)row * 8;
        if(index == app->signal_selection) {
            canvas_draw_box(canvas, 0, y - 6, 128, 8);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y, app->signals[index].name);
        if(index == app->signal_selection) canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(canvas, app->status[0] ? app->status : "UP/DOWN choose  OK assign  BACK cancel");
}

void tv_remote_draw(Canvas* canvas, void* context) {
    TvRemoteApp* app = context;
    if(furi_mutex_acquire(app->state_mutex, FuriWaitForever) != FuriStatusOk) return;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(app->screen == ScreenFiles) {
        draw_files_screen(canvas, app);
    } else if(app->screen == ScreenRemote) {
        draw_remote_screen(canvas, app);
    } else {
        draw_map_screen(canvas, app);
    }

    furi_mutex_release(app->state_mutex);
}
