# SPDX-License-Identifier: MIT

fuzzer_c_args = []
fuzzer_link_args = []
if cc.get_id() == 'clang'
    fuzzer_c_args += ['-fsanitize=fuzzer', '-DBC_FUZZ_LIBFUZZER']
    fuzzer_link_args += ['-fsanitize=fuzzer']
endif

fuzz_targets = [
    'bc_concurrency_dispatch',
    'bc_concurrency_queue',
]

foreach name : fuzz_targets
    executable('fuzz_' + name,
        'fuzz_' + name + '.c',
        dependencies : [bc_concurrency_dep],
        c_args : fuzzer_c_args,
        link_args : fuzzer_link_args,
    )
endforeach
