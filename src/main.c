#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "river-layout-v3-client-protocol.h"
#include "scroller.h"
#include "dwindle.h"
#include "stack.h"

typedef enum {
    MODE_STACK,
    MODE_TILING,
    MODE_DWINDLE,
    MODE_NIRI,
    MODE_MONOCLE
} LayoutMode;

LayoutMode current_mode = MODE_STACK; // По умолчанию режим Master-Stack
float master_ratio = 0.60;            // По умолчанию Мастер занимает 60%
int scroll_window_index = 0;
static uint32_t last_view_count = 0; // Для отслеживания появления новых окон
int layout_gap = 10;                  // Единый отступ (gap) для всех тайлинговых режимов


struct river_layout_manager_v3 *layout_manager = NULL;
#define MAX_OUTPUTS 10
struct wl_output *outputs[MAX_OUTPUTS];
int output_count = 0;

// --- Обработчик IPC команд от River ---
static void layout_handle_user_command(void *data, struct river_layout_v3 *layout, const char *command) {
    (void)layout; // Чтобы компилятор не ругался на неиспользуемую переменную

    if (strcmp(command, "toggle") == 0) {
        if (current_mode == MODE_STACK) {
            current_mode = MODE_TILING;
            printf("current mod is MODE_TILING\n");
            system("notify-send -t 1500 'Layout Changed' 'Tiling (Master-Stack)' &");
        } else if (current_mode == MODE_TILING) {
            current_mode = MODE_DWINDLE;
            printf("current mod is MODE_DWINDLE\n");
            system("notify-send -t 1500 'Layout Changed' 'Dwindle (Spiral)' &");
        } else if (current_mode == MODE_DWINDLE) {
            current_mode = MODE_NIRI;
            printf("current mod is MODE_NIRI\n");
            system("notify-send -t 1500 'Layout Changed' 'Niri (Scrolling)' &");
        } else if (current_mode == MODE_NIRI) {
            current_mode = MODE_MONOCLE;
            printf("current mod is MODE_MONOCLE\n");
            system("notify-send -t 1500 'Layout Changed' 'Monocle (Fullscreen)' &");
        } else {
            current_mode = MODE_STACK;
            printf("current mod is MODE_STACK\n");
            system("notify-send -t 1500 'Layout Changed' 'Stacking (Floating)' &");
        }
    } else if (strcmp(command, "niri") == 0) {
        current_mode = MODE_NIRI;
        printf("current mod is MODE_NIRI\n");
        system("notify-send -t 1500 'Layout Changed' 'Niri (Scrolling)' &");
    } else if (strcmp(command, "tiling") == 0) {
        current_mode = MODE_TILING;
        printf("current mod is MODE_TILING\n");
        system("notify-send -t 1500 'Layout Changed' 'Tiling (Master-Stack)' &");
    } else if (strcmp(command, "stack") == 0) {
        current_mode = MODE_STACK;
        printf("current mod is MODE_STACK\n");
        system("notify-send -t 1500 'Layout Changed' 'Stacking (Floating)' &");
    } else if (strcmp(command, "dwindle") == 0) {
        current_mode = MODE_DWINDLE;
        printf("current mod is MODE_DWINDLE\n");
        system("notify-send -t 1500 'Layout Changed' 'Dwindle (Spiral)' &");
    } else if (strcmp(command, "monocle") == 0) {
        current_mode = MODE_MONOCLE;
        printf("current mod is MODE_MONOCLE\n");
        system("notify-send -t 1500 'Layout Changed' 'Monocle (Fullscreen)' &");
    } else if (strcmp(command, "scroll-right") == 0) {
        scroll_window_index++; // Сдвигаем ровно на 1 окно вправо
    } else if (strcmp(command, "scroll-left") == 0) {
        scroll_window_index--; // Сдвигаем ровно на 1 окно влево
        if (scroll_window_index < 0) scroll_window_index = 0; // Блокируем скролл левее первого окна
    } else if (strcmp(command, "ratio-inc") == 0) {
        master_ratio += 0.05f;
        if (master_ratio > 0.95f) master_ratio = 0.95f; // Защита
    } else if (strcmp(command, "ratio-dec") == 0) {
        master_ratio -= 0.05f;
        if (master_ratio < 0.05f) master_ratio = 0.05f; // Защита
    }
}

// --- Обработчик перерасчета координат ---
static void handle_layout_demand(void *data,
                                 struct river_layout_v3 *layout,
                                 uint32_t view_count,
                                 uint32_t usable_width,
                                 uint32_t usable_height,
                                 uint32_t tags,
                                 uint32_t serial) {
    if (view_count == 0) {
        last_view_count = view_count;
        river_layout_v3_commit(layout, "my-layout", serial);
        return;
    }

    if (current_mode == MODE_STACK) {
        struct WindowRect windows[view_count];
        calculate_stack((int)usable_width, (int)usable_height, layout_gap, (int)view_count, windows);
        
        for (uint32_t i = 0; i < view_count; ++i) {
            river_layout_v3_push_view_dimensions(layout, windows[i].x, windows[i].y, windows[i].w, windows[i].h, serial);
        }
    } 
    
    else if (current_mode == MODE_TILING) {
        // --- Режим Мастер-Стек ---
        if (view_count == 1) {
            river_layout_v3_push_view_dimensions(layout, layout_gap, layout_gap, usable_width - (layout_gap * 2), usable_height - (layout_gap * 2), serial);
        } else {
            uint32_t available_width = usable_width - (layout_gap * 3); // отступы слева, между окнами, справа
            uint32_t master_width = (uint32_t)(available_width * master_ratio);
            uint32_t stack_width = available_width - master_width;
            
            // Высота для каждого окна в стеке
            uint32_t stack_view_count = view_count - 1;
            uint32_t available_height = usable_height - (layout_gap * (stack_view_count + 1));
            uint32_t stack_height = available_height / stack_view_count;

            for (uint32_t i = 0; i < view_count; ++i) {
                if (i == 0) {
                    // Мастер-окно (слева)
                    river_layout_v3_push_view_dimensions(layout, layout_gap, layout_gap, master_width, usable_height - (layout_gap * 2), serial);
                } else {
                    // Окна в стеке (справа, делят высоту)
                    int32_t x = (layout_gap * 2) + master_width;
                    int32_t y = layout_gap + (i - 1) * (stack_height + layout_gap);
                    
                    // Компенсируем пиксели от округления при делении для самого нижнего окна
                    uint32_t h = stack_height;
                    if (i == view_count - 1) {
                        h = (usable_height - layout_gap) - y;
                    }
                    
                    river_layout_v3_push_view_dimensions(layout, x, y, stack_width, h, serial);
                }
            }
        }
    }
    else if (current_mode == MODE_DWINDLE) {
        // --- Режим Dwindle (Спираль) ---
        if (view_count == 1) {
            river_layout_v3_push_view_dimensions(layout, layout_gap, layout_gap, usable_width - (layout_gap * 2), usable_height - (layout_gap * 2), serial);
        } else {
            struct WindowRect windows[view_count];
            calculate_dwindle(0, 0, (int)usable_width, (int)usable_height, layout_gap, (int)view_count, windows);
            
            for (uint32_t i = 0; i < view_count; ++i) {
                river_layout_v3_push_view_dimensions(layout, windows[i].x, windows[i].y, windows[i].w, windows[i].h, serial);
            }
        }
    }
    else if (current_mode == MODE_NIRI) {
        // Настройки Niri:
        int column_width = ((int)usable_width - (layout_gap * 3)) / 2; 

        // Ограничиваем индекс скролла, чтобы не уйти за края
        int max_index = (int)view_count - 2; 
        if (max_index < 0) max_index = 0;

        // Автоскролл к самому правому краю при появлении нового окна
        if (view_count > last_view_count) {
            scroll_window_index = max_index;
        }

        if (scroll_window_index > max_index) scroll_window_index = max_index;
        if (scroll_window_index < 0) scroll_window_index = 0;

        // Вычисляем реальное смещение в пикселях: одно окно (колонка) + отступ
        int actual_scroll_offset = scroll_window_index * (column_width + layout_gap);

        struct WindowRect windows[view_count];
        calculate_niri_scrolling(0, 0, (int)usable_width, (int)usable_height, layout_gap, (int)view_count, column_width, actual_scroll_offset, windows);

        for (uint32_t i = 0; i < view_count; ++i) {
            river_layout_v3_push_view_dimensions(layout, windows[i].x, windows[i].y, windows[i].w, windows[i].h, serial);
        }
    }
    else if (current_mode == MODE_MONOCLE) {
        // --- Режим Monocle (все окна на весь экран с учетом отступов) ---
        for (uint32_t i = 0; i < view_count; ++i) {
            river_layout_v3_push_view_dimensions(layout, layout_gap, layout_gap, usable_width - (layout_gap * 2), usable_height - (layout_gap * 2), serial);
        }
    }

    last_view_count = view_count;

    river_layout_v3_commit(layout, "my-layout", serial);
}

// Привязываем функции к протоколу
static const struct river_layout_v3_listener layout_listener = {
    .layout_demand = handle_layout_demand,
    .user_command = layout_handle_user_command,
};



// --- Обработчик реестра (инициализация) ---
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, river_layout_manager_v3_interface.name) == 0) {
        layout_manager = wl_registry_bind(registry, name, &river_layout_manager_v3_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (output_count < MAX_OUTPUTS) {
            outputs[output_count++] = wl_registry_bind(registry, name, &wl_output_interface, 1);
        }
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

int main(int argc, char **argv) {
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!layout_manager || output_count == 0) {
        fprintf(stderr, "Initialization failed. Are you running river? Or no outputs found.\n");
        return 1;
    }

    // Регистрация нашего генератора в River для всех мониторов под именем "yaso-layout"
    struct river_layout_v3 *layouts[MAX_OUTPUTS];
    for (int i = 0; i < output_count; i++) {
        layouts[i] = river_layout_manager_v3_get_layout(layout_manager, outputs[i], "yaso-layout");
        river_layout_v3_add_listener(layouts[i], &layout_listener, NULL);
    }

    // Бесконечный цикл прослушивания сокета Wayland
    while (wl_display_dispatch(display) != -1) {
        // Ожидание событий
    }

    for (int i = 0; i < output_count; i++) {
        river_layout_v3_destroy(layouts[i]);
    }
    river_layout_manager_v3_destroy(layout_manager);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    return 0;
}