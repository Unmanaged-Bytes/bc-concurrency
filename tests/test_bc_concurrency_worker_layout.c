// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency_context_internal.h"

static void test_work_ready_and_work_done_on_separate_cache_lines(void** state)
{
    (void)state;

    const size_t ready_offset = offsetof(bc_concurrency_worker_t, work_ready);
    const size_t done_offset = offsetof(bc_concurrency_worker_t, work_done);

    assert_true(done_offset - ready_offset >= 64);
}

static void test_work_ready_and_sleeping_on_separate_cache_lines(void** state)
{
    (void)state;

    const size_t ready_offset = offsetof(bc_concurrency_worker_t, work_ready);
    const size_t sleeping_offset = offsetof(bc_concurrency_worker_t, sleeping);

    assert_true(sleeping_offset - ready_offset >= 64);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_work_ready_and_work_done_on_separate_cache_lines),
        cmocka_unit_test(test_work_ready_and_sleeping_on_separate_cache_lines),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
