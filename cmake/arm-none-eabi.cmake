# RP2350 Cortex-M33 のツールチェーン（サイズ測定と SGX 版）。
# 【2026-11-06】FPU あり（+fp+dsp）にした。sgnano の SDK・libc（v8-m.main+fp/softfp）と浮動小数点の印をそろえないと、
# リンカが印を合わせる所の assertion（elf32-arm.c、Tag_ABI_HardFP_use）で止まった
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp")
