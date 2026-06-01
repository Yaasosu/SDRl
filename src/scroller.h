#ifndef NIRI_LAYOUT_H
#define NIRI_LAYOUT_H

// Структура геометрии (если не объявлена глобально)
#ifndef WINDOW_RECT_DEFINED
#define WINDOW_RECT_DEFINED
struct WindowRect {
    int x;
    int y;
    int w;
    int h;
};
#endif

// Функция расчета раскладки Scroll (в стиле Niri)
inline void calculate_niri_scrolling(
    int monitor_x, int monitor_y, int monitor_w, int monitor_h,
    int gap, int num_windows, int column_width, int scroll_offset,
    struct WindowRect* windows) 
{
    if (num_windows == 0) return;

    // В базовом скроллинге высота всех окон одинаковая — на весь монитор
    int window_h = monitor_h - (gap * 2);
    int current_y = monitor_y + gap;

    for (int i = 0; i < num_windows; ++i) {
        windows[i].w = column_width;
        windows[i].h = window_h;
        windows[i].y = current_y;

        // X-координата — это самое важное здесь.
        windows[i].x = monitor_x + gap + (i * (column_width + gap)) - scroll_offset;
    }
}

#endif // NIRI_LAYOUT_H