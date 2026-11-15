#pragma once

// Minimal self-registering test harness (no network download needed).
#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

inline int& failures() {
    static int count = 0;
    return count;
}

}  // namespace testing

#define FS_CONCAT2(a, b) a##b
#define FS_CONCAT(a, b) FS_CONCAT2(a, b)
#define TEST_CASE(name)                                                                 \
    static void FS_CONCAT(test_fn_, __LINE__)();                                        \
    static const testing::Registrar FS_CONCAT(test_reg_, __LINE__)(name, FS_CONCAT(test_fn_, __LINE__)); \
    static void FS_CONCAT(test_fn_, __LINE__)()

#define FS_FAIL(msg)                                                                    \
    do {                                                                                \
        std::ostringstream fs_os;                                                       \
        fs_os << __FILE__ << ":" << __LINE__ << ": " << msg;                            \
        throw testing::Failure{fs_os.str()};                                            \
    } while (0)

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) FS_FAIL("CHECK(" #cond ") failed");                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto fs_a = (a);                                                          \
        const auto fs_b = (b);                                                          \
        if (!(fs_a == fs_b)) FS_FAIL("CHECK_EQ(" #a ", " #b ") failed: " << fs_a << " vs " << fs_b); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                           \
    do {                                                                                \
        const double fs_a = (a);                                                        \
        const double fs_b = (b);                                                        \
        if (!(std::abs(fs_a - fs_b) <= (tol)))                                          \
            FS_FAIL("CHECK_NEAR(" #a ", " #b ") failed: " << fs_a << " vs " << fs_b);   \
    } while (0)

#define CHECK_THROWS_AS(expr, type)                                                     \
    do {                                                                                \
        bool fs_thrown = false;                                                         \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const type&) {                                                         \
            fs_thrown = true;                                                           \
        }                                                                               \
        if (!fs_thrown) FS_FAIL("expected " #expr " to throw " #type);                  \
    } while (0)
