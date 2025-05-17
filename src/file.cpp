#include <iostream>
#include <vector>
#include <functional>

class Calculator {
public:
    int add(int a, int b) {
        return a + b;
    }

    int multiply(int a, int b) {
        return a * b;
    }
};

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

void processNumbers(const std::vector<int>& numbers, std::function<int(int, int)> op) {
    int result = numbers.empty() ? 0 : numbers[0];
    for (size_t i = 1; i < numbers.size(); i++) {
        result = op(result, numbers[i]);
    }
    std::cout << "Processed result: " << result << std::endl;
}

void testFunction() {
    Calculator calc;
    int x = 5, y = 3;

    std::cout << "Sum: " << calc.add(x, y) << std::endl;
    std::cout << "Product: " << calc.multiply(x, y) << std::endl;

    std::vector<int> values = {1, 2, 3, 4, 5};
    processNumbers(values, [](int a, int b) { return a + b; });

    try {
        if (x > y) throw std::runtime_error("Test exception");
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

    for (int i = 0; i < 3; i++) {
        std::cout << "Loop iteration: " << i << std::endl;
    }

    int fact = factorial(5);
    std::cout << "Factorial(5): " << fact << std::endl;
}

int main() {
    testFunction();
    return 0;
}
