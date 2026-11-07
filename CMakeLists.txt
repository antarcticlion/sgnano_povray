cmake_minimum_required(VERSION 3.16)
project(sgnano_povray LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(POVRAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/povray")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/povray_sources.cmake")

set(SGNANO_PNG_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/app/sgnano_png.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz_tdef.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz_tinfl.c"
)

add_executable(povray_host
    ${POVRAY_SOURCES}
    ${VFE_SOURCES}
    ${SGNANO_PNG_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/app/sgnano_app.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/host/sgnano_app_host.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/host/sgnano_display.cpp"
)

target_include_directories(povray_host PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sgnano"
    "${POVRAY_DIR}/source"
    "${POVRAY_DIR}/source/backend"
    "${POVRAY_DIR}/source/base"
    "${POVRAY_DIR}/source/frontend"
    "${POVRAY_DIR}/vfe"
    "${POVRAY_DIR}/vfe/unix"
    "${POVRAY_DIR}/unix"
)

target_compile_definitions(povray_host PRIVATE
    HAVE_CONFIG_H=1
    POVLIBDIR="${POVRAY_DIR}"
    POVCONFDIR="${CMAKE_CURRENT_SOURCE_DIR}/platform/host/conf"
    POVCONFDIR_BACKWARD="${CMAKE_CURRENT_SOURCE_DIR}/platform/host/conf"
)

target_compile_options(povray_host PRIVATE
    -O2
    -fno-strict-aliasing
    -Wno-deprecated-declarations
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-sign-compare
    -Wno-write-strings
)

target_link_libraries(povray_host PRIVATE m pthread png jpeg z)

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(test_sgx_checkpoint
        tests/test_sgx_checkpoint.c
        app/sgnano_app.c
        app/sgx/sgnano_app_sgx.c
        platform/sgx/sgx4_stub_host.c
        ${SGNANO_PNG_SOURCES}
    )
    target_include_directories(test_sgx_checkpoint PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/sgx"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sgnano"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
    )
    target_link_libraries(test_sgx_checkpoint PRIVATE m)

    # チェックポイントの誤りの試し（2026-11-07）: 書き込み・flush・PNG をわざと失敗させる
    add_executable(test_sgx_ckp_faults
        tests/test_sgx_ckp_faults.c
        app/sgnano_app.c
        app/sgx/sgnano_app_sgx.c
        platform/sgx/sgx4_stub_host.c
        ${SGNANO_PNG_SOURCES}
    )
    target_include_directories(test_sgx_ckp_faults PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/sgx"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sgnano"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
    )
    target_link_libraries(test_sgx_ckp_faults PRIVATE m)
endif()

# 実機 SGX 用の小さな試し（apps/povprobe）。sgnano の SDK の sgx.cmake を使う。
#   cmake -S . -B build_sgx -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#     -DSGNANO_BUILD_SGX_PROBE=ON
option(SGNANO_BUILD_SGX_PROBE "Build povprobe.sgx for a real sgnano device" OFF)
if(SGNANO_BUILD_SGX_PROBE)
    set(SGNANO_SGX_SDK_DIR "/home/antarcticlion/Project/sgnano/sdk/sgx" CACHE PATH "sgnano SGX SDK directory")
    if(NOT EXISTS "${SGNANO_SGX_SDK_DIR}/sgx.cmake")
        message(FATAL_ERROR "SGNANO_SGX_SDK_DIR does not contain sgx.cmake: ${SGNANO_SGX_SDK_DIR}")
    endif()
    include("${SGNANO_SGX_SDK_DIR}/sgx.cmake")
    sgx_add_program(povprobe NAME "POV-Ray probe" STACK 65536
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/povprobe/povprobe.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/app/sgnano_app.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/app/sgx/sgnano_app_sgx.c"
            ${SGNANO_PNG_SOURCES}
        INCLUDES
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/sgx"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
        DEFINES
            SGNANO_SGX_OFFICIAL_ABI=1
            MINIZ_NO_ARCHIVE_APIS=1
            MINIZ_NO_TIME=1
            MINIZ_NO_STDIO=1)
    # 【2026-11-06】-fno-builtin は外した。sgnano の SDK（ABI 4.1）の sgx_libc.c が、calloc の中の memset を
    # 関数ポインタ越しに呼ぶようにして、GCC が calloc 自身へ置き換える無限再帰を直したため
endif()

# POV-Ray 3.7 の sgnano SGX 版（2026-11-06、app/sgx/povray_sgx.cpp）。できるもの: build_sgx/sgx/povray.sgx（S:/bin に置く）
# スレッドは compat_sgx/boost/thread.hpp（M33BEATOS のタスク）、VFE のプラットフォームは platform/sgx_pov
option(SGNANO_BUILD_SGX_POVRAY "Build povray.sgx for a real sgnano device" OFF)
if(SGNANO_BUILD_SGX_POVRAY)
    set(SGNANO_SGX_SDK_DIR "/home/antarcticlion/Project/sgnano/sdk/sgx" CACHE PATH "sgnano SGX SDK directory")
    include("${SGNANO_SGX_SDK_DIR}/sgx.cmake")
    set(_vfe_sgx
        ${POVRAY_DIR}/vfe/vfesession.cpp
        ${POVRAY_DIR}/vfe/vfepovms.cpp
        ${POVRAY_DIR}/vfe/vfe.cpp
        ${POVRAY_DIR}/vfe/vfecontrol.cpp
        ${POVRAY_DIR}/vfe/vfedisplay.cpp)
    # FLAGS 1 = SGX_F_ESC_KEY: ESC はプログラムへ（自分で止めて、描けた分を救い出し .partial.png を残す）。強く止めるのは Shift+ESC（2026-11-07）
    sgx_add_program(povray NAME "POV-Ray 3.7" FLAGS 1 STACK 131072
        SOURCES
            # 先頭は FPU の印（Tag_FP_arch）を持つものにする。浮動小数点を使わない optout.cpp が先頭だと、FPU の種類なし・
            # ハードの浮動小数点ありの食い違った印になり、binutils が印を合わせる所の assertion（elf32-arm.c）で止まった
            "${CMAKE_CURRENT_SOURCE_DIR}/app/sgx/povray_sgx.cpp"
            ${POVRAY_CORE_SOURCES}
            ${_vfe_sgx}
            "${CMAKE_CURRENT_SOURCE_DIR}/app/sgx/zlib_shim.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/app/sgnano_app.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/app/sgx/sgnano_app_sgx.c"
            ${SGNANO_PNG_SOURCES}
        INCLUDES
            "${CMAKE_CURRENT_SOURCE_DIR}/compat_sgx"
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/sgx_pov"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/sgx"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
            "${POVRAY_DIR}/source"
            "${POVRAY_DIR}/source/backend"
            "${POVRAY_DIR}/source/base"
            "${POVRAY_DIR}/source/frontend"
            "${POVRAY_DIR}/vfe"
        DEFINES
            SGNANO_SGX_OFFICIAL_ABI=1
            HAVE_CONFIG_H=1
            MINIZ_NO_ARCHIVE_APIS=1
            MINIZ_NO_TIME=1
            MINIZ_NO_STDIO=1
        OPTIONS -Os -Wno-deprecated-declarations -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-write-strings>)
    sgx_link_cxx(povray)
    # 字句の解析が数を sscanf("%lf") で読む。newlib-nano の scanf は -u _scanf_float が無いと小数を読まない（assumed_gamma 1.0 が 0 になった。2026-11-06）
    target_link_options(povray PRIVATE -Wl,-u,_scanf_float)
    # compat_sgx/boost/thread.hpp だけの試し（2026-11-06）
    sgx_add_program(threadtest NAME "thread test" STACK 32768
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/apps/threadtest/threadtest.cpp"
        INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/compat_sgx" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost")
    sgx_link_cxx(threadtest)
    sgx_add_program(exctest NAME "exception test" STACK 32768
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/apps/exctest/exctest.cpp")
    sgx_link_cxx(exctest)
    # binutils 2.38 は -q（再配置を残す）と例外の表のまとめが重なると assertion で止まる（elf32-arm.c:14807）
    target_link_options(povray PRIVATE -Wl,--no-merge-exidx-entries)
endif()

# ARM (RP2350) のサイズ測定用。リンクはせず、オブジェクトの合計サイズを見る。
option(SGNANO_BUILD_ARM_SIZE "Compile POV-Ray core for ARM size measurement" OFF)
if(SGNANO_BUILD_ARM_SIZE)
    add_library(povray_arm_size OBJECT ${POVRAY_CORE_SOURCES})
    target_include_directories(povray_arm_size PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/compat_arm"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/arm"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost"
        "${POVRAY_DIR}/source"
        "${POVRAY_DIR}/source/backend"
        "${POVRAY_DIR}/source/base"
        "${POVRAY_DIR}/source/frontend"
        "${POVRAY_DIR}/vfe"
        "${POVRAY_DIR}/vfe/unix"
        "${POVRAY_DIR}/unix"
    )
    target_compile_definitions(povray_arm_size PRIVATE HAVE_CONFIG_H=1)
    target_compile_options(povray_arm_size PRIVATE
        -Os -ffunction-sections -fdata-sections
        -Wno-deprecated-declarations -Wno-unused-parameter
        -Wno-unused-variable -Wno-sign-compare -Wno-write-strings
    )

    # アプリ共通層＋SGX スタブのコンパイル確認（リンクはしない）。
    add_library(sgnano_arm_app OBJECT
        "${CMAKE_CURRENT_SOURCE_DIR}/app/sgnano_app.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/sgx/sgnano_app_sgx.c"
        ${SGNANO_PNG_SOURCES}
    )
    target_include_directories(sgnano_arm_app PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/sgx"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sgnano"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
    )
    target_compile_options(sgnano_arm_app PRIVATE -Os -ffunction-sections -fdata-sections)
endif()
