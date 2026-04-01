#!/bin/zsh
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: scripts/run_processor_smoke.sh <audio-file>"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AUDIO_FILE="$1"
DERIVED_DIR="/tmp/BolbolRefMasterDerived"
RESP="$DERIVED_DIR/Build/Intermediates.noindex/Bolbol RefMaster.build/Debug/Bolbol RefMaster - Shared Code.build/Objects-normal/arm64/82b82416624d2658e5098eb0a28c15c5-common-args.resp"
OUTPUT_BIN="/tmp/processor_smoke"
OUTPUT_OBJ="/tmp/processor_smoke.o"

cd "$ROOT_DIR"

xcodebuild -project "Builds/MacOSX/Bolbol RefMaster.xcodeproj" \
  -scheme "Bolbol RefMaster - VST3" \
  -configuration Debug \
  -derivedDataPath "$DERIVED_DIR" \
  build >/tmp/bolbol_processor_smoke_build.log 2>&1

xcrun clang++ @"$RESP" -I. -c Tests/ProcessorSmokeMain.cpp -o "$OUTPUT_OBJ"
xcrun clang++ -o "$OUTPUT_BIN" "$OUTPUT_OBJ" Builds/MacOSX/build/Debug/libBolbolRefMaster.a \
  -framework Cocoa -framework Foundation -framework AppKit -framework AudioToolbox -framework CoreAudio -framework CoreMIDI \
  -framework Accelerate -framework QuartzCore -framework IOKit -framework DiscRecording -framework WebKit \
  -framework Metal -framework MetalKit -framework Security

"$OUTPUT_BIN" "$AUDIO_FILE"
