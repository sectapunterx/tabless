#!/usr/bin/env bash
# TEMPORARY (hotfix/0.10-full-ci): which QML suites crash on Qt 6.9.1, and why.
set -u
re='heap_qml_tests\.(KeymapSchemes|ShellLayout|MouseNav|ZoomKeys|PanelKeys|SettingsView|R3Ui)$'
for i in 1 2 3; do
  echo "::group::plain run $i"
  ctest --test-dir build/tests -R "$re" --parallel 4 --timeout 300 2>&1 | grep -E "Test +#|tests passed"
  echo "::endgroup::"
done
for i in 1 2 3; do
  echo "::group::QV4_GC_TIMELIMIT=0 run $i"
  QV4_GC_TIMELIMIT=0 ctest --test-dir build/tests -R "$re" --parallel 4 --timeout 300 2>&1 | grep -E "Test +#|tests passed"
  echo "::endgroup::"
done
if command -v gdb >/dev/null; then
  cd build/tests || exit 0
  for t in KeymapSchemes ShellLayout MouseNav; do
    echo "::group::gdb $t"
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic HEAP_QML_TEST_FILE=gdb$t \
      gdb -q -batch -ex run -ex "thread apply all bt 40" --args ./heap_qml_tests -input ../../tests/qml/tst_$t.qml 2>&1 \
      | grep -vE "^(PASS|\[New Thread|\[Thread .* exited)" | tail -150
    echo "::endgroup::"
  done
fi
exit 0
