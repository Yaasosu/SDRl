#ifndef LAYOUT_MONOCLE_H
#define LAYOUT_MONOCLE_H

#include "river-window-management-v1-client-protocol.h"

// Применяем монокль-раскладку к окну (занимает весь экран)
static inline void apply_monocle_layout(struct river_window_v1 *window, int32_t screen_width, int32_t screen_height) {
    if (!window) return;
    
    // Если размеры монитора еще неизвестны, используем дефолтные
    if (screen_width <= 0) screen_width = 1920;
    if (screen_height <= 0) screen_height = 1080;

    // Запрашиваем размеры во весь экран
    river_window_v1_propose_dimensions(window, screen_width, screen_height);
    
    // Получаем ноду рендеринга и располагаем её на самом верху, начиная с верхнего левого угла (0,0)
    struct river_node_v1 *node = river_window_v1_get_node(window);
    river_node_v1_place_top(node);
    river_node_v1_set_position(node, 0, 0);
    
    // Делаем окно видимым
    river_window_v1_show(window);
}

#endif // LAYOUT_MONOCLE_H