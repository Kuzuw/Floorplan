#pragma once

// Minimal dependency-free test harness. Each TEST registers itself; the
// runner executes one test by name (as CTest does) or all of them.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace fptest
{

struct TestCase
{
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Registrar
{
    Registrar (const char* name, void (*fn)()) { registry().push_back ({ name, fn }); }
};

inline void fail (const char* file, int line, const char* expr)
{
    ++failures();
    std::printf ("  FAILED %s:%d: %s\n", file, line, expr);
}

inline int runMain (int argc, char** argv)
{
    if (argc > 1 && std::strcmp (argv[1], "--list") == 0)
    {
        for (auto& t : registry())
            std::printf ("%s\n", t.name);
        return 0;
    }

    int run = 0;
    for (auto& t : registry())
    {
        if (argc > 1 && std::strcmp (argv[1], t.name) != 0)
            continue;

        const int before = failures();
        std::printf ("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf ("[ %s ] %s\n", failures() == before ? " OK " : "FAIL", t.name);
        ++run;
    }

    if (run == 0)
    {
        std::printf ("No test matched.\n");
        return 2;
    }

    std::printf ("%d test(s), %d failure(s)\n", run, failures());
    return failures() == 0 ? 0 : 1;
}

} // namespace fptest

#define FP_TEST(name)                                                     \
    static void name();                                                   \
    static const fptest::Registrar name##_registrar (#name, &name);       \
    static void name()

#define CHECK(cond) \
    do { if (! (cond)) fptest::fail (__FILE__, __LINE__, #cond); } while (false)

#define CHECK_NEAR(a, b, tol) \
    do { if (! (std::abs ((double) (a) - (double) (b)) <= (double) (tol))) { \
        std::printf ("    (%g vs %g)\n", (double) (a), (double) (b));           \
        fptest::fail (__FILE__, __LINE__, #a " ~= " #b); } } while (false)
