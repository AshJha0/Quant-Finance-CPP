// Micro test harness -- keeps the C++ port zero-dependency, mirroring
// the Java project's ethos. One TEST per behavior; EXPECT_* record
// failures with file:line; main() runs everything and returns nonzero
// on any failure so CTest sees red.
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace qtest {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void fail(const char* file, int line, const std::string& msg) {
    std::printf("  FAIL %s:%d  %s\n", file, line, msg.c_str());
    ++failures();
}

inline int run_all() {
    int n = 0;
    for (auto& c : registry()) {
        int before = failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        std::printf(failures() == before ? "[  OK  ] %s\n" : "[ BAD  ] %s\n", c.name);
        ++n;
    }
    std::printf("%d test(s), %d failure(s)\n", n, failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace qtest

#define TEST(name)                                                        \
    static void qtest_fn_##name();                                        \
    static qtest::Registrar qtest_reg_##name(#name, qtest_fn_##name);     \
    static void qtest_fn_##name()

#define EXPECT_TRUE(cond)                                                 \
    do {                                                                  \
        if (!(cond)) qtest::fail(__FILE__, __LINE__, "EXPECT_TRUE(" #cond ")"); \
    } while (0)

#define EXPECT_NEAR(a, b, tol)                                            \
    do {                                                                  \
        double qa = (a), qb = (b), qt = (tol);                            \
        if (!(std::fabs(qa - qb) <= qt))                                  \
            qtest::fail(__FILE__, __LINE__,                               \
                        std::string(#a " vs " #b ": ") + std::to_string(qa) + \
                            " != " + std::to_string(qb) + " (tol " +      \
                            std::to_string(qt) + ")");                    \
    } while (0)

#define EXPECT_THROWS(expr)                                               \
    do {                                                                  \
        bool qthrew = false;                                              \
        try {                                                             \
            (void)(expr);                                                 \
        } catch (...) {                                                   \
            qthrew = true;                                                \
        }                                                                 \
        if (!qthrew) qtest::fail(__FILE__, __LINE__, "EXPECT_THROWS(" #expr ") did not throw"); \
    } while (0)

#define QTEST_MAIN()                                                      \
    int main() { return qtest::run_all(); }
