#ifndef SHAPES_HPP
#define SHAPES_HPP

#include <iostream>
#include <cmath>

class Circle {
private:
    double radius;

public:
    Circle(double r);
    double area() const;
    void print() const;
};

#endif
