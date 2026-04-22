# SPDX-License-Identifier: MIT

project('bc-concurrency', 'c',
  version : '1.1.0',
  license : 'MIT',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
    'optimization=3',
    'default_library=static',
  ],
)

cc = meson.get_compiler('c')

extra_c_args = [
  '-pedantic',
  '-Wconversion',
  '-Wshadow',
  '-Wformat=2',
]

extra_c_args += '-D_GNU_SOURCE'

foreach arg : extra_c_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

lto_c_args = []
lto_link_args = []
if get_option('buildtype') in ['release', 'minsize']
  arch_value = get_option('arch')
  if arch_value == ''
    arch_value = 'x86-64-v3'
  endif

  add_project_arguments(
    '-march=' + arch_value,
    '-funroll-loops',
    '-fomit-frame-pointer',
    '-ffunction-sections',
    '-fdata-sections',
    language : 'c',
  )
  add_project_link_arguments('-Wl,--gc-sections', language : 'c')
endif

pkg = import('pkgconfig')

bc_core_dep = dependency('bc-core')
bc_allocators_dep = dependency('bc-allocators')
thread_dep = dependency('threads', required : true)

bc_concurrency_public_inc = include_directories('include/public')
bc_concurrency_internal_inc = include_directories('include/internal')

subdir('src')

pkg.generate(
  bc_concurrency_lib,
  name : 'bc-concurrency',
  description : 'Concurrency : threads, dispatch, slots, signal',
  version : meson.project_version(),
  extra_cflags : ['-I${includedir}/bc'],
)

install_headers(
  'include/public/bc_concurrency.h',
  'include/public/bc_concurrency_signal.h',
  subdir : 'bc',
)

if get_option('tests')
  subdir('tests')
endif

if get_option('benchmarks')
  subdir('benchmarks')
endif

if get_option('fuzzing')
  subdir('fuzzing')
endif
