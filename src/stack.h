#ifndef STACK_LAYOUT_H
#define STACK_LAYOUT_H

#ifndef WINDOW_RECT_DEFINED
#define WINDOW_RECT_DEFINED
struct WindowRect {
    int x;
    int y;
    int w;
    int h;
};
#endif

// Функция расчета каскадной раскладки (Stack / Floating)
inline void calculate_stack(int usable_width, int usable_height, int layout_gap, int num_windows, struct WindowRect* windows) {
    if (num_windows == 0) return;

    // Настройки каскада
    const int offset_x = 40; // Смещение вправо (в пикселях)
    const int offset_y = 40; // Смещение вниз (в пикселях)

    // Размер самого окна (например, 70% от ширины и высоты доступного экрана)
    int window_width = (usable_width * 70) / 100;
    int window_height = (usable_height * 70) / 100;

    for (int i = 0; i < num_windows; ++i) {
        int x = layout_gap + (i * offset_x);
        int y = layout_gap + (i * offset_y);

        // Защита: ограничиваем максимальное смещение, чтобы окна не уезжали за экран
        if (x + window_width > usable_width - layout_gap) {
            x = usable_width - window_width - layout_gap;
        }
        if (y + window_height > usable_height - layout_gap) {
            y = usable_height - window_height - layout_gap;
        }

        windows[i].x = x;
        windows[i].y = y;
        windows[i].w = window_width;
        windows[i].h = window_height;
    }
}

#endif // STACK_LAYOUT_H