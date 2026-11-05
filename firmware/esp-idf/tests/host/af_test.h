#pragma once
/* Мини-харнесс host-тестов AtomFast: RUN(fn) печатает PASS/FAIL по имени теста; CHECK называет тест в строке FAIL. */
#include <stdio.h>
static int g_failures = 0;
static const char *g_test = "?";
static int g_tests = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define RUN(fn) do { int f0_ = g_failures; g_test = #fn; g_tests++; fn(); if (g_failures == f0_) printf("PASS %s\n", #fn); } while (0)
#define TESTS_DONE() do { printf("TESTS %d FAILURES %d\n", g_tests, g_failures); return g_failures ? 1 : 0; } while (0)
