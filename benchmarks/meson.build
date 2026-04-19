bench_names = [
    'submit_wait_roundtrip', 'batch_throughput', 'scalability',
    'parallel_for', 'wave_throughput', 'slot_accumulation',
    'main_contribution',
]

foreach name : bench_names
    benchmark('bench_bc_concurrency_' + name,
        executable('bench_bc_concurrency_' + name,
            'bench_bc_concurrency_' + name + '.c',
            dependencies : [bc_concurrency_dep, bc_core_dep],
        ),
        timeout : 300,
    )
endforeach
