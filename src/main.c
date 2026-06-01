#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "river-window-management-v1-client-protocol.h"
#include "river-xkb-bindings-v1-client-protocol.h"

static struct river_window_manager_v1 *wm = NULL;
static struct river_window_v1 *new_window = NULL;
static struct river_seat_v1 *active_seat = NULL;
static struct river_xkb_bindings_v1 *xkb_bindings = NULL;
static struct river_xkb_binding_v1 *term_binding = NULL;
static struct river_xkb_binding_v1 *exit_binding = NULL;
static int bindings_enabled = 0;
static int next_x = 100;
static int next_y = 100;

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

static void setup_bindings() {
    if (!xkb_bindings || !active_seat || term_binding) return;
    
    // Super + Enter = запуск терминала
    term_binding = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings, active_seat, XKB_KEY_Return, RIVER_SEAT_V1_MODIFIERS_MOD4);
    river_xkb_binding_v1_add_listener(term_binding, &term_key_listener, NULL);
    
    // Super + Shift + E = выход из WM
    exit_binding = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings, active_seat, XKB_KEY_e, RIVER_SEAT_V1_MODIFIERS_MOD4 | RIVER_SEAT_V1_MODIFIERS_SHIFT);
    river_xkb_binding_v1_add_listener(exit_binding, &exit_key_listener, NULL);
    
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
        // Запрашиваем размер 800x600
        river_window_v1_propose_dimensions(new_window, 800, 600);
        
        // Создаём ноду рендеринга и располагаем её на экране
        struct river_node_v1 *node = river_window_v1_get_node(new_window);
        river_node_v1_place_top(node);
        river_node_v1_set_position(node, next_x, next_y);
        
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
