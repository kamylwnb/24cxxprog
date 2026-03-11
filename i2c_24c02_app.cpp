/**
 * @file nauka.c
 * @brief Test application for Flipper Zero - Learning Demo
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <gui/elements.h>

// Application states
typedef enum {
    AppStateMenu,
    AppStateCounter,
    AppStateAbout,
} AppState;

// Application context
typedef struct {
    AppState state;
    uint32_t counter;
    uint8_t menu_cursor;
    uint32_t last_tick;
} NaukaContext;

// Draw menu screen
static void draw_menu(Canvas* canvas, NaukaContext* ctx) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignTop, "Test Menu");
    
    canvas_set_font(canvas, FontSecondary);
    
    // Menu items
    const char* items[] = {"Counter Test", "About", "Exit"};
    for(uint8_t i = 0; i < 3; i++) {
        uint8_t y = 24 + (i * 12);
        if(ctx->menu_cursor == i) {
            canvas_draw_box(canvas, 10, y - 2, 108, 11);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 14, y + 7, items[i]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 14, y + 7, items[i]);
        }
    }
    
    elements_button_center(canvas, "OK");
}

// Draw counter screen
static void draw_counter(Canvas* canvas, NaukaContext* ctx) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignTop, "Counter Test");
    
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignTop, "Press OK to count");
    
    // Display counter
    char counter_str[32];
    snprintf(counter_str, sizeof(counter_str), "Count: %lu", ctx->counter);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignTop, counter_str);
    
    // Show uptime
    uint32_t uptime_sec = (furi_get_tick() - ctx->last_tick) / 1000;
    char uptime_str[32];
    snprintf(uptime_str, sizeof(uptime_str), "Uptime: %lus", uptime_sec);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignTop, uptime_str);
    
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Count");
}

// Draw about screen
static void draw_about(Canvas* canvas, NaukaContext* ctx) {
    UNUSED(ctx);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignTop, "About");
    
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignTop, "Nauka Test App");
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignTop, "v0.1");
    canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignTop, "Learn Flipper Dev");
    
    elements_button_left(canvas, "Back");
}

// Main draw callback
static void nauka_draw_callback(Canvas* canvas, void* ctx) {
    NaukaContext* context = ctx;
    
    switch(context->state) {
        case AppStateMenu:
            draw_menu(canvas, context);
            break;
        case AppStateCounter:
            draw_counter(canvas, context);
            break;
        case AppStateAbout:
            draw_about(canvas, context);
            break;
    }
}

// Input callback
static void nauka_input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, event, FuriWaitForever);
}

int32_t nauka(void* p) {
    UNUSED(p);

    // Allocate context
    NaukaContext* context = malloc(sizeof(NaukaContext));
    context->state = AppStateMenu;
    context->counter = 0;
    context->menu_cursor = 0;
    context->last_tick = furi_get_tick();

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, nauka_draw_callback, context);
    view_port_input_callback_set(view_port, nauka_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypePress || event.type == InputTypeRepeat) {
                switch(context->state) {
                    case AppStateMenu:
                        if(event.key == InputKeyUp) {
                            if(context->menu_cursor > 0) context->menu_cursor--;
                        } else if(event.key == InputKeyDown) {
                            if(context->menu_cursor < 2) context->menu_cursor++;
                        } else if(event.key == InputKeyOk) {
                            switch(context->menu_cursor) {
                                case 0:
                                    context->state = AppStateCounter;
                                    context->counter = 0;
                                    context->last_tick = furi_get_tick();
                                    break;
                                case 1:
                                    context->state = AppStateAbout;
                                    break;
                                case 2:
                                    running = false;
                                    break;
                            }
                        } else if(event.key == InputKeyBack) {
                            running = false;
                        }
                        break;
                        
                    case AppStateCounter:
                        if(event.key == InputKeyOk) {
                            context->counter++;
                        } else if(event.key == InputKeyBack) {
                            context->state = AppStateMenu;
                        }
                        break;
                        
                    case AppStateAbout:
                        if(event.key == InputKeyBack || event.key == InputKeyOk) {
                            context->state = AppStateMenu;
                        }
                        break;
                }
            }
        }
        view_port_update(view_port);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
    free(context);

    return 0;
}
