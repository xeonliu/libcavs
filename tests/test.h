/*
 * Copyright (c) 2026 libcavs contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Test checks that remain active when NDEBUG is defined.
 */
#ifndef CAVS_TEST_H
#define CAVS_TEST_H

#include <stdio.h>
#include <stdlib.h>

static void cavs_test_fail(const char *expression, const char *file, int line) {
    fprintf(stderr, "%s:%d: test check failed: %s\n", file, line, expression);
    fflush(stderr);
    exit(EXIT_FAILURE);
}

#define TEST_CHECK(expression) \
    ((expression) ? (void)0 : cavs_test_fail(#expression, __FILE__, __LINE__))

#endif
