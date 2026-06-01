#ifndef DWINDLE_LAYOUT_H
#define DWINDLE_LAYOUT_H

#include <vector>

// Структура для хранения геометрии окна
struct WindowRect {
    int x;
    int y;
    int w;
    int h;
};

// Функция расчета раскладки Dwindle
// monitor_x, monitor_y - координаты левого верхнего угла монитора
// monitor_w, monitor_h - ширина и высота доступной области монитора
// gap - отступы между окнами (опционально, если твой генератор это поддерживает)
// num_windows - сколько окон нужно разместить
inline std::vector<WindowRect> calculate_dwindle(int monitor_x, int monitor_y, int monitor_w, int monitor_h, int gap, int num_windows) {
    std::vector<WindowRect> windows;
    if (num_windows == 0) return windows;

    // Оставшееся свободное пространство, которое мы будем "откусывать"
    int current_x = monitor_x + gap;
    int current_y = monitor_y + gap;
    int current_w = monitor_w - (gap * 2);
    int current_h = monitor_h - (gap * 2);

    for (int i = 0; i < num_windows; ++i) {
        WindowRect win;

        // Если это последнее окно, оно забирает всё оставшееся место
        if (i == num_windows - 1) {
            win.x = current_x;
            win.y = current_y;
            win.w = current_w;
            win.h = current_h;
            windows.push_back(win);
            break;
        }

        // Вычисляем, по какой стороне делить (всегда делим бОльшую сторону)
        if (current_w > current_h) {
            // Делим по горизонтали (левая и правая половины)
            win.x = current_x;
            win.y = current_y;
            win.w = (current_w - gap) / 2;
            win.h = current_h;

            // Сдвигаем оставшееся пространство вправо
            current_x += win.w + gap;
            current_w -= win.w + gap;
        } else {
            // Делим по вертикали (верхняя и нижняя половины)
            win.x = current_x;
            win.y = current_y;
            win.w = current_w;
            win.h = (current_h - gap) / 2;

            // Сдвигаем оставшееся пространство вниз
            current_y += win.h + gap;
            current_h -= win.h + gap;
        }

        windows.push_back(win);
    }

    return windows;
}

#endif // DWINDLE_LAYOUT_H