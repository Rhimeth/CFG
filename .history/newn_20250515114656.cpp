#include "shapes.hpp"

Circle::Circle(double r) : radius(r) {}

double Circle::area() const {
    return 3.14159 * radius * radius;
}

void Circle::print() const {
    std::cout << "Circle with radius: " << radius
              << " has area: " << area() << std::endl;
}

int main() {
    Circle c(3.0);
    c.print();
    return 0;
}
