#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <linux/input-event-codes.h>
#include "river-window-management-v1-client-protocol.h"
#include "river-xkb-bindings-v1-client-protocol.h"

static struct river_window_manager_v1 *wm = NULL;
static struct river_window_v1 *new_window = NULL;
static struct river_seat_v1 *active_seat = NULL;
static struct river_xkb_bindings_v1 *xkb_bindings = NULL;
static struct river_xkb_binding_v1 *term_binding = NULL;
static struct river_xkb_binding_v1 *exit_binding = NULL;
static int bindings_enabled = 0;
static int pointer_bindings_enabled = 0;
static int next_x = 100;
static int next_y = 100;

struct window_state {
    struct river_window_v1 *window;
    struct river_node_v1 *node;
    int x, y;
    int w, h;
};

static struct river_pointer_binding_v1 *move_binding = NULL;
static struct river_pointer_binding_v1 *resize_binding = NULL;
static struct river_window_v1 *pointer_window = NULL;
static struct river_window_v1 *op_window = NULL;
static int op_type = 0; // 1 = move, 2 = resize
static int op_last_dx = 0;
static int op_last_dy = 0;

static void term_key_pressed(void *data, struct river_xkb_binding_v1 *binding) {
    if (fork() == 0) {
        execlp("alacritty", "alacritty", NULL);
        exit(1);
    }
}
static void term_key_released(void *data, struct river_xkb_binding_v1 *binding) {}
static const struct river_xkb_binding_v1_listener term_key_listener = {
    .pressed = term_key_pressed,
    .released = term_key_released,
};

static void exit_key_pressed(void *data, struct river_xkb_binding_v1 *binding) {
    if (wm) {
        river_window_manager_v1_exit_session(wm);
    }
}
static void exit_key_released(void *data, struct river_xkb_binding_v1 *binding) {}
static const struct river_xkb_binding_v1_listener exit_key_listener = {
    .pressed = exit_key_pressed,
    .released = exit_key_released,
};

static void move_binding_pressed(void *data, struct river_pointer_binding_v1 *binding) {
    if (active_seat && pointer_window) {
        op_window = pointer_window;
        op_type = 1;
        op_last_dx = 0;
        op_last_dy = 0;
        river_seat_v1_op_start_pointer(active_seat);
        if (wm) river_window_manager_v1_manage_dirty(wm);
    }
}
static void move_binding_released(void *data, struct river_pointer_binding_v1 *binding) {}
static const struct river_pointer_binding_v1_listener move_binding_listener = {
    .pressed = move_binding_pressed,
    .released = move_binding_released,
};

static void resize_binding_pressed(void *data, struct river_pointer_binding_v1 *binding) {
    if (active_seat && pointer_window) {
        op_window = pointer_window;
        op_type = 2;
        op_last_dx = 0;
        op_last_dy = 0;
        river_seat_v1_op_start_pointer(active_seat);
        if (wm) river_window_manager_v1_manage_dirty(wm);
    }
}
static void resize_binding_released(void *data, struct river_pointer_binding_v1 *binding) {}
static const struct river_pointer_binding_v1_listener resize_binding_listener = {
    .pressed = resize_binding_pressed,
    .released = resize_binding_released,
};

static void seat_removed(void *data, struct river_seat_v1 *seat) {}
static void seat_wl_seat(void *data, struct river_seat_v1 *seat, uint32_t name) {}
static void seat_pointer_enter(void *data, struct river_seat_v1 *seat, struct river_window_v1 *window) {
    pointer_window = window;
}
static void seat_pointer_leave(void *data, struct river_seat_v1 *seat) {
    pointer_window = NULL;
}
static void seat_window_interaction(void *data, struct river_seat_v1 *seat, struct river_window_v1 *window) {}
static void seat_shell_surface_interaction(void *data, struct river_seat_v1 *seat, struct river_shell_surface_v1 *shell_surface) {}
static void seat_op_delta(void *data, struct river_seat_v1 *seat, int32_t dx, int32_t dy) {
    if (!op_window) return;
    struct window_state *state = river_window_v1_get_user_data(op_window);
    if (!state) return;

    op_last_dx = dx;
    op_last_dy = dy;

    if (op_type == 1) { // Перемещение
        river_node_v1_set_position(state->node, state->x + dx, state->y + dy);
    } else if (op_type == 2) { // Изменение размера (с ограничением минимального размера)
        int new_w = state->w + dx > 10 ? state->w + dx : 10;
        int new_h = state->h + dy > 10 ? state->h + dy : 10;
        river_window_v1_propose_dimensions(op_window, new_w, new_h);
    }

    if (wm) river_window_manager_v1_manage_dirty(wm);
}
static void seat_op_release(void *data, struct river_seat_v1 *seat) {
    if (op_window) {
        struct window_state *state = river_window_v1_get_user_data(op_window);
        if (state) {
            if (op_type == 1) {
                state->x += op_last_dx;
                state->y += op_last_dy;
            } else if (op_type == 2) {
                state->w = state->w + op_last_dx > 10 ? state->w + op_last_dx : 10;
                state->h = state->h + op_last_dy > 10 ? state->h + op_last_dy : 10;
            }
        }
    }
    river_seat_v1_op_end(seat);
    op_window = NULL;
    op_type = 0;
    op_last_dx = 0;
    op_last_dy = 0;

    if (wm) river_window_manager_v1_manage_dirty(wm);
}
static void seat_pointer_position(void *data, struct river_seat_v1 *seat, int32_t x, int32_t y) {}

static const struct river_seat_v1_listener seat_listener = {
    .removed = seat_removed,
    .wl_seat = seat_wl_seat,
    .pointer_enter = seat_pointer_enter,
    .pointer_leave = seat_pointer_leave,
    .window_interaction = seat_window_interaction,
    .shell_surface_interaction = seat_shell_surface_interaction,
    .op_delta = seat_op_delta,
    .op_release = seat_op_release,
    .pointer_position = seat_pointer_position,
};

static void setup_bindings() {
    if (!active_seat) return;
    
    // --- Настройка клавиатуры ---
    if (xkb_bindings && !term_binding) {
        term_binding = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings, active_seat, XKB_KEY_Return, RIVER_SEAT_V1_MODIFIERS_MOD4);
        river_xkb_binding_v1_add_listener(term_binding, &term_key_listener, NULL);
        
        exit_binding = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings, active_seat, XKB_KEY_e, RIVER_SEAT_V1_MODIFIERS_MOD4 | RIVER_SEAT_V1_MODIFIERS_SHIFT);
        river_xkb_binding_v1_add_listener(exit_binding, &exit_key_listener, NULL);
    }

    // --- Настройка мыши ---
    if (!move_binding) {
        // Super + ЛКМ = Перемещение
        move_binding = river_seat_v1_get_pointer_binding(active_seat, BTN_LEFT, RIVER_SEAT_V1_MODIFIERS_MOD4);
        river_pointer_binding_v1_add_listener(move_binding, &move_binding_listener, NULL);
    }

    if (!resize_binding) {
        // Super + ПКМ = Изменение размера
        resize_binding = river_seat_v1_get_pointer_binding(active_seat, BTN_RIGHT, RIVER_SEAT_V1_MODIFIERS_MOD4);
        river_pointer_binding_v1_add_listener(resize_binding, &resize_binding_listener, NULL);
    }
    
    if (wm) {
        river_window_manager_v1_manage_dirty(wm);
    }
}

static void wm_unavailable(void *data, struct river_window_manager_v1 *wm) {
    fprintf(stderr, "WM недоступен (возможно, уже запущен другой оконный менеджер)\n");
    exit(1);
}
static void wm_finished(void *data, struct river_window_manager_v1 *wm) {}
static void wm_manage_start(void *data, struct river_window_manager_v1 *wm) {
    // Если появилось новое окно, задаем ему размеры и позиционируем
    if (new_window) {
        struct window_state *state = calloc(1, sizeof(struct window_state));
        state->window = new_window;
        state->x = next_x;
        state->y = next_y;
        state->w = 800;
        state->h = 600;
        state->node = river_window_v1_get_node(new_window);
        
        // Прикрепляем состояние к окну, чтобы к нему был доступ в op_delta
        river_window_v1_set_user_data(new_window, state);
        
        // Запрашиваем размер
        river_window_v1_propose_dimensions(new_window, state->w, state->h);
        
        // Создаём ноду рендеринга и располагаем её на экране
        river_node_v1_place_top(state->node);
        river_node_v1_set_position(state->node, state->x, state->y);
        
        // Сдвигаем координаты для каждого следующего окна (эффект лесенки)
        next_x += 40;
        next_y += 40;
        if (next_x > 500) { 
            next_x = 100; next_y = 100; 
        }

        // Делаем окно видимым
        river_window_v1_show(new_window);
        
        // Передаём фокус ввода (клавиатуру) новому окну
        if (active_seat) {
            river_seat_v1_focus_window(active_seat, new_window);
        }

        new_window = NULL;
    }
    
    if (move_binding && resize_binding && !pointer_bindings_enabled) {
        river_pointer_binding_v1_enable(move_binding);
        river_pointer_binding_v1_enable(resize_binding);
        pointer_bindings_enabled = 1;
    }

    if (term_binding && exit_binding && !bindings_enabled) {
        river_xkb_binding_v1_enable(term_binding);
        river_xkb_binding_v1_enable(exit_binding);
        bindings_enabled = 1;
    }

    river_window_manager_v1_manage_finish(wm);
}
static void wm_render_start(void *data, struct river_window_manager_v1 *wm) {
    river_window_manager_v1_render_finish(wm);
}
static void wm_session_locked(void *data, struct river_window_manager_v1 *wm) {}
static void wm_session_unlocked(void *data, struct river_window_manager_v1 *wm) {}
static void wm_window(void *data, struct river_window_manager_v1 *wm, struct river_window_v1 *window) {
    printf("Новое окно создано!\n");
    new_window = window;
}
static void wm_output(void *data, struct river_window_manager_v1 *wm, struct river_output_v1 *output) {
    printf("Найден монитор!\n");
}
static void wm_seat(void *data, struct river_window_manager_v1 *wm, struct river_seat_v1 *seat) {
    printf("Найдено устройство ввода (seat)!\n");
    active_seat = seat;
    river_seat_v1_add_listener(seat, &seat_listener, NULL);
    setup_bindings();
}

static const struct river_window_manager_v1_listener wm_listener = {
    .unavailable = wm_unavailable,
    .finished = wm_finished,
    .manage_start = wm_manage_start,
    .render_start = wm_render_start,
    .session_locked = wm_session_locked,
    .session_unlocked = wm_session_unlocked,
    .window = wm_window,
    .output = wm_output,
    .seat = wm_seat,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
    if (strcmp(interface, river_window_manager_v1_interface.name) == 0) {
        // Используем предлагаемую сервером версию вместо жестко заданной 1
        wm = wl_registry_bind(registry, name,
                              &river_window_manager_v1_interface, version);
        
        // ВАЖНО: Вешаем слушатель сразу после bind, до завершения wl_display_roundtrip!
        river_window_manager_v1_add_listener(wm, &wm_listener, NULL);
        printf("Найден river-window-management-v1\n");
    } else if (strcmp(interface, "river_xkb_bindings_v1") == 0) {
        xkb_bindings = wl_registry_bind(registry, name,
                                        (const struct wl_interface*)&river_xkb_bindings_v1_interface, version);
        printf("Найден river-xkb-bindings-v1\n");
        setup_bindings();
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int main(void) {
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Не удалось подключиться к Wayland\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!wm) {
        fprintf(stderr, "river-window-management-v1 не найден\n");
        return 1;
    }

    printf("WM запущен, ждём события...\n");
    while (wl_display_dispatch(display) != -1) {}

    wl_display_disconnect(display);
    return 0;
}
