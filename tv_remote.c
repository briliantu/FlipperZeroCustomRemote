#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <infrared/worker/infrared_transmit.h>
#include <lib/infrared/signal/infrared_signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INFRARED_DIRECTORY "/ext/infrared"
#define MAPPING_DIRECTORY "/ext/apps_data/tv_remote"
#define MAX_REMOTE_FILES 32
#define MAX_SIGNALS 64
#define PATH_LENGTH 128
#define NAME_LENGTH 32
#define KEY_COUNT 13
#define EVENT_QUEUE_LENGTH 8

typedef enum {
    ScreenFiles,
    ScreenRemote,
    ScreenMap,
} RemoteScreen;

typedef struct {
    char path[PATH_LENGTH];
    char name[NAME_LENGTH];
} RemoteFile;

typedef struct {
    char name[NAME_LENGTH];
} RemoteSignal;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    Storage* storage;
    RemoteScreen screen;
    RemoteFile files[MAX_REMOTE_FILES];
    RemoteSignal signals[MAX_SIGNALS];
    int16_t mappings[KEY_COUNT];
    size_t file_count;
    size_t signal_count;
    size_t file_selection;
    size_t file_offset;
    size_t signal_selection;
    size_t signal_offset;
    uint8_t key_selection;
    char mapping_path[PATH_LENGTH];
    char status[32];
    bool exit;
} TvRemoteApp;

static const char* const button_names[KEY_COUNT] = {
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

static const char* const button_labels[KEY_COUNT] = {
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

static void copy_string(char* destination, size_t size, const char* source) {
    if(size == 0) return;
    if(!source) source = "";
    size_t index = 0;
    while(index + 1 < size && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static bool has_ir_extension(const char* name) {
    size_t length = strlen(name);
    return length >= 3 && strcmp(name + length - 3, ".ir") == 0;
}

static void update_file_offset(TvRemoteApp* app) {
    if(app->file_selection < app->file_offset) {
        app->file_offset = app->file_selection;
    } else if(app->file_selection >= app->file_offset + 5) {
        app->file_offset = app->file_selection - 4;
    }
}

static void update_signal_offset(TvRemoteApp* app) {
    if(app->signal_selection < app->signal_offset) {
        app->signal_offset = app->signal_selection;
    } else if(app->signal_selection >= app->signal_offset + 4) {
        app->signal_offset = app->signal_selection - 3;
    }
}

static void set_status(TvRemoteApp* app, const char* status) {
    copy_string(app->status, sizeof(app->status), status);
}

static void scan_remote_files(TvRemoteApp* app) {
    app->file_count = 0;
    app->file_selection = 0;
    app->file_offset = 0;

    File* directory = storage_file_alloc(app->storage);
    if(!storage_dir_open(directory, INFRARED_DIRECTORY)) {
        set_status(app, "Cannot open /ext/infrared");
        storage_file_free(directory);
        return;
    }

    FileInfo info;
    char name[128];
    while(app->file_count < MAX_REMOTE_FILES &&
          storage_dir_read(directory, &info, name, sizeof(name))) {
        if((info.flags & FSF_DIRECTORY) || !has_ir_extension(name)) continue;

        RemoteFile* file = &app->files[app->file_count];
        int path_length = snprintf(file->path, sizeof(file->path), "%s/%s", INFRARED_DIRECTORY, name);
        if(path_length < 0 || (size_t)path_length >= sizeof(file->path)) continue;
        copy_string(file->name, sizeof(file->name), name);
        app->file_count++;
    }

    storage_dir_close(directory);
    storage_file_free(directory);
    set_status(app, app->file_count ? "" : "No .ir files in /ext/infrared");
}

static bool make_mapping_path(TvRemoteApp* app, const char* remote_path) {
    const char* base_name = strrchr(remote_path, '/');
    base_name = base_name ? base_name + 1 : remote_path;
    size_t base_length = strlen(base_name);
    if(base_length < 4 || base_length - 3 >= NAME_LENGTH) return false;

    char name[NAME_LENGTH];
    memcpy(name, base_name, base_length - 3);
    name[base_length - 3] = '\0';

    int path_length = snprintf(
        app->mapping_path,
        sizeof(app->mapping_path),
        "%s/%s.map",
        MAPPING_DIRECTORY,
        name);
    return path_length >= 0 && (size_t)path_length < sizeof(app->mapping_path);
}

static void load_mappings(TvRemoteApp* app) {
    for(size_t i = 0; i < KEY_COUNT; i++) app->mappings[i] = -1;

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, app->mapping_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_close(file);
        storage_file_free(file);
        return;
    }

    char contents[KEY_COUNT * 8 + 16];
    size_t bytes_read = storage_file_read(file, contents, sizeof(contents) - 1);
    storage_file_close(file);
    storage_file_free(file);
    contents[bytes_read] = '\0';

    if(strncmp(contents, "TVREMOTE1\n", 10) != 0) return;
    char* line = contents + 10;
    for(size_t key = 0; key < KEY_COUNT && line; key++) {
        char* next_line = strchr(line, '\n');
        if(next_line) *next_line = '\0';
        int signal_index = -1;
        if(sscanf(line, "%d", &signal_index) == 1 && signal_index >= 0 &&
           (size_t)signal_index < app->signal_count) {
            app->mappings[key] = (int16_t)signal_index;
        }
        line = next_line ? next_line + 1 : NULL;
    }
}

static bool save_mappings(TvRemoteApp* app) {
    if(storage_common_mkdir(app->storage, "/ext/apps_data") != FSE_OK &&
       !storage_common_exists(app->storage, "/ext/apps_data")) {
        set_status(app, "Cannot create app data folder");
        return false;
    }
    if(storage_common_mkdir(app->storage, MAPPING_DIRECTORY) != FSE_OK &&
       !storage_common_exists(app->storage, MAPPING_DIRECTORY)) {
        set_status(app, "Cannot create mapping folder");
        return false;
    }

    char contents[KEY_COUNT * 8 + 16];
    int used = snprintf(contents, sizeof(contents), "TVREMOTE1\n");
    if(used < 0 || (size_t)used >= sizeof(contents)) {
        set_status(app, "Mapping data too large");
        return false;
    }

    for(size_t key = 0; key < KEY_COUNT; key++) {
        int written = snprintf(
            contents + used,
            sizeof(contents) - (size_t)used,
            "%d\n",
            app->mappings[key]);
        if(written < 0 || (size_t)written >= sizeof(contents) - (size_t)used) {
            set_status(app, "Mapping data too large");
            return false;
        }
        used += written;
    }

    File* file = storage_file_alloc(app->storage);
    bool success = storage_file_open(file, app->mapping_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        success = storage_file_write(file, contents, (size_t)used) == (size_t)used;
    }
    storage_file_close(file);
    storage_file_free(file);
    if(!success) set_status(app, "Could not save mapping");
    return success;
}

static bool open_ir_file(TvRemoteApp* app, const char* path, FlipperFormat** format_out) {
    FlipperFormat* format = flipper_format_buffered_file_alloc(app->storage);
    if(!flipper_format_buffered_file_open_existing(format, path)) {
        flipper_format_free(format);
        set_status(app, "Cannot open IR file");
        return false;
    }

    FuriString* header = furi_string_alloc();
    uint32_t version = 0;
    bool valid_header = flipper_format_read_header(format, header, &version) &&
                        furi_string_equal_str(header, "IR signals file") && version == 1;
    furi_string_free(header);
    if(!valid_header) {
        flipper_format_free(format);
        set_status(app, "Unsupported .ir file");
        return false;
    }

    *format_out = format;
    return true;
}

static bool load_remote(TvRemoteApp* app, size_t file_index) {
    if(file_index >= app->file_count) return false;

    FlipperFormat* format = NULL;
    if(!open_ir_file(app, app->files[file_index].path, &format)) return false;

    app->signal_count = 0;
    InfraredSignal* signal = infrared_signal_alloc();
    FuriString* signal_name = furi_string_alloc();
    InfraredErrorCode error = InfraredErrorCodeNone;
    while(app->signal_count < MAX_SIGNALS) {
        furi_string_reset(signal_name);
        error = infrared_signal_read(signal, format, signal_name);
        if(error != InfraredErrorCodeNone) break;
        copy_string(
            app->signals[app->signal_count].name,
            sizeof(app->signals[app->signal_count].name),
            furi_string_get_cstr(signal_name));
        app->signal_count++;
    }

    bool reached_end = INFRARED_ERROR_CHECK(error, InfraredErrorCodeSignalNameNotFound);
    bool truncated = app->signal_count == MAX_SIGNALS && error == InfraredErrorCodeNone;
    furi_string_free(signal_name);
    infrared_signal_free(signal);
    flipper_format_free(format);

    if(app->signal_count == 0 || (!reached_end && !truncated)) {
        app->signal_count = 0;
        set_status(app, "Invalid or empty .ir file");
        return false;
    }

    if(!make_mapping_path(app, app->files[file_index].path)) {
        set_status(app, "Remote filename is too long");
        return false;
    }
    load_mappings(app);
    app->screen = ScreenRemote;
    app->key_selection = 4;
    set_status(app, truncated ? "Showing first 64 signals" : "");
    return true;
}

static void transmit_signal(TvRemoteApp* app, size_t signal_index) {
    if(signal_index >= app->signal_count) {
        set_status(app, "No signal assigned");
        return;
    }

    FlipperFormat* format = NULL;
    RemoteFile* remote_file = &app->files[app->file_selection];
    if(!open_ir_file(app, remote_file->path, &format)) return;

    InfraredSignal* signal = infrared_signal_alloc();
    InfraredErrorCode error =
        infrared_signal_search_by_index_and_read(signal, format, signal_index);
    flipper_format_free(format);
    if(error != InfraredErrorCodeNone) {
        infrared_signal_free(signal);
        set_status(app, "Could not read that signal");
        return;
    }

    if(infrared_signal_is_raw(signal)) {
        const InfraredRawSignal* raw = infrared_signal_get_raw_signal(signal);
        infrared_send_raw_ext(
            raw->timings,
            (uint32_t)raw->timings_size,
            true,
            raw->frequency,
            raw->duty_cycle);
    } else {
        infrared_send(infrared_signal_get_message(signal), 1);
    }
    infrared_signal_free(signal);

    char status[sizeof(app->status)];
    snprintf(status, sizeof(status), "Sent %s", button_names[app->key_selection]);
    set_status(app, status);
}

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
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 34, app->status[0] ? app->status : "Copy files to /ext/infrared");
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
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 4, y, app->files[index].name);
        if(index == app->file_selection) canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(
        canvas,
        app->status[0] ? app->status : "UP/DOWN choose  OK open  BACK exit");
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
        draw_text_centered(canvas, x + button_width / 2 - 1, y + 2, button_labels[key]);
        if(app->mappings[key] < 0) {
            canvas_draw_disc(canvas, x + button_width - 2, y + 1, 1);
        }
    }

    char footer[48];
    if(app->status[0]) {
        snprintf(footer, sizeof(footer), "%s", app->status);
    } else {
        snprintf(footer, sizeof(footer), "OK send  HOLD map  BACK files");
    }
    draw_footer(canvas, footer);
}

static void draw_map_screen(Canvas* canvas, const TvRemoteApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "MAP SIGNAL");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 18, button_names[app->key_selection]);

    for(size_t row = 0; row < 4 && app->signal_offset + row < app->signal_count; row++) {
        size_t index = app->signal_offset + row;
        int y = 30 + (int)row * 8;
        if(index == app->signal_selection) {
            canvas_draw_box(canvas, 0, y - 6, 128, 8);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 4, y, app->signals[index].name);
        if(index == app->signal_selection) canvas_set_color(canvas, ColorBlack);
    }
    draw_footer(
        canvas,
        app->status[0] ? app->status : "UP/DOWN choose  OK assign  BACK cancel");
}

static void draw_callback(Canvas* canvas, void* context) {
    const TvRemoteApp* app = context;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(app->screen == ScreenFiles) {
        draw_files_screen(canvas, app);
    } else if(app->screen == ScreenRemote) {
        draw_remote_screen(canvas, app);
    } else {
        draw_map_screen(canvas, app);
    }
}

static void select_file(TvRemoteApp* app) {
    if(app->file_count == 0) return;
    if(!load_remote(app, app->file_selection)) app->screen = ScreenFiles;
}

static void handle_input(TvRemoteApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeLong) return;
    bool long_press = event->type == InputTypeLong;

    if(app->screen == ScreenFiles) {
        if(event->key == InputKeyBack && !long_press) {
            app->exit = true;
        } else if(!long_press && event->key == InputKeyUp && app->file_selection > 0) {
            app->file_selection--;
            update_file_offset(app);
        } else if(!long_press && event->key == InputKeyDown &&
                  app->file_selection + 1 < app->file_count) {
            app->file_selection++;
            update_file_offset(app);
        } else if(!long_press && event->key == InputKeyOk) {
            select_file(app);
        }
    } else if(app->screen == ScreenRemote) {
        if(event->key == InputKeyBack && !long_press) {
            app->screen = ScreenFiles;
            set_status(app, "");
        } else if(event->key == InputKeyOk && long_press) {
            app->screen = ScreenMap;
            app->signal_selection = app->mappings[app->key_selection] >= 0 ?
                                        (size_t)app->mappings[app->key_selection] :
                                        0;
            app->signal_offset = 0;
            update_signal_offset(app);
        } else if(event->key == InputKeyOk && !long_press) {
            if(app->mappings[app->key_selection] >= 0) {
                transmit_signal(app, (size_t)app->mappings[app->key_selection]);
            } else {
                set_status(app, "Hold OK to map this button");
            }
        } else if(!long_press && event->key == InputKeyLeft &&
                  app->key_selection < KEY_COUNT - 1 && app->key_selection % 3 > 0) {
            app->key_selection--;
            set_status(app, "");
        } else if(!long_press && event->key == InputKeyRight &&
                  app->key_selection < KEY_COUNT - 1 && app->key_selection % 3 < 2) {
            app->key_selection++;
            set_status(app, "");
        } else if(!long_press && event->key == InputKeyUp &&
                  app->key_selection == KEY_COUNT - 1) {
            app->key_selection = 10;
            set_status(app, "");
        } else if(!long_press && event->key == InputKeyUp && app->key_selection >= 3) {
            app->key_selection -= 3;
            set_status(app, "");
        } else if(!long_press && event->key == InputKeyDown &&
                  app->key_selection >= 9 && app->key_selection <= 11) {
            app->key_selection = KEY_COUNT - 1;
            set_status(app, "");
        } else if(!long_press && event->key == InputKeyDown && app->key_selection + 3 < 12) {
            app->key_selection += 3;
            set_status(app, "");
        }
    } else {
        if(event->key == InputKeyBack && !long_press) {
            app->screen = ScreenRemote;
        } else if(!long_press && event->key == InputKeyUp && app->signal_selection > 0) {
            app->signal_selection--;
            update_signal_offset(app);
        } else if(!long_press && event->key == InputKeyDown &&
                  app->signal_selection + 1 < app->signal_count) {
            app->signal_selection++;
            update_signal_offset(app);
        } else if(!long_press && event->key == InputKeyOk) {
            app->mappings[app->key_selection] = (int16_t)app->signal_selection;
            if(save_mappings(app)) {
                app->screen = ScreenRemote;
                char status[sizeof(app->status)];
                snprintf(status, sizeof(status), "Mapped %s", button_names[app->key_selection]);
                set_status(app, status);
            }
        }
    }
}

static void input_callback(InputEvent* event, void* context) {
    TvRemoteApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

static void app_free(TvRemoteApp* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tv_remote_app(void* context) {
    UNUSED(context);
    TvRemoteApp* app = malloc(sizeof(TvRemoteApp));
    furi_check(app);
    memset(app, 0, sizeof(TvRemoteApp));
    app->screen = ScreenFiles;
    app->storage = furi_record_open(RECORD_STORAGE);
    app->event_queue = furi_message_queue_alloc(EVENT_QUEUE_LENGTH, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    app->gui = furi_record_open(RECORD_GUI);

    scan_remote_files(app);
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    while(!app->exit) {
        if(furi_message_queue_get(app->event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            handle_input(app, &event);
            view_port_update(app->view_port);
        }
    }

    app_free(app);
    return 0;
}
