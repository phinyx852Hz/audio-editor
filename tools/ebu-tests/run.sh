#!/bin/sh
# Measures PHI's loudness meters against the EBU loudness test set, then the Φ Limiter,
# Compressor, Multiband and EQ with them. See docs/testing/ebu-tests.md.
#   EBU_SET=/path/to/ebu-loudness-test-setv05 tools/ebu-tests/run.sh [build-dir]
# Needs g++ (C++20), Python 3 with NumPy and Matplotlib. The test set is the EBU's own
# download (https://tech.ebu.ch/loudness) and is not kept in this repository.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../Source/core"
out=$(mkdir -p "${1:-build-ebu}" && cd "${1:-build-ebu}" && pwd)
: "${EBU_SET:?set EBU_SET to the folder of the unzipped EBU test set}"
cd "$out"

# PHI's K-weighting, true-peak detector and gated integrated loudness, as they are in Dsp.cpp.
{
    cat <<'HDR'
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>
namespace dsp {
    namespace {
        constexpr double pi = 3.14159265358979323846;
        double power (double lufs)        { return std::pow (10.0, (lufs + 0.691) / 10.0); }
        double lufsFromPower (double p)   { return p > 0.0 ? -0.691 + 10.0 * std::log10 (p) : -200.0; }
    }
HDR
    awk '/ITU-R BS.1770 K-weighting: high shelf/{p=1} p{print} p&&/^    };$/{c++; if(c==2){exit}}' "$src/Dsp.h"
    awk '/class IntegratedLoudness/{p=1} p{print} p&&/^    };$/{exit}' "$src/Dsp.h"
    awk '/float KWeighting::Biquad::process/{p=1} /Speaker speakerAt \(int channel, int n\)/{exit} p{print}' "$src/Dsp.cpp" | sed '$d' | sed '$d'
    awk '/void IntegratedLoudness::add/{p=1} /static double loudnessRange/{exit} p{print}' "$src/Dsp.cpp"
    echo "}"
} > extracted.h

g++ -O2 -std=c++20 -I. "$here/meters.cpp" -o meters
g++ -O2 -std=c++20 -I. -I"$src" "$here/phi.cpp" "$src/PhiDynamics.cpp" "$src/Phi.cpp" "$src/Fft.cpp" -o phi

(cd "$EBU_SET" && ls *.wav | sort -V | while read -r f; do "$out/meters" "$f"; done) > meter_out.txt
EBU_SET="$EBU_SET" ./phi
OUT="$out" python3 "$here/conform.py"
OUT="$out" python3 "$here/analyse.py" > /dev/null
echo "Results and graphs in $out"
