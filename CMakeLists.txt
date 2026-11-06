cmake_minimum_required(VERSION 3.16)
project(sgnano_povray LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(POVRAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/povray")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/povray_sources.cmake")

add_executable(povray_host
    ${POVRAY_SOURCES}
    ${VFE_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/app/sgnano_app.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/host/sgnano_app_host.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/host/sgnano_display.cpp"
)

target_include_directories(povray_host PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost"
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
