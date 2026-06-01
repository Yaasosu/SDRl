#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "river-layout-v3-client-protocol.h"
#include "scroller.h"
#include "dwindle.h"

// --- Состояние нашего менеджера слоев ---
// --- Состояние нашего менеджера слоев ---
typedef enum {
    MODE_STACK,
    MODE_TILING,
    MODE_DWINDLE,
    MODE_NIRI
} LayoutMode;

LayoutMode current_mode = MODE_STACK; // По умолчанию режим Master-Stack
float master_ratio = 0.60;            // По умолчанию Мастер занимает 60%
int scroll_window_index = 0;
static uint32_t last_view_count = 0; // Для отслеживания появления новых окон


struct river_layout_manager_v3 *layout_manager = NULL;
struct wl_output *target_output = NULL;

// --- Обработчик IPC команд от River ---
static void layout_handle_user_command(void *data, struct river_layout_v3 *layout, const char *command) {
    (void)layout; // Чтобы компилятор не ругался на неиспользуемую переменную

    if (strcmp(command, "toggle") == 0) {
        if (current_mode == MODE_STACK) {
            current_mode = MODE_TILING;
            printf("current mod is MODE_TILING\n");
        } else if (current_mode == MODE_TILING) {
            current_mode = MODE_DWINDLE;
            printf("current mod is MODE_DWINDLE\n");
        } else if (current_mode == MODE_DWINDLE) {
            current_mode = MODE_NIRI;
            printf("current mod is MODE_NIRI\n");
        } else {
            current_mode = MODE_STACK;
            printf("current mod is MODE_STACK\n");
        }
    } else if (strcmp(command, "niri") == 0) {
        current_mode = MODE_NIRI;
        printf("current mod is MODE_NIRI\n");
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
// Настройки каскада (можешь поменять цифры под себя)
        const uint32_t offset_x = 40; // Смещение вправо (в пикселях)
        const uint32_t offset_y = 40; // Смещение вниз (в пикселях)

        // Размер самого окна (например, 70% от ширины и высоты доступного экрана)
        uint32_t window_width = (usable_width * 70) / 100;
        uint32_t window_height = (usable_height * 70) / 100;

        for (uint32_t i = 0; i < view_count; ++i) {
            // Вычисляем позицию для текущего окна
            uint32_t x = i * offset_x;
            uint32_t y = i * offset_y;

            // Защита: если открыто слишком много окон, они могут уехать за правый/нижний край экрана.
            // Поэтому ограничиваем максимальное смещение:
            if (x + window_width > usable_width) {
                x = usable_width - window_width;
            }
            if (y + window_height > usable_height) {
                y = usable_height - window_height;
            }

            river_layout_v3_push_view_dimensions(
                layout, 
                x, 
                y, 
                window_width, 
                window_height, 
                serial
            );
        }
    } 
    
    else if (current_mode == MODE_TILING) {
        // --- Режим Мастер-Стек ---
        if (view_count == 1) {
            river_layout_v3_push_view_dimensions(layout, 0, 0, usable_width, usable_height, serial);
        } else {
            // Главное окно (Master) теперь занимает процент, зависящий от master_ratio
            uint32_t master_width = (uint32_t)(usable_width * master_ratio);
            uint32_t stack_width = usable_width - master_width;
            
            // Высота для каждого окна в стеке
            uint32_t stack_view_count = view_count - 1;
            uint32_t stack_height = usable_height / stack_view_count;

            for (uint32_t i = 0; i < view_count; ++i) {
                if (i == 0) {
                    // Мастер-окно (слева)
                    river_layout_v3_push_view_dimensions(layout, 0, 0, master_width, usable_height, serial);
                } else {
                    // Окна в стеке (справа, делят высоту)
                    int32_t x = master_width;
                    int32_t y = (i - 1) * stack_height;
                    river_layout_v3_push_view_dimensions(layout, x, y, stack_width, stack_height, serial);
                }
            }
        }
    }
    else if (current_mode == MODE_DWINDLE) {
        // --- Режим Dwindle (Спираль) ---
        if (view_count == 1) {
            river_layout_v3_push_view_dimensions(layout, 0, 0, usable_width, usable_height, serial);
        } else {
            int gap = 10;
            struct WindowRect windows[view_count];
            calculate_dwindle(0, 0, (int)usable_width, (int)usable_height, gap, (int)view_count, windows);
            
            for (uint32_t i = 0; i < view_count; ++i) {
                river_layout_v3_push_view_dimensions(layout, windows[i].x, windows[i].y, windows[i].w, windows[i].h, serial);
            }
        }
    }
    else if (current_mode == MODE_NIRI) {
        // Настройки Niri:
        int gap = 10; // Внешние и внутренние отступы
        int column_width = ((int)usable_width - (gap * 3)) / 2; 

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
        int actual_scroll_offset = scroll_window_index * (column_width + gap);

        struct WindowRect windows[view_count];
        calculate_niri_scrolling(0, 0, (int)usable_width, (int)usable_height, gap, (int)view_count, column_width, actual_scroll_offset, windows);

        for (uint32_t i = 0; i < view_count; ++i) {
            river_layout_v3_push_view_dimensions(layout, windows[i].x, windows[i].y, windows[i].w, windows[i].h, serial);
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
        if (target_output == NULL) {
            target_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
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

    if (!layout_manager || !target_output) {
        fprintf(stderr, "Initialization failed. Are you running river-classic?\n");
        return 1;
    }

    // Регистрация нашего генератора в River под именем "yaso-layout"
    struct river_layout_v3 *layout = river_layout_manager_v3_get_layout(layout_manager, target_output, "yaso-layout");
    river_layout_v3_add_listener(layout, &layout_listener, NULL);

    // Бесконечный цикл прослушивания сокета Wayland
    while (wl_display_dispatch(display) != -1) {
        // Ожидание событий
    }

    river_layout_v3_destroy(layout);
    river_layout_manager_v3_destroy(layout_manager);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    return 0;
}