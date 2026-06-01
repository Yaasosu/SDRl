#ifndef DWINDLE_LAYOUT_H
#define DWINDLE_LAYOUT_H

#ifndef WINDOW_RECT_DEFINED
#define WINDOW_RECT_DEFINED
struct WindowRect {
    int x;
    int y;
    int w;
    int h;
};
#endif

// Функция расчета раскладки Dwindle
inline void calculate_dwindle(int monitor_x, int monitor_y, int monitor_w, int monitor_h, int gap, int num_windows, struct WindowRect* windows) {
    if (num_windows == 0) return;

    // Оставшееся свободное пространство, которое мы будем "откусывать"
    int current_x = monitor_x + gap;
    int current_y = monitor_y + gap;
    int current_w = monitor_w - (gap * 2);
    int current_h = monitor_h - (gap * 2);

    for (int i = 0; i < num_windows; ++i) {
        // Если это последнее окно, оно забирает всё оставшееся место
        if (i == num_windows - 1) {
            windows[i].x = current_x;
            windows[i].y = current_y;
            windows[i].w = current_w;
            windows[i].h = current_h;
            break;
        }

        // Вычисляем, по какой стороне делить (всегда делим бОльшую сторону)
        if (current_w > current_h) {
            // Делим по горизонтали (левая и правая половины)
            windows[i].x = current_x;
            windows[i].y = current_y;
            windows[i].w = (current_w - gap) / 2;
            windows[i].h = current_h;

            // Сдвигаем оставшееся пространство вправо
            current_x += windows[i].w + gap;
            current_w -= windows[i].w + gap;
        } else {
            // Делим по вертикали (верхняя и нижняя половины)
            windows[i].x = current_x;
            windows[i].y = current_y;
            windows[i].w = current_w;
            windows[i].h = (current_h - gap) / 2;

            // Сдвигаем оставшееся пространство вниз
            current_y += windows[i].h + gap;
            current_h -= windows[i].h + gap;
        }
    }
}

#endif // DWINDLE_LAYOUT_H