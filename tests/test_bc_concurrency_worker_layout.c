// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_concurrency_context_internal.h"

static void test_worker_struct_has_state_mutex(void** state)
{
    (void)state;
    bc_concurrency_worker_t worker;
    assert_non_null(&worker.state_mutex);
    assert_non_null(&worker.ready_cond);
    assert_non_null(&worker.done_cond);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_worker_struct_has_state_mutex),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
