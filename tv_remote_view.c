#include "tv_remote.h"

#include <stdio.h>
#include "backgrounds/themes/theme_bitmaps.h"

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

static const char* const remote_theme_names[ThemeCount] =
    {"NARUTO", "BLEACH", "DEATH NOTE", "HUNTER X HUNTER"};

static const char* const remote_theme_short_names[ThemeCount] =
    {"NARUTO", "BLEACH", "DEATH NOTE", "H X H"};

static void draw_text_centered(Canvas* canvas, int x, int y, const char* text) {
    canvas_draw_str_aligned(canvas, x, y, AlignCenter, AlignCenter, text);
}

static void draw_theme_panel(Canvas* canvas, int x, int y, int width, int height, bool selected) {
    if(selected) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, x, y, width, height);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_rframe(canvas, x + 1, y + 1, width - 2, height - 2, 3);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, x, y, width, height);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, x, y, width, height, 3);
    }
}

static void draw_remote_background(Canvas* canvas, RemoteTheme theme) {
    const uint8_t* bitmap = naruto_background;
    if(theme == ThemeBleach) {
        bitmap = bleach_background;
    } else if(theme == ThemeDeathNote) {
        bitmap = deathnote_background;
    } else if(theme == ThemeHxH) {
        bitmap = hxh_background;
    }
    canvas_draw_xbm(canvas, 0, 0, 64, 128, bitmap);
}

static void draw_footer(Canvas* canvas, const char* text) {
    canvas_set_font(canvas, FontSecondary);
    int footer_y = (int)canvas_height(canvas) - 2;
    int divider_y = (int)canvas_height(canvas) - 11;
    canvas_draw_line(canvas, 1, divider_y, (int)canvas_width(canvas) - 2, divider_y);
    canvas_draw_str(canvas, 2, footer_y, text);
}

static void draw_home_screen(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 3, 10, "IR REMOTE");
    canvas_set_font(canvas, FontSecondary);
    draw_text_centered(canvas, 105, 9, remote_theme_short_names[app->theme]);
    canvas_draw_line(canvas, 3, 13, 124, 13);

    for(size_t option = 0; option < 2; option++) {
        int x = 4 + (int)option * 62;
        bool selected = option == app->home_selection;
        if(selected) {
            canvas_draw_box(canvas, x, 17, 58, 34);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_rframe(canvas, x + 1, 18, 56, 32, 4);
        } else {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_rframe(canvas, x, 17, 58, 34, 4);
        }

        int icon_x = x + 25;
        canvas_draw_rframe(canvas, icon_x, 20, 9, 14, 2);
        if(option == 0) {
            canvas_draw_line(canvas, icon_x + 4, 23, icon_x + 4, 29);
            canvas_draw_line(canvas, icon_x + 1, 26, icon_x + 7, 26);
        } else {
            canvas_draw_line(canvas, icon_x + 2, 23, icon_x + 6, 23);
            canvas_draw_line(canvas, icon_x + 2, 26, icon_x + 6, 26);
            canvas_draw_line(canvas, icon_x + 2, 29, icon_x + 6, 29);
        }

        canvas_set_font(canvas, FontSecondary);
        if(option == 0) {
            draw_text_centered(canvas, x + 29, 43, "NEW REMOTE");
        } else {
            draw_text_centered(canvas, x + 29, 40, "SAVED");
            draw_text_centered(canvas, x + 29, 47, "REMOTES");
        }
        canvas_set_color(canvas, ColorBlack);
    }

    draw_footer(canvas, "OK OPEN  HOLD OK BG");
}

static void draw_theme_screen(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "CHOOSE BACKGROUND");

    for(size_t theme = 0; theme < ThemeCount; theme++) {
        int y = 16 + (int)theme * 9;
        bool selected = theme == app->theme_selection;
        draw_theme_panel(canvas, 2, y, 124, 8, selected);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 5, y + 6, remote_theme_names[theme]);
        if(theme == app->theme) {
            canvas_draw_disc(canvas, 117, y + 5, 2);
        }
        canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(canvas, "UP/DN  OK APPLY  BACK");
}

static void draw_landscape_files(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(
        canvas, 2, 9, app->file_list_mode == FileListNewRemote ? "NEW REMOTE" : "SAVED REMOTES");

    if(app->file_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 31, app->status[0] ? app->status : "No remotes found");
        draw_footer(canvas, "BACK");
        return;
    }

    for(size_t row = 0; row < 4 && app->file_offset + row < app->file_count; row++) {
        size_t index = app->file_offset + row;
        int y = 24 + (int)row * 8;
        if(index == app->file_selection) {
            canvas_draw_box(canvas, 1, y - 7, 126, 9);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 4, y, app->files[index].name);
        if(index == app->file_selection) canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(canvas, app->status[0] ? app->status : "UP/DN  OK OPEN  BACK HOME");
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
        int y = netflix_key ? 104 : 17 + row * 8;
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

    draw_footer(canvas, app->status[0] ? app->status : "OK SEND  HOLD BK SAVE");
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
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 6, 128, 8);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }
        canvas_draw_str(canvas, 4, y, app->signals[index].name);
        if(index == app->signal_selection) canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(canvas, app->status[0] ? app->status : "UP/DOWN choose  OK assign  BACK cancel");
}

static void draw_portrait_button(
    Canvas* canvas,
    const TvRemoteApp* app,
    size_t key,
    int x,
    int y,
    int width,
    int height) {
    const char* label = tv_remote_button_labels[key];
    if(key == 9) label = "LT";
    if(key == 11) label = "RT";
    if(key == 8) label = "DN";

    bool selected = key == app->key_selection;
    if(selected) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, x, y, width, height);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_rframe(canvas, x, y, width, height, 2);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, x, y, width, height);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, x, y, width, height, 2);
    }
    canvas_set_font(canvas, FontSecondary);
    if(selected) canvas_set_color(canvas, ColorWhite);
    draw_text_centered(canvas, x + width / 2, y + height / 2, label);
    canvas_set_color(canvas, ColorBlack);
    if(app->mappings[key] < 0) {
        canvas_draw_disc(canvas, x + width - 4, y + 3, 1);
    }
}

static void draw_portrait_files(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, "TV REMOTE");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 17, "Select captured remote");

    if(app->file_count == 0) {
        canvas_draw_str(canvas, 2, 38, app->status[0] ? app->status : "Copy .ir files to:");
        canvas_draw_str(canvas, 2, 48, "/ext/infrared");
        draw_footer(canvas, "BACK exits");
        return;
    }

    for(size_t row = 0; row < 10 && app->file_offset + row < app->file_count; row++) {
        size_t index = app->file_offset + row;
        int y = 28 + (int)row * 9;
        if(index == app->file_selection) {
            canvas_draw_box(canvas, 1, y - 7, 62, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 3, y, app->files[index].name);
        if(index == app->file_selection) canvas_set_color(canvas, ColorBlack);
    }

    draw_footer(canvas, app->status[0] ? app->status : "UP/DN scroll  OK open");
}

static void draw_portrait_remote(Canvas* canvas, const TvRemoteApp* app) {
    draw_portrait_button(canvas, app, 0, 2, 8, 28, 11);
    draw_portrait_button(canvas, app, 1, 34, 8, 28, 11);
    draw_portrait_button(canvas, app, 6, 2, 21, 28, 11);
    draw_portrait_button(canvas, app, 2, 34, 21, 28, 11);
    draw_portrait_button(canvas, app, 5, 2, 34, 28, 11);
    draw_portrait_button(canvas, app, 3, 34, 34, 28, 11);

    draw_portrait_button(canvas, app, 7, 23, 44, 18, 11);
    draw_portrait_button(canvas, app, 9, 2, 56, 18, 11);
    draw_portrait_button(canvas, app, 4, 23, 56, 18, 11);
    draw_portrait_button(canvas, app, 11, 44, 56, 18, 11);
    draw_portrait_button(canvas, app, 8, 23, 68, 18, 11);

    draw_portrait_button(canvas, app, 10, 18, 80, 28, 11);
    draw_portrait_button(canvas, app, 12, 2, 92, 60, 11);
    draw_footer(canvas, app->status[0] ? app->status : "HOLD BK SAVE");
}

static void draw_portrait_map(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, "MAP SIGNAL");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 17, tv_remote_button_names[app->key_selection]);

    for(size_t row = 0; row < 10 && app->signal_offset + row < app->signal_count; row++) {
        size_t index = app->signal_offset + row;
        int y = 26 + (int)row * 8;
        if(index == app->signal_selection) {
            canvas_draw_box(canvas, 1, y - 6, 62, 8);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 3, y, app->signals[index].name);
        if(index == app->signal_selection) canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(canvas, app->status[0] ? app->status : "OK MAP  BK CANCEL");
}

void tv_remote_draw(Canvas* canvas, void* context) {
    TvRemoteApp* app = context;
    if(furi_mutex_acquire(app->state_mutex, FuriWaitForever) != FuriStatusOk) return;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    bool portrait = canvas_height(canvas) > canvas_width(canvas);
    if(portrait && app->screen == ScreenRemote) {
        draw_remote_background(canvas, app->theme);
    }
    if(app->screen == ScreenHome) {
        draw_home_screen(canvas, app);
    } else if(app->screen == ScreenThemes) {
        draw_theme_screen(canvas, app);
    } else if(portrait && app->screen == ScreenFiles) {
        draw_portrait_files(canvas, app);
    } else if(!portrait && app->screen == ScreenFiles) {
        draw_landscape_files(canvas, app);
    } else if(portrait && app->screen == ScreenRemote) {
        draw_portrait_remote(canvas, app);
    } else if(portrait) {
        draw_portrait_map(canvas, app);
    } else if(app->screen == ScreenRemote) {
        draw_remote_screen(canvas, app);
    } else {
        draw_map_screen(canvas, app);
    }

    furi_mutex_release(app->state_mutex);
}
