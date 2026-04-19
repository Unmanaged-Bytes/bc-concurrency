cmocka_dep = dependency('cmocka')

simple_tests = [
    'lifecycle', 'submit_wait', 'async', 'worker_resources',
    'context_slots', 'signal', 'physical_isolation',
    'main_participation', 'worker_layout',
    'worker_count_configurable',
    'queue',
]

foreach name : simple_tests
    test('bc_concurrency_' + name,
        executable('test_bc_concurrency_' + name,
            'test_bc_concurrency_' + name + '.c',
            include_directories : [bc_concurrency_public_inc, bc_concurrency_internal_inc],
            dependencies : [cmocka_dep, bc_concurrency_dep],
        ),
        timeout : 30
    )
endforeach

test_bc_concurrency_errors = executable('test_bc_concurrency_errors',
    'test_bc_concurrency_errors.c',
    '../src/context/bc_concurrency.c',
    '../src/dispatch/bc_concurrency_async.c',
    '../src/signal/bc_concurrency_signal.c',
    '../src/platform/bc_concurrency_platform.c',
    include_directories : [bc_concurrency_public_inc, bc_concurrency_internal_inc],
    dependencies : [cmocka_dep, bc_core_dep, bc_allocators_dep, thread_dep],
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=bc_allocators_pool_allocate',
        '-Wl,--wrap=pthread_attr_init',
        '-Wl,--wrap=pthread_attr_setstacksize',
        '-Wl,--wrap=pthread_create',
    ],
)
test('bc_concurrency_errors', test_bc_concurrency_errors, timeout : 30)

test_bc_concurrency_errors_v2 = executable('test_bc_concurrency_errors_v2',
    'test_bc_concurrency_errors_v2.c',
    '../src/context/bc_concurrency.c',
    '../src/dispatch/bc_concurrency_async.c',
    '../src/signal/bc_concurrency_signal.c',
    '../src/platform/bc_concurrency_platform.c',
    include_directories : [bc_concurrency_public_inc, bc_concurrency_internal_inc],
    dependencies : [cmocka_dep, bc_core_dep, bc_allocators_dep, thread_dep],
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=bc_allocators_pool_allocate',
        '-Wl,--wrap=bc_allocators_context_create',
        '-Wl,--wrap=pthread_attr_setstacksize',
        '-Wl,--wrap=pthread_create',
        '-Wl,--wrap=sigaction',
        '-Wl,--wrap=nanosleep',
    ],
)
test('bc_concurrency_errors_v2', test_bc_concurrency_errors_v2, timeout : 60)
