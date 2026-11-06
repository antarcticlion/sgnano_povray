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
            MINIZ_NO_STDIO=1
        # GCC が sgx_libc.c の「malloc + memset(0)」を calloc への呼び出しに
        # 置き換え、calloc 自身の定義の中で無限再帰になるのを防ぐ。
        # -fno-builtin-calloc だけでは止まらない（malloc 側の認識なので全体を切る）。
        OPTIONS -fno-builtin)
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
