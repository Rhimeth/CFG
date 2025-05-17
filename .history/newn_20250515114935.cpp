#include <iostream>
#include <vector>
#include <memory>

// Forward declarations of factory functions
std::shared_ptr<class Shape> makeCircle(double radius);
std::shared_ptr<class Shape> makeRectangle(double width, double height);

int main() {
    std::vector<std::shared_ptr<Shape>> shapes;

    shapes.push_back(makeCircle(3.0));
    shapes.push_back(makeRectangle(4.0, 5.0));

    for (const auto& shape : shapes) {
        shape->print();
    }

    return 0;
}
