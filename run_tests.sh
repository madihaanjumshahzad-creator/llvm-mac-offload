#!/bin/sh
# build the pass and run all host tests
set -e
cd "$(dirname "$0")"
PLUGIN=build/MacOffload.so
CFLAGS="-O1 -fno-vectorize -fno-unroll-loops -fno-discard-value-names"

cmake -S . -B build -DLLVM_DIR="$(llvm-config --cmakedir)" > /dev/null
cmake --build build > /dev/null
mkdir -p out

echo "== 1. data-movement report (before offload)"
clang $CFLAGS -S -emit-llvm tests/dot.c -o out/dot.ll
opt -load-pass-plugin=$PLUGIN -passes=mac-report -disable-output out/dot.ll

echo "== 2. offload the int8 dot product"
opt -load-pass-plugin=$PLUGIN -passes=mac-offload out/dot.ll -S -o out/dot_offload.ll
grep -q "call i32 @mac_accel_dot" out/dot_offload.ll
echo "   call to mac_accel_dot found in out/dot_offload.ll"

echo "== 3. negative tests (nothing may be offloaded)"
clang $CFLAGS -S -emit-llvm tests/negative.c -o out/negative.ll
opt -load-pass-plugin=$PLUGIN -passes=mac-offload out/negative.ll -S -o out/negative_after.ll
if grep -q mac_accel_dot out/negative_after.ll; then echo "   FAIL: negative case offloaded"; exit 1; fi
echo "   ok, no loop offloaded"

echo "== 4. run on the host with the software model of the accelerator"
clang -O1 out/dot_offload.ll tests/main.c runtime/mac_runtime_host.c -o out/test_host
./out/test_host

echo "== 5. same pass for the RISC-V target (rv32im)"
clang --target=riscv32 -march=rv32im $CFLAGS -S -emit-llvm tests/dot.c -o out/dot_rv.ll
opt -load-pass-plugin=$PLUGIN -passes=mac-offload out/dot_rv.ll -S -o out/dot_rv_offload.ll
llc -O2 out/dot_rv_offload.ll -o out/dot_rv.s
grep -q "mac_accel_dot" out/dot_rv.s
echo "   RISC-V assembly calls mac_accel_dot (see out/dot_rv.s)"
