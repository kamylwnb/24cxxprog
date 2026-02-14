#include "furi.h"
#include "furi_hal.h"
#include <gui/gui.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <furi_hal_i2c.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>
#include <furi_hal_bus.h>
#include <storage/storage.h>
#include <toolbox/path.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include "i2c_24c02.hpp"
#include "i2c_24c02_startup.h"

#define EEPROM_APP_DIR "/ext/24cxxprog"

// UI Layout constants (based on ui_design_prompt.md)
#define UI_MARGIN_LEFT   2
#define UI_MARGIN_TOP    10
#define UI_HEADER_Y      10
#define UI_STATUS_Y1     24
#define UI_STATUS_Y2     36
#define UI_CONTENT_Y     44
#define UI_BUTTONS_Y     56
#define UI_SCREEN_WIDTH  128
#define UI_SCREEN_HEIGHT 64

// Application states
typedef enum {
    AppState_Main,
    AppState_Read,
    AppState_Write,
    AppState_LoadFile,
    AppState_ConfirmLoad,
    AppState_SaveFile,
    AppState_Delete,
    AppState_Erase,
    AppState_Settings,
    AppState_About,
} AppState;

// Menu items
typedef enum {
    MainItem_Read,
    MainItem_Write,
    MainItem_LoadFile,
    MainItem_SaveFile,
    MainItem_Delete,
    MainItem_Erase,
    MainItem_Settings,
    MainItem_About,
    MainItem_Count
} MainItem;

// EEPROM chip types with different sizes
typedef enum {
    EEPROMType_24C01, // 128 bytes (1Kb)
    EEPROMType_24C02, // 256 bytes (2Kb)
    EEPROMType_24C04, // 512 bytes (4Kb)
    EEPROMType_24C08, // 1024 bytes (8Kb)
    EEPROMType_24C16, // 2048 bytes (16Kb)
    EEPROMType_24C32, // 4096 bytes (32Kb)
    EEPROMType_24C64, // 8192 bytes (64Kb)
    EEPROMType_24C128, // 16384 bytes (128Kb)
    EEPROMType_24C256, // 32768 bytes (256Kb)
    EEPROMType_24C512, // 65536 bytes (512Kb)
    EEPROMType_Count
} EEPROMType;

// Settings items
typedef enum {
    SettingsItem_Address,
    SettingsItem_ViewMode,
    SettingsItem_ChipType,
    SettingsItem_Count
} SettingsItem;

// View modes for memory display
typedef enum {
    ViewMode_Hex,
    ViewMode_Bit,
    ViewMode_Both
} ViewMode;

// Application structure
typedef struct {
    // Basic system objects
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex;

    // Application state
    AppState current_state;
    uint8_t main_cursor;
    uint8_t settings_cursor;
    uint8_t save_file_cursor;

    // EEPROM interface
    EEPROM24C02* eeprom;
    uint8_t i2c_address;
    bool eeprom_connected;
    EEPROMType chip_type; // New field for chip type selection

    // Memory data
    uint8_t memory_data[256];
    uint8_t current_address;
    uint8_t view_mode;

    // Read/Write operations
    uint8_t read_start_addr;
    uint8_t read_length;
    uint8_t write_start_addr;
    uint8_t write_data[16];
    uint8_t write_length;
    uint8_t write_cursor;

    // UI state
    bool operation_success;
    bool show_message;
    char message_text[64];
    uint32_t message_timer;

    // Progress bar for erase operation
    bool show_progress;
    uint8_t progress_value;
    uint32_t progress_timer;

    // Async erase operation
    bool erasing;
    uint8_t erase_current_addr;
    uint32_t erase_last_update;

    // Async read operation
    bool reading;
    uint8_t read_current_addr;
    uint32_t read_last_update;
    uint8_t read_total_bytes;

    // File operations
    char file_path[256];
    bool file_loaded;
    uint8_t file_data[256];
    uint8_t file_size;

    // Load confirmation dialog
    bool confirm_load_yes; // For Yes/No selection in confirmation dialog

    // Save to file operations
    bool save_mode;
    char save_path[256];

    // File browser
    char* file_list[64];
    uint8_t file_count;
    uint8_t file_cursor;
    bool browsing_files;
    char current_directory[256];
    bool show_hidden_files;
    char filename_input[32];
    bool inputting_filename;
    uint8_t filename_cursor;

    bool running;
    bool dark_mode;
} EEPROMApp;

// Function prototypes
static void draw_main_screen(Canvas* canvas, EEPROMApp* app);
static void draw_read_screen(Canvas* canvas, EEPROMApp* app);
static void draw_write_screen(Canvas* canvas, EEPROMApp* app);
static void draw_load_file_screen(Canvas* canvas, EEPROMApp* app);
static void draw_save_file_screen(Canvas* canvas, EEPROMApp* app);
static void draw_erase_screen(Canvas* canvas, EEPROMApp* app);
static void draw_settings_screen(Canvas* canvas, EEPROMApp* app);
static void draw_about_screen(Canvas* canvas, EEPROMApp* app);
static void eeprom_draw_callback(Canvas* canvas, void* context);
static void eeprom_input_callback(InputEvent* input_event, void* context);
static EEPROMApp* eeprom_app_alloc();
static void eeprom_app_free(EEPROMApp* app);
static void show_message(EEPROMApp* app, const char* message, bool success);
static bool read_memory_range(EEPROMApp* app);
static bool save_memory_to_file(EEPROMApp* app);
static void scan_directory(EEPROMApp* app, const char* path);
static void free_file_list(EEPROMApp* app);
static bool is_valid_extension(const char* filename);
static void add_directory_entry(EEPROMApp* app, const char* path, const char* name, bool is_dir);
static bool load_file_from_sd(EEPROMApp* app);
static void generate_filename(EEPROMApp* app, char* buffer, size_t buffer_size);
static bool erase_memory_range(EEPROMApp* app, uint8_t start_addr, uint8_t length);
static bool write_memory_data(EEPROMApp* app);
static void ensure_app_directory(EEPROMApp* app);
static void process_erase_step(EEPROMApp* app);
static void process_read_step(EEPROMApp* app);

// New function for confirmation dialog
static void draw_confirm_load_screen(Canvas* canvas, EEPROMApp* app);

// Main screen drawing
static void draw_main_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    const char* menu_items[] = {
        "Read", "Write", "Load File", "Save File", "Delete", "Erase", "Settings", "About"};

    size_t position = app->main_cursor;

    // Calculate which 3 items to show (sliding window without wrap-around)
    size_t first_visible = 0;
    if(position > 0) {
        first_visible = position - 1;
    }
    // Don't scroll past the last item
    if(first_visible + 3 > MainItem_Count) {
        first_visible = MainItem_Count - 3;
    }

    canvas_set_font(canvas, FontSecondary);

    // Draw 3 visible items
    for(size_t i = 0; i < 3 && (first_visible + i) < MainItem_Count; i++) {
        size_t item_idx = first_visible + i;
        uint8_t y_pos;

        if(i == 0)
            y_pos = 12;
        else if(i == 1)
            y_pos = 28;
        else
            y_pos = 44;

        if(item_idx == position) {
            // Current item - large font with frame
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, 6, y_pos, menu_items[item_idx]);
            canvas_set_font(canvas, FontSecondary);

            // Frame around current item
            elements_frame(canvas, 0, y_pos - 13, 123, 18);
        } else {
            // Other items - small font
            canvas_draw_str(canvas, 6, y_pos, menu_items[item_idx]);
        }
    }

    // Scrollbar
    elements_scrollbar(canvas, position, MainItem_Count);

    // Button
    elements_button_center(canvas, "OK");
}

// Read screen drawing
static void draw_read_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Read Memory");

    // Process async read step if needed
    if(app->reading) {
        process_read_step(app);
    }

    canvas_set_font(canvas, FontSecondary);

    // Show progress bar if reading
    if(app->show_progress && app->reading) {
        canvas_draw_str(canvas, 2, 24, "Reading EEPROM...");

        // Progress bar
        canvas_draw_frame(canvas, 12, 32, 100, 7);
        uint8_t fill_width = (app->progress_value * 98) / app->read_total_bytes;
        if(fill_width > 0) {
            canvas_draw_box(canvas, 13, 33, fill_width, 5);
        }

        // Progress percentage
        char progress_text[16];
        snprintf(
            progress_text,
            sizeof(progress_text),
            "%d%%",
            (app->progress_value * 100) / app->read_total_bytes);
        canvas_draw_str(canvas, 54, 48, progress_text);
    } else {
        // Display memory data - HEX dump (max 3 lines)
        char display_line[32];

        for(uint8_t i = 0; i < 3 && (app->current_address + i * 4) < 256; i++) {
            uint8_t addr = app->current_address + i * 4;
            snprintf(
                display_line,
                sizeof(display_line),
                "0x%02X: %02X %02X %02X %02X",
                addr,
                app->memory_data[addr],
                app->memory_data[addr + 1],
                app->memory_data[addr + 2],
                app->memory_data[addr + 3]);
            canvas_draw_str(canvas, 2, 22 + i * 9, display_line);
        }

        // Show message if needed
        if(app->show_message && furi_get_tick() < app->message_timer) {
            canvas_draw_str(canvas, 2, 48, app->message_text);
        }
    }

    // Buttons
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Read");
}

// Write screen drawing
static void draw_write_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Write Byte");

    canvas_set_font(canvas, FontSecondary);

    // Show write address with selection indicator
    char addr_line[32];
    if(app->write_cursor == 0) {
        snprintf(addr_line, sizeof(addr_line), "> Address: 0x%02X", app->write_start_addr);
    } else {
        snprintf(addr_line, sizeof(addr_line), "  Address: 0x%02X", app->write_start_addr);
    }
    canvas_draw_str(canvas, 2, 24, addr_line);

    // Show data to write with selection indicator
    char data_line[32];
    if(app->write_cursor == 1) {
        snprintf(data_line, sizeof(data_line), "> Data:    0x%02X", app->write_data[0]);
    } else {
        snprintf(data_line, sizeof(data_line), "  Data:    0x%02X", app->write_data[0]);
    }
    canvas_draw_str(canvas, 2, 34, data_line);

    // Instructions
    canvas_draw_str(canvas, 2, 46, "Up/Down: Change value");

    // Buttons
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Write");
}

// Load file screen drawing
static void draw_load_file_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Load File");

    canvas_set_font(canvas, FontSecondary);

    if(app->inputting_filename) {
        // Filename input mode
        char display_input[35];
        snprintf(display_input, sizeof(display_input), "[%s]", app->filename_input);
        canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, display_input);

        // Show cursor
        char cursor_str[35] = {0};
        if(app->filename_cursor < strlen(app->filename_input)) {
            for(uint8_t i = 0; i < app->filename_cursor; i++) {
                cursor_str[i] = ' ';
            }
            cursor_str[app->filename_cursor] = '^';
        }
        canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignTop, cursor_str);
    } else if(app->browsing_files) {
        // File browser with scrollbar (3 items visible)
        uint8_t scroll_height = 35;
        uint8_t scroll_y = 13;
        uint8_t items_per_page = 3;
        uint8_t start_item = (app->file_cursor >= 1) ? (app->file_cursor - 1) : 0;
        if(start_item + items_per_page > app->file_count) {
            start_item = (app->file_count > items_per_page) ? (app->file_count - items_per_page) :
                                                              0;
        }

        uint8_t slider_height = (items_per_page * scroll_height) / app->file_count;
        if(slider_height < 3) slider_height = 3;
        uint8_t max_sp = scroll_height - slider_height;
        uint8_t denom = (app->file_count > items_per_page) ? (app->file_count - items_per_page) :
                                                             1;
        uint8_t slider_pos = (start_item * max_sp) / denom;
        if(slider_pos > max_sp) slider_pos = max_sp;

        canvas_draw_frame(canvas, 120, scroll_y, 3, scroll_height);
        canvas_draw_box(canvas, 121, scroll_y + slider_pos, 1, slider_height);

        // Draw visible files (3 items)
        for(uint8_t i = 0; i < items_per_page && (start_item + i) < app->file_count; i++) {
            uint8_t file_idx = start_item + i;
            uint8_t y = 18 + (i * 11);

            // Extract filename from path
            char* filename = strrchr(app->file_list[file_idx], '/');
            if(filename)
                filename++;
            else
                filename = app->file_list[file_idx];

            if(app->file_cursor == file_idx) {
                // Selected: inverted
                canvas_draw_box(canvas, 0, y - 3, 118, 11);
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_str(canvas, 2, y + 5, filename);
                canvas_set_color(canvas, ColorBlack);
            } else {
                // Normal
                canvas_draw_str(canvas, 2, y + 5, filename);
            }
        }
    } else {
        // Show file path or instructions
        if(app->file_path[0] == '\0') {
            canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, "No file loaded");
        } else {
            // Truncate long paths
            char display_path[32];
            strncpy(display_path, app->file_path, 31);
            display_path[31] = '\0';
            canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, display_path);
        }

        // Show file status
        if(app->file_loaded) {
            char size_info[32];
            snprintf(size_info, sizeof(size_info), "Size: %d bytes", app->file_size);
            canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignTop, size_info);
        }
    }

    // Buttons
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Browse");
}

// Save file screen drawing
static void draw_save_file_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Save File");

    canvas_set_font(canvas, FontSecondary);

    // Show auto-save info
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, "Auto-save with timestamp:");

    // Show sample filename
    char sample_filename[64];
    generate_filename(app, sample_filename, sizeof(sample_filename));
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignTop, sample_filename);

    // Buttons
    elements_button_left(canvas, "Wroc");
    elements_button_center(canvas, "Zapisz");
}

// Erase screen drawing
static void draw_erase_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Erase Memory");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "Erase all to 0xFF");

    // Process async erase step if needed
    if(app->erasing) {
        process_erase_step(app);
    }

    // Show progress bar if erasing
    if(app->show_progress) {
        // Progress bar
        canvas_draw_frame(canvas, 12, 34, 100, 7);
        uint8_t fill_width = (app->progress_value * 98) / 255;
        if(fill_width > 0) {
            canvas_draw_box(canvas, 13, 35, fill_width, 5);
        }

        // Progress percentage
        char progress_text[16];
        snprintf(progress_text, sizeof(progress_text), "%d%%", (app->progress_value * 100) / 255);
        canvas_draw_str(canvas, 54, 46, progress_text);
    } else {
        // Show message if needed
        if(app->show_message && furi_get_tick() < app->message_timer) {
            canvas_draw_str(canvas, 2, 36, app->message_text);
        }
        // Buttons
        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Erase");
    }
}

// Settings screen drawing
static void draw_settings_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Settings");

    canvas_set_font(canvas, FontSecondary);

    uint8_t start_item = (app->settings_cursor / 3) * 3;
    uint8_t scroll_height = 35;
    uint8_t scroll_y = 13;
    uint8_t slider_height = (3 * scroll_height) / SettingsItem_Count;
    if(slider_height < 3) slider_height = 3;
    uint8_t max_sp = scroll_height - slider_height;
    uint8_t denom = (SettingsItem_Count > 3) ? (SettingsItem_Count - 3) : 1;
    uint8_t slider_pos = (start_item * max_sp) / denom;
    if(slider_pos > max_sp) slider_pos = max_sp;

    canvas_draw_frame(canvas, 120, scroll_y, 3, scroll_height);
    canvas_draw_box(canvas, 121, scroll_y + slider_pos, 1, slider_height);

    for(uint8_t i = 0; i < 3 && (start_item + i) < SettingsItem_Count; i++) {
        uint8_t ci = start_item + i;
        uint8_t y = 18 + (i * 11);

        if(app->settings_cursor == ci) {
            canvas_draw_box(canvas, 0, y - 3, 118, 14);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }

        if(app->settings_cursor == ci) {
            canvas_draw_str(canvas, 1, y + 6, ">");
        }

        switch(ci) {
        case SettingsItem_Address: {
            char addr_str[16];
            snprintf(addr_str, sizeof(addr_str), "0x%02X", app->i2c_address);
            canvas_draw_str(canvas, 5, y + 5, "I2C:");
            canvas_draw_str_aligned(canvas, 113, y - 1, AlignRight, AlignTop, addr_str);
            break;
        }
        case SettingsItem_ViewMode:
            canvas_draw_str(canvas, 5, y + 5, "View:");
            canvas_draw_str_aligned(canvas, 113, y - 1, AlignRight, AlignTop, "Hex");
            break;
        case SettingsItem_ChipType: {
            const char* chip_types[] = {
                "24C01", // 128B
                "24C02", // 256B
                "24C04", // 512B
                "24C08", // 1KB
                "24C16", // 2KB
                "24C32", // 4KB
                "24C64", // 8KB
                "24C128", // 16KB
                "24C256", // 32KB
                "24C512" // 64KB
            };
            canvas_draw_str(canvas, 5, y + 5, "Chip:");
            canvas_draw_str_aligned(
                canvas, 113, y - 1, AlignRight, AlignTop, chip_types[app->chip_type]);
            break;
        }
        default:
            break;
        }

        canvas_set_color(canvas, ColorBlack);
    }

    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "OK");
}

// Confirmation dialog for loading file to EEPROM
static void draw_confirm_load_screen(Canvas* canvas, EEPROMApp* app) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Load to EEPROM?");

    canvas_set_font(canvas, FontSecondary);

    // Show filename
    char* filename = strrchr(app->file_path, '/');
    if(filename)
        filename++;
    else
        filename = app->file_path;

    char display_name[32];
    strncpy(display_name, filename, sizeof(display_name) - 1);
    display_name[sizeof(display_name) - 1] = '\0';

    canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignTop, "File:");
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, display_name);

    // Show Yes/No buttons
    if(app->confirm_load_yes) {
        // YES selected
        canvas_draw_box(canvas, 20, 42, 35, 11);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 30, 50, "YES");
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str(canvas, 78, 50, "NO");
    } else {
        // NO selected
        canvas_draw_str(canvas, 30, 50, "YES");
        canvas_draw_box(canvas, 72, 42, 35, 11);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 80, 50, "NO");
    }

    canvas_set_color(canvas, ColorBlack);
    elements_button_center(canvas, "OK");
}

// About screen drawing
static void draw_about_screen(Canvas* canvas, EEPROMApp* app) {
    UNUSED(app);
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "About");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignTop, "24C02 EEPROM");
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, "Programmer");
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, "Author: Dr Mosfet");
    canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignTop, "I2C Memory Tool");

    elements_button_left(canvas, "Back");
}

// Main drawing callback
static void eeprom_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    EEPROMApp* app = static_cast<EEPROMApp*>(context);

    switch(app->current_state) {
    case AppState_Main:
        draw_main_screen(canvas, app);
        break;
    case AppState_Read:
        draw_read_screen(canvas, app);
        break;
    case AppState_Write:
        draw_write_screen(canvas, app);
        break;
    case AppState_LoadFile:
        draw_load_file_screen(canvas, app);
        break;
    case AppState_ConfirmLoad:
        draw_confirm_load_screen(canvas, app);
        break;
    case AppState_SaveFile:
        draw_save_file_screen(canvas, app);
        break;
    case AppState_Delete:
        draw_load_file_screen(canvas, app); // Reuse load file screen for delete
        break;
    case AppState_Erase:
        draw_erase_screen(canvas, app);
        break;
    case AppState_Settings:
        draw_settings_screen(canvas, app);
        break;
    case AppState_About:
        draw_about_screen(canvas, app);
        break;
    }
}

// Input callback
static void eeprom_input_callback(InputEvent* input_event, void* context) {
    furi_assert(context);
    EEPROMApp* app = static_cast<EEPROMApp*>(context);

    if(input_event->type == InputTypeShort || input_event->type == InputTypeRepeat) {
        switch(app->current_state) {
        case AppState_Main:
            if(input_event->key == InputKeyUp) {
                if(app->main_cursor > 0) app->main_cursor--;
            } else if(input_event->key == InputKeyDown) {
                if(app->main_cursor < MainItem_Count - 1) app->main_cursor++;
            } else if(input_event->key == InputKeyOk) {
                switch(app->main_cursor) {
                case MainItem_Read:
                    app->current_state = AppState_Read;
                    read_memory_range(app);
                    break;
                case MainItem_Write:
                    app->current_state = AppState_Write;
                    app->write_cursor = 0;
                    break;
                case MainItem_LoadFile:
                    app->current_state = AppState_LoadFile;
                    // Automatically start browsing files
                    ensure_app_directory(app);
                    strncpy(
                        app->current_directory,
                        EEPROM_APP_DIR,
                        sizeof(app->current_directory) - 1);
                    app->current_directory[sizeof(app->current_directory) - 1] = '\0';
                    scan_directory(app, app->current_directory);
                    app->browsing_files = true;
                    app->file_cursor = 0;
                    break;
                case MainItem_SaveFile:
                    app->current_state = AppState_SaveFile;
                    break;
                case MainItem_Delete:
                    app->current_state = AppState_Delete;
                    app->browsing_files = true;
                    ensure_app_directory(app);
                    strncpy(
                        app->current_directory,
                        EEPROM_APP_DIR,
                        sizeof(app->current_directory) - 1);
                    app->current_directory[sizeof(app->current_directory) - 1] = '\0';
                    scan_directory(app, app->current_directory);
                    break;
                case MainItem_Erase:
                    app->current_state = AppState_Erase;
                    break;
                case MainItem_Settings:
                    app->current_state = AppState_Settings;
                    break;
                case MainItem_About:
                    app->current_state = AppState_About;
                    break;
                }
            }
            break;

        case AppState_Read:
            if(input_event->key == InputKeyUp) {
                if(app->current_address > 0) app->current_address -= 4;
            } else if(input_event->key == InputKeyDown) {
                if(app->current_address < 252) app->current_address += 4;
            } else if(input_event->key == InputKeyOk) {
                read_memory_range(app);
            } else if(input_event->key == InputKeyBack) {
                app->current_state = AppState_Main;
            }
            break;

        case AppState_Write:
            if(input_event->key == InputKeyLeft) {
                // Switch to address editing
                app->write_cursor = 0;
            } else if(input_event->key == InputKeyRight) {
                // Switch to data editing
                app->write_cursor = 1;
            } else if(input_event->key == InputKeyUp) {
                if(app->write_cursor == 0) {
                    // Increase address
                    app->write_start_addr++;
                } else {
                    // Increase data byte
                    app->write_data[0]++;
                }
            } else if(input_event->key == InputKeyDown) {
                if(app->write_cursor == 0) {
                    // Decrease address
                    app->write_start_addr--;
                } else {
                    // Decrease data byte
                    app->write_data[0]--;
                }
            } else if(input_event->key == InputKeyOk) {
                // Write single byte to selected address
                write_memory_data(app);
            } else if(input_event->key == InputKeyBack) {
                app->current_state = AppState_Main;
            }
            break;

        case AppState_LoadFile:
            if(app->inputting_filename) {
                if(input_event->key == InputKeyLeft) {
                    if(app->filename_cursor > 0) app->filename_cursor--;
                } else if(input_event->key == InputKeyRight) {
                    if(app->filename_cursor < strlen(app->filename_input)) app->filename_cursor++;
                } else if(input_event->key == InputKeyUp) {
                    // Increment character at cursor
                    if(app->filename_cursor < strlen(app->filename_input)) {
                        app->filename_input[app->filename_cursor]++;
                    }
                } else if(input_event->key == InputKeyDown) {
                    // Decrement character at cursor
                    if(app->filename_cursor < strlen(app->filename_input)) {
                        app->filename_input[app->filename_cursor]--;
                    }
                } else if(input_event->key == InputKeyOk) {
                    // Accept filename and load file
                    char full_path[300];
                    int result = snprintf(
                        full_path,
                        sizeof(full_path),
                        "%s/%s",
                        app->current_directory,
                        app->filename_input);
                    if(result >= 0 && (size_t)result < sizeof(full_path)) {
                        strncpy(app->file_path, full_path, sizeof(app->file_path) - 1);
                        app->file_path[sizeof(app->file_path) - 1] = '\0';
                        app->inputting_filename = false;
                        free_file_list(app);
                        load_file_from_sd(app);
                    }
                } else if(input_event->key == InputKeyBack) {
                    // Cancel filename input
                    app->inputting_filename = false;
                }
            } else if(app->browsing_files) {
                if(input_event->key == InputKeyUp) {
                    if(app->file_cursor > 0) app->file_cursor--;
                } else if(input_event->key == InputKeyDown) {
                    if(app->file_count > 0 && app->file_cursor < app->file_count - 1)
                        app->file_cursor++;
                } else if(input_event->key == InputKeyOk) {
                    if(app->file_count > 0 && app->file_cursor < app->file_count) {
                        char* full_path = app->file_list[app->file_cursor];
                        strncpy(app->file_path, full_path, sizeof(app->file_path) - 1);
                        app->file_path[sizeof(app->file_path) - 1] = '\0';
                        app->browsing_files = false;
                        free_file_list(app);
                        load_file_from_sd(app);

                        // After loading file, show confirmation dialog
                        if(app->file_loaded) {
                            app->current_state = AppState_ConfirmLoad;
                            app->confirm_load_yes = false; // Default to NO for safety
                        }
                    }
                } else if(input_event->key == InputKeyBack) {
                    // Cancel browsing and return to main menu
                    app->browsing_files = false;
                    free_file_list(app);
                    app->current_state = AppState_Main;
                }
            } else {
                if(input_event->key == InputKeyOk) {
                    ensure_app_directory(app);
                    strncpy(
                        app->current_directory,
                        EEPROM_APP_DIR,
                        sizeof(app->current_directory) - 1);
                    app->current_directory[sizeof(app->current_directory) - 1] = '\0';
                    scan_directory(app, app->current_directory);
                    app->browsing_files = true;
                    app->file_cursor = 0;
                } else if(input_event->key == InputKeyBack) {
                    // Save current memory to default file
                    char default_filename[64];
                    snprintf(
                        default_filename,
                        sizeof(default_filename),
                        "/ext/eeprom_backup_%lu.bin",
                        (unsigned long)furi_get_tick());
                    strncpy(app->save_path, default_filename, sizeof(app->save_path) - 1);
                    app->save_path[sizeof(app->save_path) - 1] = '\0';
                    save_memory_to_file(app);
                }
            }
            break;

        case AppState_ConfirmLoad:
            if(input_event->key == InputKeyLeft) {
                app->confirm_load_yes = true;
            } else if(input_event->key == InputKeyRight) {
                app->confirm_load_yes = false;
            } else if(input_event->key == InputKeyOk) {
                if(app->confirm_load_yes) {
                    // User confirmed YES - write file data to EEPROM
                    bool success = app->eeprom->writeBytes(0, app->file_data, app->file_size);
                    if(success) {
                        show_message(app, "File written to EEPROM!", true);
                        // Copy file data to memory display
                        memcpy(app->memory_data, app->file_data, sizeof(app->memory_data));
                    } else {
                        show_message(app, "Write failed!", false);
                    }
                }
                // Return to main menu regardless of choice
                app->current_state = AppState_Main;
            } else if(input_event->key == InputKeyBack) {
                // Cancel - return to main menu without writing
                app->current_state = AppState_Main;
            }
            break;

        case AppState_Delete:
            if(app->browsing_files) {
                if(input_event->key == InputKeyUp) {
                    if(app->file_cursor > 0) app->file_cursor--;
                } else if(input_event->key == InputKeyDown) {
                    if(app->file_count > 0 && app->file_cursor < app->file_count - 1)
                        app->file_cursor++;
                } else if(input_event->key == InputKeyOk) {
                    if(app->file_count > 0 && app->file_cursor < app->file_count) {
                        char* full_path = app->file_list[app->file_cursor];
                        Storage* storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));
                        if(storage_simply_remove(storage, full_path)) {
                            show_message(app, "File deleted!", true);
                        }
                        furi_record_close(RECORD_STORAGE);

                        // Refresh file list
                        free_file_list(app);
                        scan_directory(app, app->current_directory);
                    }
                } else if(input_event->key == InputKeyBack) {
                    app->browsing_files = false;
                    free_file_list(app);
                    app->current_state = AppState_Main;
                }
            } else {
                if(input_event->key == InputKeyOk) {
                    ensure_app_directory(app);
                    strncpy(
                        app->current_directory,
                        EEPROM_APP_DIR,
                        sizeof(app->current_directory) - 1);
                    app->current_directory[sizeof(app->current_directory) - 1] = '\0';
                    scan_directory(app, app->current_directory);
                    app->browsing_files = true;
                } else if(input_event->key == InputKeyBack) {
                    app->current_state = AppState_Main;
                }
            }
            break;

        case AppState_SaveFile:
            if(app->inputting_filename) {
                // Old method - deprecated
                if(input_event->key == InputKeyLeft) {
                    if(app->filename_cursor > 0) app->filename_cursor--;
                } else if(input_event->key == InputKeyRight) {
                    if(app->filename_cursor < strlen(app->filename_input)) app->filename_cursor++;
                } else if(input_event->key == InputKeyUp) {
                    // Increment character at cursor
                    if(app->filename_cursor < strlen(app->filename_input)) {
                        app->filename_input[app->filename_cursor]++;
                    }
                } else if(input_event->key == InputKeyDown) {
                    // Decrement character at cursor
                    if(app->filename_cursor < strlen(app->filename_input)) {
                        app->filename_input[app->filename_cursor]--;
                    }
                } else if(input_event->key == InputKeyOk) {
                    // Accept filename and save file
                    char full_path[300];
                    int result = snprintf(
                        full_path,
                        sizeof(full_path),
                        "%s/%s",
                        app->current_directory,
                        app->filename_input);
                    if(result >= 0 && (size_t)result < sizeof(full_path)) {
                        strncpy(app->save_path, full_path, sizeof(app->save_path) - 1);
                        app->save_path[sizeof(app->save_path) - 1] = '\0';
                        app->inputting_filename = false;
                        free_file_list(app);
                        save_memory_to_file(app);
                    }
                } else if(input_event->key == InputKeyBack) {
                    // Cancel filename input
                    app->inputting_filename = false;
                }
            } else if(app->browsing_files) {
                if(input_event->key == InputKeyUp) {
                    if(app->file_cursor > 0) app->file_cursor--;
                } else if(input_event->key == InputKeyDown) {
                    if(app->file_count > 0 && app->file_cursor < app->file_count - 1)
                        app->file_cursor++;
                } else if(input_event->key == InputKeyOk) {
                    if(app->file_count > 0 && app->file_cursor < app->file_count) {
                        char* full_path = app->file_list[app->file_cursor];
                        char* filename = strrchr(full_path, '/');
                        filename = filename ? (filename + 1) : full_path;
                        strncpy(app->filename_input, filename, sizeof(app->filename_input) - 1);
                        app->filename_input[sizeof(app->filename_input) - 1] = '\0';
                        app->filename_cursor = strlen(app->filename_input);
                        app->inputting_filename = true;
                    }
                } else if(input_event->key == InputKeyBack) {
                    // Cancel browsing
                    app->browsing_files = false;
                    free_file_list(app);
                }
            } else {
                if(input_event->key == InputKeyOk) {
                    // Generate filename with chip model, date and time and save directly
                    ensure_app_directory(app);
                    strncpy(
                        app->current_directory,
                        EEPROM_APP_DIR,
                        sizeof(app->current_directory) - 1);
                    app->current_directory[sizeof(app->current_directory) - 1] = '\0';

                    // Generate automatic filename
                    char auto_filename[64];
                    generate_filename(app, auto_filename, sizeof(auto_filename));

                    // Create full path and save
                    char full_path[300];
                    int result = snprintf(
                        full_path,
                        sizeof(full_path),
                        "%s/%s",
                        app->current_directory,
                        auto_filename);
                    if(result >= 0 && (size_t)result < sizeof(full_path)) {
                        strncpy(app->save_path, full_path, sizeof(app->save_path) - 1);
                        app->save_path[sizeof(app->save_path) - 1] = '\0';
                        save_memory_to_file(app);
                    }
                } else if(input_event->key == InputKeyBack) {
                    app->current_state = AppState_Main;
                }
            }
            break;

        case AppState_Erase:
            if(input_event->key == InputKeyOk) {
                erase_memory_range(app, 0, 255);
            } else if(input_event->key == InputKeyBack) {
                app->current_state = AppState_Main;
            }
            break;

        case AppState_Settings:
            if(input_event->key == InputKeyUp) {
                if(app->settings_cursor > 0) app->settings_cursor--;
            } else if(input_event->key == InputKeyDown) {
                if(app->settings_cursor < SettingsItem_Count - 1) app->settings_cursor++;
            } else if(input_event->key == InputKeyLeft || input_event->key == InputKeyRight) {
                if(app->settings_cursor == SettingsItem_Address) {
                    if(input_event->key == InputKeyLeft) {
                        if(app->i2c_address > EEPROM_24C02_BASE_ADDR) app->i2c_address--;
                    } else {
                        if(app->i2c_address < EEPROM_24C02_MAX_ADDR) app->i2c_address++;
                    }
                    app->eeprom->setAddress(app->i2c_address);
                } else if(app->settings_cursor == SettingsItem_ChipType) {
                    if(input_event->key == InputKeyLeft) {
                        if(app->chip_type > (EEPROMType)0)
                            app->chip_type = (EEPROMType)(app->chip_type - 1);
                    } else {
                        if(app->chip_type < (EEPROMType)(EEPROMType_Count - 1))
                            app->chip_type = (EEPROMType)(app->chip_type + 1);
                    }
                    // TODO: Reinitialize EEPROM with new chip type
                }
            } else if(input_event->key == InputKeyOk) {
                // Test connection
                app->eeprom_connected = app->eeprom->isAvailable();
                show_message(
                    app,
                    app->eeprom_connected ? "Connected!" : "Not Connected",
                    app->eeprom_connected);
            } else if(input_event->key == InputKeyBack) {
                app->current_state = AppState_Main;
            }
            break;

        case AppState_About:
            if(input_event->key == InputKeyOk || input_event->key == InputKeyBack) {
                app->current_state = AppState_Main;
            }
            break;
        }
    }

    if(input_event->type == InputTypeLong && input_event->key == InputKeyBack) {
        app->running = false;
    }
}

// Show message function
static void show_message(EEPROMApp* app, const char* message, bool success) {
    strncpy(app->message_text, message, sizeof(app->message_text) - 1);
    app->message_text[sizeof(app->message_text) - 1] = '\0';
    app->operation_success = success;
    app->show_message = true;
    app->message_timer = furi_get_tick() + 2000; // Show for 2 seconds
}

// Generate filename with chip model, date and time
static void generate_filename(EEPROMApp* app, char* buffer, size_t buffer_size) {
    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);

    const char* chip_name = "24C02";
    if(app->chip_type == EEPROMType_24C04)
        chip_name = "24C04";
    else if(app->chip_type == EEPROMType_24C08)
        chip_name = "24C08";
    else if(app->chip_type == EEPROMType_24C16)
        chip_name = "24C16";

    snprintf(
        buffer,
        buffer_size,
        "%s_%04d%02d%02d_%02d%02d%02d.bin",
        chip_name,
        datetime.year,
        datetime.month,
        datetime.day,
        datetime.hour,
        datetime.minute,
        datetime.second);
}

// Process async erase step - called from draw callback
static void process_erase_step(EEPROMApp* app) {
    uint32_t current_time = furi_get_tick();

    // Process one chunk every 50ms to show progress
    if(current_time - app->erase_last_update >= 50) {
        if(app->erase_current_addr >= 255) {
            // Erase completed
            app->erasing = false;
            app->show_progress = false;
            show_message(app, "Erase Success!", true);
            return;
        }
        if(app->erase_current_addr >= 255) {
            // Erase completed
            app->erasing = false;
            app->show_progress = false;
            show_message(app, "Erase Success!", true);
            return;
        }

        // Erase one chunk
        uint8_t chunk_size =
            (app->erase_current_addr + 8 <= 255) ? 8 : (255 - app->erase_current_addr);
        uint8_t erase_data[8];

        for(uint8_t j = 0; j < chunk_size; j++) {
            erase_data[j] = 0xFF;
        }

        bool success = app->eeprom->writeBytes(app->erase_current_addr, erase_data, chunk_size);
        if(!success) {
            app->erasing = false;
            app->show_progress = false;
            show_message(app, "Erase Failed!", false);
            return;
        }

        // Update progress
        app->erase_current_addr += chunk_size;
        app->progress_value = app->erase_current_addr;
        app->erase_last_update = current_time;
    }
}

// Process async read step - called from draw callback
static void process_read_step(EEPROMApp* app) {
    uint32_t current_time = furi_get_tick();

    // Process one chunk every 30ms to show progress
    if(current_time - app->read_last_update >= 30) {
        if(app->read_current_addr >= app->read_total_bytes) {
            // Read completed
            app->reading = false;
            app->show_progress = false;
            show_message(app, "", true);
            return;
        }

        // Read one chunk (16 bytes at a time for better performance)
        uint8_t chunk_size = (app->read_current_addr + 16 <= app->read_total_bytes) ?
                                 16 :
                                 (app->read_total_bytes - app->read_current_addr);

        bool success = app->eeprom->readBytes(
            app->read_current_addr, &app->memory_data[app->read_current_addr], chunk_size);
        if(!success) {
            app->reading = false;
            app->show_progress = false;
            show_message(app, "Read Failed!", false);
            return;
        }

        // Update progress
        app->read_current_addr += chunk_size;
        app->progress_value = app->read_current_addr;
        app->read_last_update = current_time;
    }
}

// Read memory range - start async read operation
static bool read_memory_range(EEPROMApp* app) {
    // Start async read of entire EEPROM
    app->reading = true;
    app->read_current_addr = 0;
    app->read_last_update = furi_get_tick();
    app->show_progress = true;
    app->progress_value = 0;
    app->read_total_bytes = 255; // Read entire EEPROM

    return true;
}

// Write memory data
static bool write_memory_data(EEPROMApp* app) {
    // Write single byte to selected address
    bool success = app->eeprom->writeBytes(app->write_start_addr, app->write_data, 1);
    char msg[64];
    snprintf(
        msg,
        sizeof(msg),
        "Write 0x%02X to 0x%02X %s",
        app->write_data[0],
        app->write_start_addr,
        success ? "OK" : "FAIL");
    show_message(app, msg, success);
    return success;
}

// Erase memory range - start async erase operation
static bool erase_memory_range(EEPROMApp* app, uint8_t start_addr, uint8_t length) {
    UNUSED(start_addr);
    UNUSED(length);

    // Start async erase
    app->erasing = true;
    app->erase_current_addr = 0;
    app->erase_last_update = furi_get_tick();
    app->show_progress = true;
    app->progress_value = 0;

    return true;
}

// Save memory to file
static bool save_memory_to_file(EEPROMApp* app) {
    Storage* storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));
    File* file = storage_file_alloc(storage);

    // Read entire EEPROM memory
    bool success = app->eeprom->readBytes(0, app->memory_data, 255);

    if(success) {
        ensure_app_directory(app);

        // Use provided path or generate default filename
        const char* save_path = app->save_path;
        if(save_path[0] == '\0') {
            // Generate default filename with timestamp
            char default_filename[64];
            snprintf(
                default_filename,
                sizeof(default_filename),
                EEPROM_APP_DIR "/eeprom_backup_%lu.bin",
                (unsigned long)furi_get_tick());
            save_path = default_filename;
        }

        success = storage_file_open(file, save_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);

        if(success) {
            success = (storage_file_write(file, app->memory_data, 255) == 255);

            if(success) {
                show_message(app, "Memory saved!", true);
                app->current_state = AppState_Main;
            } else {
                show_message(app, "Write error!", false);
            }
        } else {
            show_message(app, "Cannot create file!", false);
        }
    } else {
        show_message(app, "Read error!", false);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return success;
}

// Load file from SD card
static bool load_file_from_sd(EEPROMApp* app) {
    Storage* storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));
    File* file = storage_file_alloc(storage);

    bool success = storage_file_open(file, app->file_path, FSAM_READ, FSOM_OPEN_EXISTING);

    if(success) {
        uint64_t size = storage_file_size(file);
        if(size > 256) {
            size = 256; // Limit to EEPROM size
        }

        app->file_size = (uint8_t)size;
        success = (storage_file_read(file, app->file_data, app->file_size) == app->file_size);

        if(success) {
            app->file_loaded = true;
            // Don't show message here - will show confirmation dialog instead
        } else {
            show_message(app, "Read error!", false);
        }
    } else {
        show_message(app, "File not found!", false);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return success;
}

// Scan directory for files
static void scan_directory(EEPROMApp* app, const char* path) {
    Storage* storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));

    // Clear previous list
    free_file_list(app);

    File* directory = storage_file_alloc(storage);
    if(storage_dir_open(directory, path)) {
        char* filename = static_cast<char*>(malloc(256));
        FileInfo file_info;

        while(storage_dir_read(directory, &file_info, filename, 256)) {
            if(file_info.flags & FSF_DIRECTORY) continue;

            if(filename[0] == '.' && !app->show_hidden_files) {
                continue;
            }

            if(is_valid_extension(filename)) {
                add_directory_entry(app, path, filename, false);
            }

            if(app->file_count >= 64) break;
        }

        free(filename);
    }

    storage_dir_close(directory);
    storage_file_free(directory);
    furi_record_close(RECORD_STORAGE);
}

// Free file list memory
static void free_file_list(EEPROMApp* app) {
    for(uint8_t i = 0; i < app->file_count; i++) {
        if(app->file_list[i]) {
            free(app->file_list[i]);
            app->file_list[i] = nullptr;
        }
    }
    app->file_count = 0;
}

// Check if file has valid extension
static bool is_valid_extension(const char* filename) {
    // TEMP: Accept all files for debugging
    return true;

    char* ext = strrchr(filename, '.');
    if(!ext) return false;

    // Accept common binary/data file extensions
    return (
        strcmp(ext, ".bin") == 0 || strcmp(ext, ".hex") == 0 || strcmp(ext, ".dat") == 0 ||
        strcmp(ext, ".raw") == 0 || strcmp(ext, ".eeprom") == 0 || strcmp(ext, ".rom") == 0);
}

// Add directory entry to file list
static void add_directory_entry(EEPROMApp* app, const char* path, const char* name, bool is_dir) {
    UNUSED(is_dir);

    if(app->file_count >= 64) return; // Limit reached

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, name);

    app->file_list[app->file_count] = static_cast<char*>(malloc(strlen(full_path) + 1));
    strcpy(app->file_list[app->file_count], full_path);
    app->file_count++;
}

static void ensure_app_directory(EEPROMApp* app) {
    UNUSED(app);
    Storage* storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));
    storage_simply_mkdir(storage, EEPROM_APP_DIR);
    furi_record_close(RECORD_STORAGE);
}

// Allocate application
static EEPROMApp* eeprom_app_alloc() {
    EEPROMApp* app = static_cast<EEPROMApp*>(malloc(sizeof(EEPROMApp)));
    furi_assert(app);

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->gui = static_cast<Gui*>(furi_record_open(RECORD_GUI));
    app->view_port = view_port_alloc();

    view_port_draw_callback_set(app->view_port, eeprom_draw_callback, app);
    view_port_input_callback_set(app->view_port, eeprom_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // Initialize EEPROM
    app->i2c_address = EEPROM_24C02_BASE_ADDR;
    app->chip_type = EEPROMType_24C02; // Default to 24C02
    app->eeprom = new EEPROM24C02(app->i2c_address);
    app->eeprom_connected = app->eeprom->isAvailable();

    // Initialize state
    app->running = true;
    app->current_state = AppState_Main;
    app->main_cursor = 0;
    app->settings_cursor = 0;
    app->save_file_cursor = 0;
    app->current_address = 0;
    app->view_mode = ViewMode_Hex;
    app->dark_mode = false;
    app->show_message = false;

    // Initialize progress
    app->show_progress = false;
    app->progress_value = 0;
    app->progress_timer = 0;

    // Initialize async erase
    app->erasing = false;
    app->erase_current_addr = 0;
    app->erase_last_update = 0;

    // Initialize async read
    app->reading = false;
    app->read_current_addr = 0;
    app->read_last_update = 0;
    app->read_total_bytes = 0;

    // Initialize file operations
    app->file_path[0] = '\0';
    app->file_loaded = false;
    app->file_size = 0;

    // Initialize confirmation dialog
    app->confirm_load_yes = false;

    // Initialize save operations
    app->save_mode = false;
    app->save_path[0] = '\0';

    // Initialize file browser
    app->file_count = 0;
    app->file_cursor = 0;
    app->browsing_files = false;
    app->show_hidden_files = false;
    app->inputting_filename = false;
    app->filename_cursor = 0;
    app->filename_input[0] = '\0';
    app->current_directory[0] = '\0';
    for(uint8_t i = 0; i < 64; i++) {
        app->file_list[i] = nullptr;
    }

    // Initialize write data
    app->write_start_addr = 0;
    app->write_length = 4;
    app->write_cursor = 0;
    for(uint8_t i = 0; i < 16; i++) {
        app->write_data[i] = 0x00;
    }

    // Clear memory buffer
    for(uint16_t i = 0; i < 255; i++) {
        app->memory_data[i] = 0xFF;
    }

    return app;
}

// Free application
static void eeprom_app_free(EEPROMApp* app) {
    furi_assert(app);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(app->mutex);

    // Free file list
    free_file_list(app);

    delete app->eeprom;
    free(app);
}

// Draw startup screen callback
static void startup_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    drawScreen_1(canvas);
}

// Main application entry point
extern "C" int32_t eeprom_app_24cxx(void* p) {
    UNUSED(p);

    EEPROMApp* app = eeprom_app_alloc();

    // Show startup splash screen for 2 seconds
    view_port_draw_callback_set(app->view_port, startup_draw_callback, app);
    view_port_update(app->view_port);
    furi_delay_ms(2000);

    // Restore main draw callback
    view_port_draw_callback_set(app->view_port, eeprom_draw_callback, app);

    while(app->running) {
        view_port_update(app->view_port);
        furi_delay_ms(100);
    }

    eeprom_app_free(app);
    return 0;
}
