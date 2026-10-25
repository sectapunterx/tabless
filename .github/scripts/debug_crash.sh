#!/usr/bin/env bash
# TEMPORARY (hotfix/0.10-full-ci): find the heap corruption behind the Qt 6.9.1
# QML crashes with valgrind (Linux) - the first invalid write, and who freed it.
set -u
cd build/tests || exit 0
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic
if [[ $RUNNER_OS != Linux ]]; then exit 1; fi
sudo apt-get install -y --no-install-recommends valgrind > /dev/null
suites="KeymapSchemes ShellLayout R3Ui TaskDocument"
for t in $suites; do
  HEAP_QML_TEST_FILE=vg$t timeout 1800 valgrind --error-limit=no --num-callers=40 --smc-check=all-non-file \
    --log-file=../../vg-$t.txt ./heap_qml_tests -input ../../tests/qml/tst_$t.qml > ../../vg-$t.out 2>&1 &
done
wait
for t in $suites; do
  echo "::group::valgrind $t"
  grep -E "^(FAIL|Totals)|Received signal" ../../vg-$t.out | tail -5
  echo "invalid: $(grep -c 'Invalid ' ../../vg-$t.txt)"
  # The first few distinct errors, whole.
  awk '/^==[0-9]+== (Invalid|Mismatched|Use of|Source and destination)/{n++} n>=1 && n<=5' ../../vg-$t.txt | head -300
  echo "::endgroup::"
done
exit 1
