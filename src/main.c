#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
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
static int desk_min_x = 0;
static int desk_min_y = 0;
static int desk_max_x = 1920;
static int desk_max_y = 1080;

struct output_state {
    struct river_output_v1 *output;
    int x, y, w, h;
    struct output_state *next;
};
static struct output_state *output_list = NULL;

static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct wl_surface *snap_surface = NULL;
static struct river_shell_surface_v1 *snap_shell = NULL;
static struct river_node_v1 *snap_node = NULL;
static struct wl_buffer *snap_buffer = NULL;
static int snap_buf_w = 0, snap_buf_h = 0;

static void recalc_desktop() {
    if (!output_list) return;
    desk_min_x = output_list->x;
    desk_min_y = output_list->y;
    desk_max_x = output_list->x + output_list->w;
    desk_max_y = output_list->y + output_list->h;
    for (struct output_state *os = output_list->next; os; os = os->next) {
        if (os->x < desk_min_x) desk_min_x = os->x;
        if (os->y < desk_min_y) desk_min_y = os->y;
        if (os->x + os->w > desk_max_x) desk_max_x = os->x + os->w;
        if (os->y + os->h > desk_max_y) desk_max_y = os->y + os->h;
    }
}

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
static int op_res_x = 0;
static int op_res_y = 0;
static int op_res_w = 0;
static int op_res_h = 0;
static int pointer_x = 0;
static int pointer_y = 0;
static int op_start_pointer_x = 0;
static int op_start_pointer_y = 0;
static int op_current_pointer_x = 0;
static int op_current_pointer_y = 0;

static void hide_snap_preview() {
    if (snap_node) { river_node_v1_destroy(snap_node); snap_node = NULL; }
    if (snap_shell) { river_shell_surface_v1_destroy(snap_shell); snap_shell = NULL; }
    if (snap_surface) { wl_surface_destroy(snap_surface); snap_surface = NULL; }
}

static void draw_snap_preview(int x, int y, int w, int h, struct window_state *state) {
    if (!compositor || !shm || w <= 0 || h <= 0) return;
    if (!snap_surface) {
        snap_surface = wl_compositor_create_surface(compositor);
        snap_shell = river_window_manager_v1_get_shell_surface(wm, snap_surface);
        snap_node = river_shell_surface_v1_get_node(snap_shell);
    }
    if (snap_buf_w != w || snap_buf_h != h) {
        if (snap_buffer) wl_buffer_destroy(snap_buffer);
        int stride = w * 4;
        int size = stride * h;
        
        int fd = memfd_create("snap", 0);
        if (fd < 0) return;
        if (ftruncate(fd, size) < 0) { close(fd); return; }
        uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) { close(fd); return; }
        
        for (int cy = 0; cy < h; cy++) {
            for (int cx = 0; cx < w; cx++) {
                // Рамка 2px - сплошной белый (0xFFFFFFFF)
                // Заливка - белый 25% прозрачности (0x40404040 в pre-multiplied ARGB)
                if (cx < 2 || cy < 2 || cx >= w - 2 || cy >= h - 2) {
                    data[cy * w + cx] = 0xFFFFFFFF; 
                } else {
                    data[cy * w + cx] = 0x40404040; 
                }
            }
        }
        munmap(data, size);
        struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
        snap_buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        snap_buf_w = w;
        snap_buf_h = h;
    }
    wl_surface_attach(snap_surface, snap_buffer, 0, 0);
    wl_surface_damage(snap_surface, 0, 0, w, h);
    river_node_v1_set_position(snap_node, x, y);
    if (state && state->node) river_node_v1_place_below(snap_node, state->node); // Прячем ПОД окном
    else river_node_v1_place_top(snap_node);
    river_shell_surface_v1_sync_next_commit(snap_shell);
    wl_surface_commit(snap_surface);
}

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
        struct window_state *state = river_window_v1_get_user_data(op_window);
        if (state) {
            op_res_x = state->x;
            op_res_y = state->y;
        }
        op_start_pointer_x = pointer_x;
        op_start_pointer_y = pointer_y;
        op_current_pointer_x = pointer_x;
        op_current_pointer_y = pointer_y;
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
        struct window_state *state = river_window_v1_get_user_data(op_window);
        if (state) {
            op_res_w = state->w;
            op_res_h = state->h;
        }
        op_start_pointer_x = pointer_x;
        op_start_pointer_y = pointer_y;
        op_current_pointer_x = pointer_x;
        op_current_pointer_y = pointer_y;
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
static void seat_window_interaction(void *data, struct river_seat_v1 *seat, struct river_window_v1 *window) {
    river_seat_v1_focus_window(seat, window); // Передаем фокус клавиатуры
    struct window_state *state = river_window_v1_get_user_data(window);
    if (state && state->node) {
        river_node_v1_place_top(state->node); // Поднимаем окно поверх остальных
    }
    if (wm) river_window_manager_v1_manage_dirty(wm); // Сообщаем серверу применить изменения
}
static void seat_shell_surface_interaction(void *data, struct river_seat_v1 *seat, struct river_shell_surface_v1 *shell_surface) {}

static void seat_op_delta(void *data, struct river_seat_v1 *seat, int32_t dx, int32_t dy) {
    if (!op_window) return;
    struct window_state *state = river_window_v1_get_user_data(op_window);
    if (!state) return;

    op_current_pointer_x = op_start_pointer_x + dx;
    op_current_pointer_y = op_start_pointer_y + dy;

    if (op_type == 1) { // Перемещение
        int nx = state->x + dx;
        int ny = state->y + dy;

        // Блокируем вылет за края экранов
        if (nx < desk_min_x) nx = desk_min_x;
        if (nx + state->w > desk_max_x) nx = desk_max_x - state->w;

        // Верх/низ по-прежнему ограничиваем жестко
        if (ny < desk_min_y) ny = desk_min_y;
        if (ny + state->h > desk_max_y) ny = desk_max_y - state->h;

        op_res_x = nx;
        op_res_y = ny;
        river_node_v1_set_position(state->node, nx, ny);

        // --- Превью Aero Snap ---
        int snapped = 0;
        struct output_state *os = output_list;
        while (os) {
            if (op_current_pointer_x >= os->x && op_current_pointer_x <= os->x + os->w &&
                op_current_pointer_y >= os->y && op_current_pointer_y <= os->y + os->h) {
                
                int edge = 30; // Увеличиваем толщину самого края экрана
                int corner_x = os->w / 3; // Угловая зона по горизонтали (1/3 экрана)
                int corner_y = os->h / 3; // Угловая зона по вертикали (1/3 экрана)
                int is_left = op_current_pointer_x <= os->x + edge;
                int is_right = op_current_pointer_x >= os->x + os->w - edge;
                int is_top = op_current_pointer_y <= os->y + edge;
                int is_bottom = op_current_pointer_y >= os->y + os->h - edge;

                int near_left = op_current_pointer_x <= os->x + corner_x;
                int near_right = op_current_pointer_x >= os->x + os->w - corner_x;
                int near_top = op_current_pointer_y <= os->y + corner_y;
                int near_bottom = op_current_pointer_y >= os->y + os->h - corner_y;

                // Сначала проверяем углы (25% экрана)
                if ((is_top && near_left) || (is_left && near_top)) {
                    draw_snap_preview(os->x, os->y, os->w / 2, os->h / 2, state);
                    snapped = 1;
                } else if ((is_top && near_right) || (is_right && near_top)) {
                    draw_snap_preview(os->x + os->w / 2, os->y, os->w / 2, os->h / 2, state);
                    snapped = 1;
                } else if ((is_bottom && near_left) || (is_left && near_bottom)) {
                    draw_snap_preview(os->x, os->y + os->h / 2, os->w / 2, os->h / 2, state);
                    snapped = 1;
                } else if ((is_bottom && near_right) || (is_right && near_bottom)) {
                    draw_snap_preview(os->x + os->w / 2, os->y + os->h / 2, os->w / 2, os->h / 2, state);
                    snapped = 1;
                // Затем проверяем края (50% / 100% экрана)
                } else if (is_left) {
                    draw_snap_preview(os->x, os->y, os->w / 2, os->h, state);
                    snapped = 1;
                } else if (is_right) {
                    draw_snap_preview(os->x + os->w / 2, os->y, os->w / 2, os->h, state);
                    snapped = 1;
                } else if (is_top) {
                    draw_snap_preview(os->x, os->y, os->w, os->h, state);
                    snapped = 1;
                }
                break;
            }
            os = os->next;
        }
        if (!snapped) hide_snap_preview();
    } else if (op_type == 2) { // Изменение размера (с ограничением минимального размера)
        int nw = state->w + dx;
        int nh = state->h + dy;

        if (nw < 10) nw = 10;
        if (nh < 10) nh = 10;
        if (state->x + nw > desk_max_x) nw = desk_max_x - state->x;
        if (state->y + nh > desk_max_y) nh = desk_max_y - state->y;

        op_res_w = nw;
        op_res_h = nh;
        river_window_v1_propose_dimensions(op_window, nw, nh);
    }

    if (wm) river_window_manager_v1_manage_dirty(wm);
}
static void seat_op_release(void *data, struct river_seat_v1 *seat) {
    hide_snap_preview();
    if (op_window) {
        struct window_state *state = river_window_v1_get_user_data(op_window);
        if (state) {
            if (op_type == 1) {
                state->x = op_res_x;
                state->y = op_res_y;

                // --- Логика Aero Snap ---
                struct output_state *os = output_list;
                while (os) {
                    // Проверяем, на каком мониторе сейчас находится курсор
                    if (pointer_x >= os->x && pointer_x <= os->x + os->w &&
                        pointer_y >= os->y && pointer_y <= os->y + os->h) {
                        
                        int snapped = 0;
                        int edge = 30;
                        int corner_x = os->w / 3;
                        int corner_y = os->h / 3;
                        int is_left = pointer_x <= os->x + edge;
                        int is_right = pointer_x >= os->x + os->w - edge;
                        int is_top = pointer_y <= os->y + edge;
                        int is_bottom = pointer_y >= os->y + os->h - edge;

                        int near_left = pointer_x <= os->x + corner_x;
                        int near_right = pointer_x >= os->x + os->w - corner_x;
                        int near_top = pointer_y <= os->y + corner_y;
                        int near_bottom = pointer_y >= os->y + os->h - corner_y;

                        if ((is_top && near_left) || (is_left && near_top)) {
                            state->x = os->x;
                            state->y = os->y;
                            state->w = os->w / 2;
                            state->h = os->h / 2;
                            snapped = 1;
                        } else if ((is_top && near_right) || (is_right && near_top)) {
                            state->x = os->x + os->w / 2;
                            state->y = os->y;
                            state->w = os->w / 2;
                            state->h = os->h / 2;
                            snapped = 1;
                        } else if ((is_bottom && near_left) || (is_left && near_bottom)) {
                            state->x = os->x;
                            state->y = os->y + os->h / 2;
                            state->w = os->w / 2;
                            state->h = os->h / 2;
                            snapped = 1;
                        } else if ((is_bottom && near_right) || (is_right && near_bottom)) {
                            state->x = os->x + os->w / 2;
                            state->y = os->y + os->h / 2;
                            state->w = os->w / 2;
                            state->h = os->h / 2;
                            snapped = 1;
                        } else if (is_left) {
                            state->x = os->x;
                            state->y = os->y;
                            state->w = os->w / 2;
                            state->h = os->h;
                            snapped = 1;
                        } else if (is_right) {
                            state->x = os->x + os->w / 2;
                            state->y = os->y;
                            state->w = os->w / 2;
                            state->h = os->h;
                            snapped = 1;
                        } else if (is_top) {
                            state->x = os->x;
                            state->y = os->y;
                            state->w = os->w;
                            state->h = os->h;
                            snapped = 1;
                        }

                        // Применяем новые размеры и позицию
                        if (snapped) {
                            river_window_v1_propose_dimensions(op_window, state->w, state->h);
                            river_node_v1_set_position(state->node, state->x, state->y);
                        }
                        break;
                    }
                    os = os->next;
                }
            } else if (op_type == 2) {
                state->w = op_res_w;
                state->h = op_res_h;
            }
        }
    }
    river_seat_v1_op_end(seat);
    op_window = NULL;
    op_type = 0;

    if (wm) river_window_manager_v1_manage_dirty(wm);
}
static void seat_pointer_position(void *data, struct river_seat_v1 *seat, int32_t x, int32_t y) {
    pointer_x = x;
    pointer_y = y;
}

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

static void output_removed(void *data, struct river_output_v1 *output) {
    struct output_state **curr = &output_list;
    while (*curr) {
        if ((*curr)->output == output) {
            struct output_state *tmp = *curr;
            *curr = (*curr)->next;
            free(tmp);
            break;
        }
        curr = &(*curr)->next;
    }
    recalc_desktop();
}
static void output_wl_output(void *data, struct river_output_v1 *output, uint32_t name) {}
static void output_position(void *data, struct river_output_v1 *output, int32_t x, int32_t y) {
    struct output_state *os = river_output_v1_get_user_data(output);
    if (os) { os->x = x; os->y = y; recalc_desktop(); }
}
static void output_dimensions(void *data, struct river_output_v1 *output, int32_t width, int32_t height) {
    struct output_state *os = river_output_v1_get_user_data(output);
    if (os) { os->w = width; os->h = height; recalc_desktop(); }
}
static const struct river_output_v1_listener output_listener = {
    .removed = output_removed,
    .wl_output = output_wl_output,
    .position = output_position,
    .dimensions = output_dimensions,
};

static void wm_output(void *data, struct river_window_manager_v1 *wm, struct river_output_v1 *output) {
    printf("Найден монитор!\n");
    struct output_state *os = calloc(1, sizeof(struct output_state));
    os->output = output;
    os->w = 1920; os->h = 1080;
    os->next = output_list;
    output_list = os;
    
    river_output_v1_set_user_data(output, os);
    river_output_v1_add_listener(output, &output_listener, NULL);
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
    } else if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
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
