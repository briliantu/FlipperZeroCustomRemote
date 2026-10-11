#include <furi.h>
#include <flipper_format/flipper_format.h>
#include <infrared/worker/infrared_transmit.h>
#include <lib/infrared/signal/infrared_signal.h>
#include <ctype.h>

#include "tv_remote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef enum {
    PendingActionNone,
    PendingActionOpenList,
    PendingActionOpenRemote,
    PendingActionTransmit,
    PendingActionSaveMapping,
    PendingActionSaveRemote,
    PendingActionSaveTheme,
} PendingActionType;

typedef struct {
    PendingActionType type;
    FileListMode file_list_mode;
    size_t file_index;
    size_t signal_index;
    uint8_t key_index;
    int16_t previous_mapping;
    char remote_path[PATH_LENGTH];
} PendingAction;

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
    return length >= 3 && name[length - 3] == '.' &&
           tolower((unsigned char)name[length - 2]) == 'i' &&
           tolower((unsigned char)name[length - 1]) == 'r';
}

static int compare_names_case_insensitive(const char* left, const char* right) {
    while(*left && *right) {
        int left_char = tolower((unsigned char)*left);
        int right_char = tolower((unsigned char)*right);
        if(left_char != right_char) return left_char - right_char;
        left++;
        right++;
    }
    return tolower((unsigned char)*left) - tolower((unsigned char)*right);
}

static void update_file_offset(TvRemoteApp* app) {
    if(app->file_selection < app->file_offset) {
        app->file_offset = app->file_selection;
    } else if(app->file_selection >= app->file_offset + 4) {
        app->file_offset = app->file_selection - 3;
    }
}

static void update_signal_offset(TvRemoteApp* app) {
    if(app->signal_selection < app->signal_offset) {
        app->signal_offset = app->signal_selection;
    } else if(app->signal_selection >= app->signal_offset + 10) {
        app->signal_offset = app->signal_selection - 9;
    }
}

static void set_status(TvRemoteApp* app, const char* status) {
    if(app->state_mutex) furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    copy_string(app->status, sizeof(app->status), status);
    if(app->state_mutex) furi_mutex_release(app->state_mutex);
}

static bool mapping_path_for_remote(const char* remote_path, char* mapping_path, size_t size) {
    const char* base_name = strrchr(remote_path, '/');
    base_name = base_name ? base_name + 1 : remote_path;
    size_t base_length = strlen(base_name);
    if(base_length < 4 || base_length - 3 >= NAME_LENGTH) return false;

    char name[NAME_LENGTH];
    memcpy(name, base_name, base_length - 3);
    name[base_length - 3] = '\0';
    int path_length = snprintf(mapping_path, size, "%s/%s.map", MAPPING_DIRECTORY, name);
    return path_length >= 0 && (size_t)path_length < size;
}

static void scan_remote_files(TvRemoteApp* app) {
    app->file_count = 0;
    app->file_selection = 0;
    app->file_offset = 0;

    File* directory = storage_file_alloc(app->storage);
    if(!directory) {
        set_status(app, "Cannot allocate directory handle");
        return;
    }
    if(!storage_dir_open(directory, INFRARED_DIRECTORY)) {
        set_status(app, "Cannot open /ext/infrared");
        storage_file_free(directory);
        return;
    }

    FileInfo info;
    char name[128];
    while(storage_dir_read(directory, &info, name, sizeof(name))) {
        if((info.flags & FSF_DIRECTORY) || !has_ir_extension(name)) continue;

        RemoteFile candidate = {0};
        int path_length =
            snprintf(candidate.path, sizeof(candidate.path), "%s/%s", INFRARED_DIRECTORY, name);
        if(path_length < 0 || (size_t)path_length >= sizeof(candidate.path)) continue;
        copy_string(candidate.name, sizeof(candidate.name), name);
        if(app->file_list_mode == FileListSavedRemote) {
            char mapping_path[PATH_LENGTH];
            if(!mapping_path_for_remote(candidate.path, mapping_path, sizeof(mapping_path)) ||
               !storage_common_exists(app->storage, mapping_path)) {
                continue;
            }
        }

        size_t insert_at = 0;
        while(insert_at < app->file_count &&
              compare_names_case_insensitive(app->files[insert_at].name, candidate.name) < 0) {
            insert_at++;
        }
        if(insert_at >= MAX_REMOTE_FILES) continue;
        if(app->file_count < MAX_REMOTE_FILES) app->file_count++;
        memmove(
            &app->files[insert_at + 1],
            &app->files[insert_at],
            (app->file_count - insert_at - 1) * sizeof(RemoteFile));
        app->files[insert_at] = candidate;
    }

    storage_dir_close(directory);
    storage_file_free(directory);
    if(app->file_count) {
        set_status(app, "");
    } else if(app->file_list_mode == FileListSavedRemote) {
        set_status(app, "No saved remotes yet");
    } else {
        set_status(app, "No .ir captures in /ext/infrared");
    }
}

static bool make_mapping_path(TvRemoteApp* app, const char* remote_path) {
    return mapping_path_for_remote(remote_path, app->mapping_path, sizeof(app->mapping_path));
}

static bool load_mappings(TvRemoteApp* app) {
    for(size_t i = 0; i < KEY_COUNT; i++)
        app->mappings[i] = -1;

    File* file = storage_file_alloc(app->storage);
    if(!file) {
        set_status(app, "Cannot allocate mapping reader");
        return false;
    }
    if(!storage_file_open(file, app->mapping_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_close(file);
        storage_file_free(file);
        return true;
    }

    char contents[KEY_COUNT * 8 + 16];
    size_t bytes_read = storage_file_read(file, contents, sizeof(contents) - 1);
    storage_file_close(file);
    storage_file_free(file);
    contents[bytes_read] = '\0';

    if(strncmp(contents, "TVREMOTE1\n", 10) != 0) return true;
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
    return true;
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
        int written =
            snprintf(contents + used, sizeof(contents) - (size_t)used, "%d\n", app->mappings[key]);
        if(written < 0 || (size_t)written >= sizeof(contents) - (size_t)used) {
            set_status(app, "Mapping data too large");
            return false;
        }
        used += written;
    }

    File* file = storage_file_alloc(app->storage);
    if(!file) {
        set_status(app, "Cannot allocate mapping writer");
        return false;
    }
    bool success = storage_file_open(file, app->mapping_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        success = storage_file_write(file, contents, (size_t)used) == (size_t)used;
    }
    bool close_success = storage_file_close(file);
    success = success && close_success;
    storage_file_free(file);
    if(!success) set_status(app, "Could not save mapping");
    return success;
}

static void load_theme(TvRemoteApp* app) {
    File* file = storage_file_alloc(app->storage);
    if(!file) {
        FURI_LOG_E("TvRemote", "Failed to allocate theme settings reader");
        return;
    }
    if(!storage_file_open(file, THEME_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return;
    }

    char contents[24];
    size_t bytes_read = storage_file_read(file, contents, sizeof(contents) - 1);
    bool close_success = storage_file_close(file);
    storage_file_free(file);
    if(!close_success) {
        FURI_LOG_E("TvRemote", "Failed to close theme settings reader");
        return;
    }
    contents[bytes_read] = '\0';

    unsigned theme = 0;
    if(sscanf(contents, "TVTHEME1\n%u", &theme) == 1 && theme < ThemeCount) {
        app->theme = (RemoteTheme)theme;
    } else {
        FURI_LOG_E("TvRemote", "Invalid theme settings; using default");
    }
}

static bool save_theme(TvRemoteApp* app) {
    if(storage_common_mkdir(app->storage, "/ext/apps_data") != FSE_OK &&
       !storage_common_exists(app->storage, "/ext/apps_data")) {
        set_status(app, "Cannot create app data folder");
        return false;
    }
    if(storage_common_mkdir(app->storage, MAPPING_DIRECTORY) != FSE_OK &&
       !storage_common_exists(app->storage, MAPPING_DIRECTORY)) {
        set_status(app, "Cannot create theme folder");
        return false;
    }

    char contents[24];
    int length = snprintf(contents, sizeof(contents), "TVTHEME1\n%u\n", (unsigned)app->theme);
    if(length < 0 || (size_t)length >= sizeof(contents)) {
        set_status(app, "Theme settings too large");
        return false;
    }

    File* file = storage_file_alloc(app->storage);
    if(!file) {
        set_status(app, "Cannot allocate theme writer");
        return false;
    }
    bool success = storage_file_open(file, THEME_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        success = storage_file_write(file, contents, (size_t)length) == (size_t)length;
    }
    bool close_success = storage_file_close(file);
    storage_file_free(file);
    success = success && close_success;
    if(!success) set_status(app, "Could not save theme");
    return success;
}

static bool open_ir_file(TvRemoteApp* app, const char* path, FlipperFormat** format_out) {
    FlipperFormat* format = flipper_format_buffered_file_alloc(app->storage);
    if(!format) {
        set_status(app, "Cannot allocate IR file reader");
        return false;
    }
    if(!flipper_format_buffered_file_open_existing(format, path)) {
        flipper_format_free(format);
        set_status(app, "Cannot open IR file");
        return false;
    }

    FuriString* header = furi_string_alloc();
    if(!header) {
        flipper_format_free(format);
        set_status(app, "Cannot allocate IR file header");
        return false;
    }
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
    if(!signal || !signal_name) {
        if(signal) infrared_signal_free(signal);
        if(signal_name) furi_string_free(signal_name);
        flipper_format_free(format);
        app->signal_count = 0;
        set_status(app, "Cannot allocate signal reader");
        return false;
    }
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

    bool truncated = false;
    if(app->signal_count == MAX_SIGNALS && error == InfraredErrorCodeNone) {
        furi_string_reset(signal_name);
        error = infrared_signal_read(signal, format, signal_name);
        truncated = error == InfraredErrorCodeNone;
    }
    bool reached_end = INFRARED_ERROR_CHECK(error, InfraredErrorCodeSignalNameNotFound);
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
    bool mappings_loaded = load_mappings(app);
    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    app->screen = ScreenRemote;
    app->key_selection = 4;
    set_status(
        app,
        truncated ? "Showing first 64 signals" : (mappings_loaded ? "" : "Mappings not loaded"));
    furi_mutex_release(app->state_mutex);
    return true;
}

static void transmit_signal(
    TvRemoteApp* app,
    const char* remote_path,
    size_t signal_index,
    uint8_t key_index) {
    if(signal_index >= app->signal_count) {
        set_status(app, "No signal assigned");
        return;
    }

    FlipperFormat* format = NULL;
    if(!open_ir_file(app, remote_path, &format)) return;

    InfraredSignal* signal = infrared_signal_alloc();
    if(!signal) {
        flipper_format_free(format);
        set_status(app, "Cannot allocate signal");
        return;
    }
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
            raw->timings, (uint32_t)raw->timings_size, true, raw->frequency, raw->duty_cycle);
    } else {
        infrared_send(infrared_signal_get_message(signal), 1);
    }
    infrared_signal_free(signal);

    char status[sizeof(app->status)];
    snprintf(status, sizeof(status), "Sent %s", tv_remote_button_names[key_index]);
    set_status(app, status);
}

static const uint8_t remote_key_x[KEY_COUNT] = {0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 2, 1};
static const uint8_t remote_key_y[KEY_COUNT] = {0, 0, 1, 2, 4, 2, 1, 3, 5, 4, 6, 4, 7};

static bool move_remote_selection(TvRemoteApp* app, InputKey direction) {
    if(direction != InputKeyLeft && direction != InputKeyRight && direction != InputKeyUp &&
       direction != InputKeyDown) {
        return false;
    }
    bool horizontal = direction == InputKeyLeft || direction == InputKeyRight;
    int sign = (direction == InputKeyLeft || direction == InputKeyUp) ? -1 : 1;
    int current_x = remote_key_x[app->key_selection];
    int current_y = remote_key_y[app->key_selection];
    int best_score = INT_MAX;
    size_t best_key = app->key_selection;

    for(size_t key = 0; key < KEY_COUNT; key++) {
        if(key == app->key_selection) continue;
        int dx = (int)remote_key_x[key] - current_x;
        int dy = (int)remote_key_y[key] - current_y;
        int primary = horizontal ? dx : dy;
        int secondary = horizontal ? dy : dx;
        if(primary * sign <= 0) continue;

        int score = abs(primary) * 10 + abs(secondary) * 3;
        if(score < best_score) {
            best_score = score;
            best_key = key;
        }
    }

    if(best_key == app->key_selection) return false;
    app->key_selection = (uint8_t)best_key;
    return true;
}

static bool handle_input(TvRemoteApp* app, const InputEvent* event, PendingAction* action) {
    memset(action, 0, sizeof(*action));
    bool short_press = event->type == InputTypeShort;
    bool long_press = event->type == InputTypeLong;
    bool repeat = event->type == InputTypeRepeat;
    bool navigation = short_press || repeat;
    bool redraw = false;

    if(!short_press && !long_press && !repeat) return false;
    if(furi_mutex_acquire(app->state_mutex, FuriWaitForever) != FuriStatusOk) return false;

    if(app->screen == ScreenHome) {
        if(navigation && (event->key == InputKeyLeft || event->key == InputKeyRight)) {
            app->home_selection = app->home_selection ? 0 : 1;
            redraw = true;
        } else if(long_press && event->key == InputKeyOk) {
            app->theme_selection = app->theme;
            app->screen = ScreenThemes;
            set_status(app, "");
            redraw = true;
        } else if(short_press && event->key == InputKeyOk) {
            action->type = PendingActionOpenList;
            action->file_list_mode = app->home_selection == 0 ? FileListNewRemote :
                                                                FileListSavedRemote;
        } else if(short_press && event->key == InputKeyBack) {
            app->exit = true;
            redraw = true;
        }
    } else if(app->screen == ScreenThemes) {
        if(short_press && event->key == InputKeyBack) {
            app->screen = ScreenHome;
            set_status(app, "");
            redraw = true;
        } else if(
            navigation && (event->key == InputKeyUp || event->key == InputKeyDown ||
                           event->key == InputKeyLeft || event->key == InputKeyRight)) {
            int direction = (event->key == InputKeyUp || event->key == InputKeyLeft) ? -1 : 1;
            app->theme_selection =
                (uint8_t)((app->theme_selection + ThemeCount + direction) % ThemeCount);
            redraw = true;
        } else if(short_press && event->key == InputKeyOk) {
            app->theme = (RemoteTheme)app->theme_selection;
            app->screen = ScreenHome;
            action->type = PendingActionSaveTheme;
            redraw = true;
        }
    } else if(app->screen == ScreenFiles) {
        if(short_press && event->key == InputKeyBack) {
            app->screen = ScreenHome;
            set_status(app, "");
            redraw = true;
        } else if(navigation && event->key == InputKeyUp && app->file_selection > 0) {
            app->file_selection--;
            update_file_offset(app);
            redraw = true;
        } else if(
            navigation && event->key == InputKeyDown &&
            app->file_selection + 1 < app->file_count) {
            app->file_selection++;
            update_file_offset(app);
            redraw = true;
        } else if(short_press && event->key == InputKeyOk && app->file_count > 0) {
            action->type = PendingActionOpenRemote;
            action->file_index = app->file_selection;
        }
    } else if(app->screen == ScreenRemote) {
        if(long_press && event->key == InputKeyBack) {
            action->type = PendingActionSaveRemote;
        } else if(short_press && event->key == InputKeyBack) {
            app->screen = ScreenFiles;
            set_status(app, "");
            redraw = true;
        } else if(event->key == InputKeyOk && long_press) {
            app->screen = ScreenMap;
            app->signal_selection = app->mappings[app->key_selection] >= 0 ?
                                        (size_t)app->mappings[app->key_selection] :
                                        0;
            app->signal_offset = 0;
            update_signal_offset(app);
            redraw = true;
        } else if(short_press && event->key == InputKeyOk) {
            if(app->mappings[app->key_selection] >= 0) {
                action->type = PendingActionTransmit;
                action->signal_index = (size_t)app->mappings[app->key_selection];
                action->key_index = app->key_selection;
                copy_string(
                    action->remote_path,
                    sizeof(action->remote_path),
                    app->files[app->file_selection].path);
            } else {
                set_status(app, "Hold OK to map this button");
                redraw = true;
            }
        } else if(navigation && move_remote_selection(app, event->key)) {
            set_status(app, "");
            redraw = true;
        }
    } else if(app->screen == ScreenMap) {
        if(short_press && event->key == InputKeyBack) {
            app->screen = ScreenRemote;
            redraw = true;
        } else if(navigation && event->key == InputKeyUp && app->signal_selection > 0) {
            app->signal_selection--;
            update_signal_offset(app);
            redraw = true;
        } else if(
            navigation && event->key == InputKeyDown &&
            app->signal_selection + 1 < app->signal_count) {
            app->signal_selection++;
            update_signal_offset(app);
            redraw = true;
        } else if(short_press && event->key == InputKeyOk) {
            action->type = PendingActionSaveMapping;
            action->key_index = app->key_selection;
            action->previous_mapping = app->mappings[app->key_selection];
            app->mappings[app->key_selection] = (int16_t)app->signal_selection;
        }
    }

    furi_mutex_release(app->state_mutex);
    return redraw;
}

static bool perform_action(TvRemoteApp* app, const PendingAction* action) {
    if(action->type == PendingActionNone) return false;
    if(action->type == PendingActionOpenList) {
        app->file_list_mode = action->file_list_mode;
        scan_remote_files(app);
        furi_mutex_acquire(app->state_mutex, FuriWaitForever);
        app->screen = ScreenFiles;
        furi_mutex_release(app->state_mutex);
        return true;
    }
    if(action->type == PendingActionOpenRemote) {
        load_remote(app, action->file_index);
        return true;
    }
    if(action->type == PendingActionTransmit) {
        transmit_signal(app, action->remote_path, action->signal_index, action->key_index);
        return true;
    }
    if(action->type == PendingActionSaveRemote) {
        if(save_mappings(app)) {
            set_status(app, "Remote saved");
        }
        return true;
    }
    if(action->type == PendingActionSaveTheme) {
        if(save_theme(app)) {
            set_status(app, "Theme saved");
        }
        return true;
    }
    if(action->type == PendingActionSaveMapping) {
        if(save_mappings(app)) {
            char status[sizeof(app->status)];
            snprintf(
                status, sizeof(status), "Mapped %s", tv_remote_button_names[action->key_index]);
            furi_mutex_acquire(app->state_mutex, FuriWaitForever);
            app->screen = ScreenRemote;
            set_status(app, status);
            furi_mutex_release(app->state_mutex);
        } else {
            furi_mutex_acquire(app->state_mutex, FuriWaitForever);
            app->mappings[action->key_index] = action->previous_mapping;
            furi_mutex_release(app->state_mutex);
        }
        return true;
    }
    return false;
}

static void input_callback(InputEvent* event, void* context) {
    TvRemoteApp* app = context;
    furi_message_queue_put(app->event_queue, event, 0);
}

static void app_free(TvRemoteApp* app) {
    if(!app) return;
    if(app->view_port_attached) gui_remove_view_port(app->gui, app->view_port);
    if(app->view_port) view_port_free(app->view_port);
    if(app->event_queue) furi_message_queue_free(app->event_queue);
    if(app->state_mutex) furi_mutex_free(app->state_mutex);
    if(app->gui_open) furi_record_close(RECORD_GUI);
    if(app->storage_open) furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tv_remote_app(void* context) {
    UNUSED(context);
    TvRemoteApp* app = malloc(sizeof(TvRemoteApp));
    if(!app) {
        FURI_LOG_E("TvRemote", "Failed to allocate application state");
        return -1;
    }
    memset(app, 0, sizeof(TvRemoteApp));
    app->screen = ScreenHome;
    app->theme = ThemeNaruto;
    app->storage = furi_record_open(RECORD_STORAGE);
    app->storage_open = true;
    app->gui = furi_record_open(RECORD_GUI);
    app->gui_open = true;
    load_theme(app);
    app->state_mutex = furi_mutex_alloc(FuriMutexTypeRecursive);
    app->event_queue = furi_message_queue_alloc(EVENT_QUEUE_LENGTH, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    if(!app->state_mutex || !app->event_queue || !app->view_port) {
        FURI_LOG_E("TvRemote", "Failed to allocate application resources");
        app_free(app);
        return -1;
    }

    view_port_set_orientation(app->view_port, ViewPortOrientationHorizontal);
    view_port_draw_callback_set(app->view_port, tv_remote_draw, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    app->view_port_attached = true;

    InputEvent event;
    while(!app->exit) {
        if(furi_message_queue_get(app->event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            PendingAction action;
            bool redraw = handle_input(app, &event, &action);
            redraw = perform_action(app, &action) || redraw;
            ViewPortOrientation orientation =
                (app->screen == ScreenRemote || app->screen == ScreenMap) ?
                    ViewPortOrientationVertical :
                    ViewPortOrientationHorizontal;
            if(view_port_get_orientation(app->view_port) != orientation) {
                view_port_set_orientation(app->view_port, orientation);
                redraw = true;
            }
            if(redraw) view_port_update(app->view_port);
        }
    }

    app_free(app);
    return 0;
}
