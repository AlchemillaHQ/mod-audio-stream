#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _tests_run = 0;
static int _tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void test_##name(void)

#define CHECK(cond) do { \
    _tests_run++; \
    if (!(cond)) { \
        _tests_failed++; \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

#define CHECK_STREQ(a, b) do { \
    _tests_run++; \
    if (strcmp((a), (b)) != 0) { \
        _tests_failed++; \
        fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
    } \
} while(0)

#define RUN_TEST(name) do { \
    fprintf(stderr, "  %s\n", #name); \
    test_##name(); \
} while(0)

#define TEST_MAIN() \
    int main(void) { \
        fprintf(stderr, "Running tests...\n"); \
        run_tests(); \
        fprintf(stderr, "\n%d tests, %d failed\n", _tests_run, _tests_failed); \
        return _tests_failed > 0 ? 1 : 0; \
    }

#define TEST_SUITE_END() \
    void run_tests(void)

#endif
