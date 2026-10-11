#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <storage/storage.h>

#define INFRARED_DIRECTORY  "/ext/infrared"
#define MAPPING_DIRECTORY   "/ext/apps_data/tv_remote"
#define THEME_SETTINGS_PATH MAPPING_DIRECTORY "/theme.cfg"
#define MAX_REMOTE_FILES    32
#define MAX_SIGNALS         64
#define PATH_LENGTH         128
#define NAME_LENGTH         32
#define KEY_COUNT           13
#define EVENT_QUEUE_LENGTH  8

typedef enum {
    ScreenHome,
    ScreenFiles,
    ScreenRemote,
    ScreenMap,
    ScreenThemes,
} RemoteScreen;

typedef enum {
    ThemeNaruto,
    ThemeBleach,
    ThemeDeathNote,
    ThemeHxH,
    ThemeCount,
} RemoteTheme;

typedef enum {
    FileListNewRemote,
    FileListSavedRemote,
} FileListMode;

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
    FuriMutex* state_mutex;
    Storage* storage;
    bool gui_open;
    bool storage_open;
    bool view_port_attached;
    RemoteScreen screen;
    FileListMode file_list_mode;
    RemoteTheme theme;
    uint8_t home_selection;
    uint8_t theme_selection;
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

extern const char* const tv_remote_button_names[KEY_COUNT];
extern const char* const tv_remote_button_labels[KEY_COUNT];

void tv_remote_draw(Canvas* canvas, void* context);
