/* sgnano_povray: ARM (RP2350 Cortex-M33) のサイズ測定用 config.h。
 * 実際のターゲットでは sgx_api 経由のファイル・メモリに置き換える。
 */
#ifndef POVRAY_ARM_CONFIG_H
#define POVRAY_ARM_CONFIG_H

#define BUILD_ARCH              "arm-none-eabi"
#define BUILT_FOR               "RP2350 Cortex-M33 (sgnano SGX)"
#define COMPILER_VENDOR         "GNU"
#define COMPILER_VERSION        "arm-none-eabi-g++"
#define COMPILER_VER            ".arm"
#define CXXFLAGS                "-Os"
#define DISTRIBUTION_MESSAGE_2  "sgnano_povray ARM size build"

#define HAVE_BOOST              1
#define USE_OFFICIAL_BOOST      1
#define HAVE_CLOCK_GETTIME      1
#define HAVE_GETTIMEOFDAY       1
#define HAVE_INTTYPES_H         1
#define HAVE_LIMITS_H           1
#define HAVE_NANOSLEEP          1
#define HAVE_STDINT_H           1
#define HAVE_STDIO_H            1
#define HAVE_STDLIB_H           1
#define HAVE_STRING_H           1
#define HAVE_STRINGS_H          1
#define HAVE_SYS_STAT_H         1
#define HAVE_SYS_TIME_H         1
#define HAVE_SYS_TYPES_H        1
#define HAVE_TIME_H             1
#define HAVE_UNISTD_H           1

#define PACKAGE                 "povray"
#define PACKAGE_NAME            "POV-Ray"
#define PACKAGE_VERSION         "3.7.0.10"
#define PACKAGE_STRING          "POV-Ray 3.7.0.10"
#define PACKAGE_BUGREPORT       "sgnano_povray"
#define PACKAGE_TARNAME         "povray"
#define PACKAGE_URL             ""
#define VERSION                 "3.7.0.10"
#define VERSION_BASE            "3.7"

#define SIZEOF_FLOAT            4
#define SIZEOF_INT              4
#define SIZEOF_LONG_INT         4
#define SIZEOF_SIZE_T           4
#define STDC_HEADERS            1
#define TIME_WITH_SYS_TIME      1

#define X_DISPLAY_MISSING       1
#define BUILTIN_XWIN_DISPLAY    "none"
#define BUILTIN_IO_RESTRICTIONS "none"
#define IO_RESTRICTIONS_DISABLED 1
#define BUILTIN_IMG_FORMATS     "PPM TGA BMP"
#define MISSING_IMG_FORMATS     "PNG JPEG TIFF OpenEXR"

#define LIBPNG_MISSING          1
#define LIBJPEG_MISSING         1
#define LIBTIFF_MISSING         1
#define LIBZ_MISSING            1
#define OPENEXR_MISSING         1

#endif /* POVRAY_ARM_CONFIG_H */
