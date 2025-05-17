#include <iostream>
#include <cmath>
#include <memory>

class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
    virtual void print() const = 0;
};

class Circle : public Shape {
private:
    double radius;
public:
    Circle(double r) : radius(r) {}
    double area() const override {
        return 3.14159 * radius * radius;
    }
    void print() const override {
        std::cout << "Circle(radius=" << radius << ", area=" << area() << ")\n";
    }
};

class Rectangle : public Shape {
private:
    double width, height;
public:
    Rectangle(double w, double h) : width(w), height(h) {}
    double area() const override {
        return width * height;
    }
    void print() const override {
        std::cout << "Rectangle(" << width << "x" << height << ", area=" << area() << ")\n";
    }
};

// Expose factory functions for external use
std::shared_ptr<Shape> makeCircle(double radius) {
    return std::make_shared<Circle>(radius);
}

std::shared_ptr<Shape> makeRectangle(double width, double height) {
    return std::make_shared<Rectangle>(width, height);
}
