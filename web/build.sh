#!/usr/bin/env bash
# Builds the browser engine. Needs emsdk on PATH:  source ~/emsdk/emsdk_env.sh
#
# Standalone wasm with no imports at all, so an AudioWorklet can instantiate it
# with a bare WebAssembly.instantiate() - no Emscripten JS runtime, no fetch
# inside the worklet. Memory growth is off so the Float32Array views the
# worklet holds over the heap stay valid for the life of the node.
set -euo pipefail
cd "$(dirname "$0")/.."

em++ -O3 -std=c++17 -DTEST -I eurorack -I Source \
  web/wasm_bridge.cpp Source/CloudsEngine.cpp \
  eurorack/clouds/dsp/correlator.cc eurorack/clouds/dsp/granular_processor.cc \
  eurorack/clouds/dsp/mu_law.cc eurorack/clouds/dsp/pvoc/frame_transformation.cc \
  eurorack/clouds/dsp/pvoc/phase_vocoder.cc eurorack/clouds/dsp/pvoc/stft.cc \
  eurorack/clouds/resources.cc eurorack/stmlib/dsp/atan.cc \
  eurorack/stmlib/dsp/units.cc eurorack/stmlib/utils/random.cc \
  -sSTANDALONE_WASM=1 --no-entry \
  -sEXPORTED_FUNCTIONS='["_cloudius_init","_cloudius_buf_l","_cloudius_buf_r","_cloudius_set_params","_cloudius_set_mode","_cloudius_set_quality","_cloudius_trigger","_cloudius_set_gate","_cloudius_process","_cloudius_head","_cloudius_buffer_seconds","_cloudius_latency","_cloudius_pitch_semitones"]' \
  -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=16MB \
  -o web/cloudius.wasm

echo "built web/cloudius.wasm"
