// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "bc_concurrency_signal.h"
#include "bc_allocators.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#define SLEEP_DURATION_ZERO_MS ((size_t)0)
#define SLEEP_DURATION_SHORT_MS ((size_t)10)
#define SLEEP_DURATION_LONG_MS ((size_t)2000)
#define ALARM_DELAY_US ((suseconds_t)10000)

/* ===== Helpers ===== */

static bc_allocators_context_t* make_mem(void)
{
    bc_allocators_context_t* mem = NULL;
    bc_allocators_context_create(NULL, &mem);
    return mem;
}

/* ===== Tests ===== */

static void test_signal_handler_create_and_destroy(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));
    assert_non_null(handler);
    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_signal_should_stop_initially_false(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));

    bool should_stop = true;
    assert_true(bc_concurrency_signal_handler_should_stop(handler, &should_stop));
    assert_false(should_stop);

    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_signal_install_sigusr1(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));
    assert_true(bc_concurrency_signal_handler_install(handler, SIGUSR1));
    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_signal_install_restores_on_destroy(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();

    struct sigaction original;
    sigaction(SIGUSR2, NULL, &original);

    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));
    assert_true(bc_concurrency_signal_handler_install(handler, SIGUSR2));
    bc_concurrency_signal_handler_destroy(handler);

    struct sigaction restored;
    sigaction(SIGUSR2, NULL, &restored);
    assert_ptr_equal(restored.sa_handler, original.sa_handler);

    bc_allocators_context_destroy(mem);
}

static void test_signal_should_stop_after_signal(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));
    assert_true(bc_concurrency_signal_handler_install(handler, SIGUSR1));

    kill(getpid(), SIGUSR1);

    bool should_stop = false;
    assert_true(bc_concurrency_signal_handler_should_stop(handler, &should_stop));
    assert_true(should_stop);

    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_signal_install_too_many_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));

    static const int signals[BC_CONCURRENCY_SIGNAL_MAX_INSTALLED_SIGNALS] = {
        SIGUSR1, SIGUSR2, SIGALRM, SIGCHLD, SIGCONT, SIGTSTP, SIGTTIN, SIGTTOU,
    };
    for (size_t i = 0; i < BC_CONCURRENCY_SIGNAL_MAX_INSTALLED_SIGNALS; i++) {
        assert_true(bc_concurrency_signal_handler_install(handler, signals[i]));
    }

    assert_false(bc_concurrency_signal_handler_install(handler, SIGWINCH));

    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_signal_destroy_null_is_noop(void** state)
{
    (void)state;
    bc_concurrency_signal_handler_destroy(NULL);
}

static void test_timer_zero_duration_returns_true(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));

    bool interrupted = true;
    assert_true(bc_concurrency_sleep_milliseconds(SLEEP_DURATION_ZERO_MS, handler, &interrupted));
    assert_false(interrupted);

    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_timer_zero_duration_null_handler(void** state)
{
    (void)state;
    bool interrupted = true;
    assert_true(bc_concurrency_sleep_milliseconds(SLEEP_DURATION_ZERO_MS, NULL, &interrupted));
    assert_false(interrupted);
}

static void test_timer_short_duration_completes(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));

    bool interrupted = false;
    assert_true(bc_concurrency_sleep_milliseconds(SLEEP_DURATION_SHORT_MS, handler, &interrupted));
    assert_false(interrupted);

    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

static void test_timer_interrupted_by_signal(void** state)
{
    (void)state;
    bc_allocators_context_t* mem = make_mem();
    bc_concurrency_signal_handler_t* handler = NULL;
    assert_true(bc_concurrency_signal_handler_create(mem, &handler));
    assert_true(bc_concurrency_signal_handler_install(handler, SIGALRM));

    struct itimerval timer = {
        .it_value = {.tv_sec = 0, .tv_usec = ALARM_DELAY_US},
        .it_interval = {.tv_sec = 0, .tv_usec = 0},
    };
    setitimer(ITIMER_REAL, &timer, NULL);

    bool interrupted = false;
    assert_true(bc_concurrency_sleep_milliseconds(SLEEP_DURATION_LONG_MS, handler, &interrupted));
    assert_true(interrupted);

    bc_concurrency_signal_handler_destroy(handler);
    bc_allocators_context_destroy(mem);
}

/* ===== main ===== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_signal_handler_create_and_destroy),
        cmocka_unit_test(test_signal_should_stop_initially_false),
        cmocka_unit_test(test_signal_install_sigusr1),
        cmocka_unit_test(test_signal_install_restores_on_destroy),
        cmocka_unit_test(test_signal_should_stop_after_signal),
        cmocka_unit_test(test_signal_install_too_many_fails),
        cmocka_unit_test(test_signal_destroy_null_is_noop),
        cmocka_unit_test(test_timer_zero_duration_returns_true),
        cmocka_unit_test(test_timer_zero_duration_null_handler),
        cmocka_unit_test(test_timer_short_duration_completes),
        cmocka_unit_test(test_timer_interrupted_by_signal),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
