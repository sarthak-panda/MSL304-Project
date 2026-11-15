#include <exception>
#include <iostream>

#include "test_framework.hpp"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int passed = 0;
    int failed = 0;
    for (const auto& test : testing::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        try {
            test.fn();
            ++passed;
        } catch (const testing::Failure& f) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << "\n       " << f.message << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << "\n       unexpected exception: " << e.what() << '\n';
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
