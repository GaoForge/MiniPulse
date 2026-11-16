#include "../TaskbarLayout.h"
#include <iostream>
#include <stdexcept>

void Check(int left, int right, int minimum, int preferred,
           std::vector<std::pair<int, int>> obstacles, int expectedLeft, int expectedWidth) {
    const TaskbarSlot slot = FindTaskbarSlot(left, right, minimum, preferred, obstacles);
    if (slot.left != expectedLeft || slot.width != expectedWidth) throw std::runtime_error("Unexpected placement");
    if (slot.width > 0) {
        if (slot.left < left || slot.left + slot.width > right || slot.width < minimum) {
            throw std::runtime_error("Slot outside allowed bounds");
        }
        for (const auto& item : obstacles) {
            if (item.second > item.first && slot.left < item.second && slot.left + slot.width > item.first) {
                throw std::runtime_error("Slot overlaps an occupied button");
            }
        }
    }
}

int main() {
    Check(0, 1000, 246, 300, {}, 700, 300);
    Check(0, 1000, 246, 300, {{300, 650}}, 700, 300);
    Check(0, 1000, 246, 300, {{100, 350}, {300, 800}}, 0, 0);
    Check(0, 1000, 246, 300, {{350, 600}, {850, 1000}}, 600, 250);
    Check(0, 1000, 246, 300, {{0, 754}}, 754, 246);
    Check(0, 1000, 246, 300, {{0, 755}}, 0, 0);
    Check(-1920, -400, 246, 300, {{-1100, -800}}, -700, 300);
    Check(0, 1000, 246, 300, {{-100, 100}, {1100, 1200}}, 700, 300);
    Check(100, 100, 246, 300, {}, 0, 0);
    Check(0, 1000, 324, 440, {}, 560, 440);
    Check(0, 1000, 324, 440, {{0, 650}}, 650, 350);
    Check(0, 1000, 324, 440, {{0, 677}}, 0, 0);
    Check(0, 1000, 256, 288, {}, 712, 288);
    Check(0, 1000, 256, 288, {{0, 730}}, 730, 270);
    Check(0, 1000, 256, 288, {{0, 744}}, 744, 256);
    Check(0, 1000, 256, 288, {{0, 745}}, 0, 0);
    Check(0,1000,320,360,{},640,360);
    Check(0,1000,320,360,{{0,660}},660,340);
    Check(0,1000,320,360,{{0,680}},680,320);
    Check(0,1000,320,360,{{0,681}},0,0);
    std::cout << "20 taskbar placement cases passed (bounds, overlap, five-cell width, negative coordinates).\n";
}
