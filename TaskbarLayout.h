#pragma once
#include <algorithm>
#include <utility>
#include <vector>

struct TaskbarSlot { int left = 0; int width = 0; };

// Select the rightmost usable gap before the notification area. Overlapping
// occupied intervals are merged; never force a slot over taskbar buttons.
inline TaskbarSlot FindTaskbarSlot(int left, int right, int minimum, int preferred,
                                 std::vector<std::pair<int, int>> occupied) {
    TaskbarSlot result;
    if (right <= left || minimum <= 0 || preferred < minimum) return result;
    std::sort(occupied.begin(), occupied.end());
    int previous = left;
    for (const auto& item : occupied) {
        if (item.second <= left || item.first >= right || item.second <= item.first) continue;
        const int start = std::max(left, item.first), end = std::min(right, item.second);
        const int gap = start - previous;
        if (gap >= minimum) {
            result.width = std::min(preferred, gap);
            result.left = start - result.width;
        }
        previous = std::max(previous, end);
    }
    const int gap = right - previous;
    if (gap >= minimum) {
        result.width = std::min(preferred, gap);
        result.left = right - result.width;
    }
    return result;
}
