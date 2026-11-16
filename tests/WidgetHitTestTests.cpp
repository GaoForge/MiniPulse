#include "../WidgetHitTest.h"
#include <iostream>
#include <stdexcept>

int main() {
    struct Case { int x, width, expected; };
    const Case cases[] = {{0,440,0},{109,440,0},{110,440,1},{219,440,1},{220,440,2},
        {329,440,2},{330,440,3},{439,440,3},{440,440,-1},{-1,440,-1},{0,0,-1},
        {80,324,0},{81,324,1},{162,324,2},{243,324,3},{659,660,3},{165,660,1},
        {71,288,0},{72,288,1},{143,288,1},{144,288,2},{215,288,2},{216,288,3},
        {287,288,3},{288,288,-1},{63,256,0},{64,256,1},{128,256,2},{192,256,3}};
    for (const auto& value : cases) {
        if (WidgetSectionAt(value.x,value.width,4) != value.expected) throw std::runtime_error("Hover hit region mismatch");
    }
    for (int width : {320,360,400,450,540,720}) {
        for (int section = 0; section < 4; ++section) {
            const int first = (section * width + 3) / 4;
            const int last = ((section + 1) * width + 3) / 4 - 1;
            if (WidgetSectionAt(first,width) != section || WidgetSectionAt(last,width) != section)
                throw std::runtime_error("Four-cell hit region mismatch");
        }
    }
    if (WidgetSectionAt(360,360) != -1 || WidgetSectionAt(-1,360) != -1)
        throw std::runtime_error("Four-cell outside hit region mismatch");
    std::cout << "50 four-cell boundary checks passed.\n";
    std::cout << "29 hover hit-region cases passed (section boundaries, compact layout, DPI scaling, outside positions).\n";
}
