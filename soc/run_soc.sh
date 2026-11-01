#!/bin/sh
# Build the firmware with clang + mac-offload and run it on the RISC-V SoC.
# usage: soc/run_soc.sh <path to RISC-V_near_memory_MAC>
set -e
cd "$(dirname "$0")/.."
RV=${1:?give the path of the RISC-V_near_memory_MAC repo}
PLUGIN=build/MacOffload.so
T="--target=riscv32 -march=rv32im_zicsr_zicntr -mabi=ilp32"
CFLAGS="$T -O1 -fno-vectorize -fno-unroll-loops -fno-discard-value-names -ffreestanding -nostdlib -fno-builtin"
mkdir -p out/soc

# only dot.c goes through the pass
clang $CFLAGS -S -emit-llvm tests/dot.c -o out/soc/dot.ll
opt -load-pass-plugin=$PLUGIN -passes=mac-offload out/soc/dot.ll -o out/soc/dot_offload.bc
clang $CFLAGS -c out/soc/dot_offload.bc -o out/soc/dot.o
clang $CFLAGS -c soc/main_llvm.c -o out/soc/main.o
clang $CFLAGS -c runtime/mac_runtime_riscv.c -o out/soc/runtime.o
clang $T -c "$RV/fw/start.S" -o out/soc/start.o
ld.lld -T "$RV/fw/link.ld" out/soc/start.o out/soc/main.o out/soc/dot.o out/soc/runtime.o -o out/soc/firmware.elf
llvm-objcopy -O binary out/soc/firmware.elf out/soc/firmware.bin
python3 "$RV/fw/bin2hex.py" out/soc/firmware.bin out/soc/firmware.hex 16384

mkdir -p build && cp out/soc/firmware.hex build/firmware.hex
iverilog -g2012 -o out/soc/soc_sim -s tb_soc "$RV/sim/tb_soc.v" "$RV/rtl/soc_top.v" \
    "$RV/rtl/mac_accel.v" "$RV/third_party/picorv32/picorv32.v"
vvp -n out/soc/soc_sim
