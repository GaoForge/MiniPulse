#pragma once
inline int WidgetSectionAt(int x, int width, int sections = 4) {
    if (width <= 0 || sections <= 0 || x < 0 || x >= width) return -1;
    return static_cast<int>(static_cast<long long>(x) * sections / width);
}
