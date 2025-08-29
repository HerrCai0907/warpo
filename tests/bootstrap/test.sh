rm tests/bootstrap/tmp

set -e

echo "first build"
./build/tools/asc/warpo_asc assemblyscript/src/glue/wasm/index.ts assemblyscript/src/index-wasm.ts \
  --disable-feature nontrapping-f2i \
  --initialMemory 768 \
  --exportStart _initialize --exportRuntime --exportTable \
  --use ASC_GC_TESTING=1 ASC_GC_IDLEFACTOR=0 ASC_GC_GRANULARITY=64 ASC_GC_SWEEPFACTOR=1000 \
  -o tests/bootstrap/tmp/assemblyscript.warpo-test.wasm

echo "second build"
./build/tools/asc/warpo_asc assemblyscript/src/glue/wasm/index.ts assemblyscript/src/index-wasm.ts \
  --disable-feature nontrapping-f2i \
  --initialMemory 768 \
  --exportStart _initialize --exportRuntime --exportTable \
  --asc-wasm tests/bootstrap/tmp/assemblyscript.warpo-test.wasm \
  --use ASC_GC_TESTING=1 ASC_GC_IDLEFACTOR=0 ASC_GC_GRANULARITY=64 ASC_GC_SWEEPFACTOR=1000 \
  -o tests/bootstrap/tmp/assemblyscript.warpo-test-bootstrap.wasm
  
echo "third build"
./build/tools/asc/warpo_asc assemblyscript/src/glue/wasm/index.ts assemblyscript/src/index-wasm.ts \
  --disable-feature nontrapping-f2i \
  --initialMemory 768 \
  --exportStart _initialize --exportRuntime --exportTable \
  --asc-wasm tests/bootstrap/tmp/assemblyscript.warpo-test-bootstrap.wasm \
  --use ASC_GC_TESTING=1 ASC_GC_IDLEFACTOR=0 ASC_GC_GRANULARITY=64 ASC_GC_SWEEPFACTOR=1000 \
  -o tests/bootstrap/tmp/assemblyscript.warpo-test-bootstrap.wasm
  
git --no-pager diff --no-index tests/bootstrap/tmp/assemblyscript.warpo-test.wat tests/bootstrap/tmp/assemblyscript.warpo-test-bootstrap.wat

echo "success"
