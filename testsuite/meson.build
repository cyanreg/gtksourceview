test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
  'NO_AT_BRIDGE=1',
]

testsuite_c_args = [
  '-DG_LOG_DOMAIN="GtkSourceView"',
  '-DTOP_SRCDIR="@0@"'.format(srcdir),
]

testsuite_sources = [
  ['test-buffer'],
  ['test-buffer-input-stream'],
  ['test-buffer-output-stream'],
  ['test-completion-model'],
  ['test-completion-words'],
  ['test-encoding'],
  ['test-file-loader'],
  ['test-file-saver'],
  ['test-iter'],
  ['test-language'],
  ['test-languagemanager'],
  ['test-mark'],
  ['test-printcompositor'],
  ['test-regex'],
  ['test-region'],
  ['test-search-context'],
  ['test-space-drawer'],
  ['test-stylescheme'],
  ['test-styleschememanager'],
  ['test-undo-manager'],
  ['test-utils'],
  ['test-view'],
]

foreach test: testsuite_sources
  test_name = test.get(0)
  test_sources = [
    '@0@.c'.format(test_name),
  ]

  # MSVC builds: We need to link to the main GtkSourceView .lib except
  # for test-stylescheme.c, where we must link only to the static core lib
  test_exe = executable(test_name, test_sources,
          c_args: testsuite_c_args,
    dependencies: cc.get_id() == 'msvc' and test_name == 'test-stylescheme' ? [core_dep] : [gtksource_dep],
         install: get_option('install_tests'),
     install_dir: testexecdir
  )

  test(test_name, test_exe, suite: package_string, env: test_env)

  if get_option('install_tests')
    test_t = configuration_data()
    test_t.set('testexecdir', testexecdir)
    test_t.set('test', test_name)

    configure_file(
              input: 'gtksourceview.test.in',
             output: '@0@.test'.format(test_name),
      configuration: test_t,
        install_dir: testdatadir
    )
  endif
endforeach

if get_option('install_tests')
  install_subdir('language-specs', install_dir: testexecdir)
  install_subdir('styles', install_dir: testexecdir)
endif
