bc_concurrency_lib = static_library('bc-concurrency',
    'context/bc_concurrency.c',
    'dispatch/bc_concurrency_async.c',
    'queue/bc_concurrency_queue.c',
    'signal/bc_concurrency_signal.c',
    'platform/bc_concurrency_platform.c',
    include_directories : [bc_concurrency_public_inc, bc_concurrency_internal_inc],
    dependencies : [bc_core_dep, bc_allocators_dep, thread_dep],
    c_args : lto_c_args,
    link_args : lto_link_args,
    install : true,
)

bc_concurrency_dep = declare_dependency(
    link_with : bc_concurrency_lib,
    include_directories : bc_concurrency_public_inc,
    dependencies : [bc_core_dep, bc_allocators_dep, thread_dep],
)
