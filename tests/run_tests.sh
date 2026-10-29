#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# n001se host tests: the maths, the platform pieces kept from Felucca / X0X / FoMni / FiMba-1, the instrument
# (built without libm, as on the device), and the whole firmware app in the simulator
# (tests/scenarios/*.kal: the step keys, the bars, MIDI and its clock, persistence), with screenshots and audio in
# build/scenarios/ and 16 bars of the starting set in build/host/n001se_16bars.wav.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FAIL=0
OUT=build/host
mkdir -p "$OUT" build/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run fastmath sh -c "$CC -O2 -ffp-contract=off -Wall -Wextra -Werror -o $OUT/fastmath_test tests/host/fastmath_test.c -lm && $OUT/fastmath_test"
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run uac sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/uac_test tests/host/uac_test.c -lm && $OUT/uac_test"
run trs sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/trs_test tests/host/trs_test.c && $OUT/trs_test"
# no -lm: a libm call that crept into the engine fails to link here, as it would on the device
run n001se sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DOM_HOST -Ifirmware/src/dsp \
    -o $OUT/n001se_test tests/host/n001se_test.c firmware/src/dsp/n001se.c firmware/src/dsp/nfm.c firmware/src/dsp/nseq.c \
    firmware/src/dsp/nfx.c firmware/src/dsp/nacid.c && $OUT/n001se_test $OUT"
# again with the sanitizer: an unaligned store or an index out of bounds traps on the FM-1's CPU
run n001se-ubsan sh -c "$CC -O1 -g -std=c99 -fsanitize=alignment,undefined -fno-sanitize-recover=all -DOM_HOST -Ifirmware/src/dsp \
    -o $OUT/n001se_ubsan tests/host/n001se_test.c firmware/src/dsp/n001se.c firmware/src/dsp/nfm.c firmware/src/dsp/nseq.c \
    firmware/src/dsp/nfx.c firmware/src/dsp/nacid.c && $OUT/n001se_ubsan"
# the acid line against a 303's rules: gate, slide, accent, the filter, the generator by Chaos
run nacid sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DOM_HOST -Ifirmware/src/dsp \
    -o $OUT/nacid_test tests/host/nacid_test.c firmware/src/dsp/nacid.c firmware/src/dsp/nfm.c -lm && $OUT/nacid_test"
# SPACE: the reverb's rooms and the delay's lengths, slow glides, long stays, no jumps
run space sh -c "$CC -O2 -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DOM_HOST -Ifirmware/src/dsp \
    -o $OUT/space_test tests/host/space_test.c firmware/src/dsp/nfx.c firmware/src/dsp/nfm.c -lm && $OUT/space_test"
# the GLUE as a mastering chain: matched loudness, its ceiling, width and the lows in mono, the EQ's lean
run glue sh -c "$CC -O2 -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DOM_HOST -Ifirmware/src/dsp \
    -o $OUT/glue_test tests/host/glue_test.c firmware/src/dsp/n001se.c firmware/src/dsp/nfm.c firmware/src/dsp/nseq.c \
    firmware/src/dsp/nfx.c firmware/src/dsp/nacid.c && $OUT/glue_test $OUT"
run storage sh -c "$CC -O2 -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
# the update path, against the firmware package (Felucca's tests; needs ./build.sh)
if [ -f build/n001se.fwsc ]; then
    run ota-entry sh -c "$CC -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test build/n001se.fwsc"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip update-loader (needs AC79_SDK, as the build)"
    else
    run update-loader sh -c "head -c 100000 build/n001se.bin > $OUT/old_app.bin && \
        python3 tools/fm1pkg_make.py $OUT/old_app.bin build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc build/n001se.fwsc"
    fi
    run installer python3 tests/install_test.py
    # the package taken apart and every CRC checked; against FoMni's own package when one is there
    # (build/fomni-ref: see BUILDING.md, "Before installing")
    REF=""
    [ -f build/fomni-ref/build/omni.fwsc ] && REF="--ref build/fomni-ref/build/omni.fwsc"
    run package sh -c "python3 tools/check_fwsc.py build/n001se.fwsc --app build/n001se.bin $REF"
else
    echo "  skip update-path tests (no build/n001se.fwsc: run ./build.sh)"
fi
run host-build sh host/build_host.sh
if false && command -v emcc >/dev/null 2>&1; then   # the web emulator is not ported to n001se yet
    run emu sh -c "sh web/emu/build.sh >/dev/null 2>&1 && node tests/host/emu_test.mjs build/emu/kalimba.wasm"
    CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    [ -x "$CHROME" ] || CHROME="$(command -v google-chrome || command -v chromium || true)"
    # the installer as the page ships it, against a simulated FM-1
    run updater node tests/web/updater_test.mjs
    if [ -n "$CHROME" ]; then
        run standalone node tests/host/standalone_test.mjs "$CHROME"
        if [ -f build/n001se.fwsc ]; then       # the installer's buttons, end to end (needs a firmware build)
            run installer-page node tests/web/installer_page_test.mjs "$CHROME"
        fi
    fi
fi
for s in tests/scenarios/*.kal; do   # (n001se scenarios keep FiMba's .kal suffix)
    n=$(basename "$s" .kal)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/n001se_host "$s" "build/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
