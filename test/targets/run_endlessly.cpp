#include <iostream>
#include <ostream>

int main() {
    volatile int i = 3;
    while (true) std::cout << i << std::endl;
}
