# Cross-compiling for sndstation handhelds (Debian 13 on RK3326, RK3566, H700 and A133P units) with
# Zig, as the KNULLI build does (knulli-aarch64.cmake has the reasons: glibc stubs, libc++ linked
# statically, the Cortex-A53 target, --allow-shlib-undefined).
#
# The sysroot holds the device's SDL2, SDL2_ttf, SDL2_image and alsa-lib with their Debian headers
# and pkg-config files; tools/make-sndstation-sysroot.sh builds it. Override its location with
# -DTWILL_SYSROOT=... or the TWILL_SYSROOT environment variable. The sndstation SDK's source is
# found with -DSNDSTATION_DIR=... (default ~/SDKs/sndstation).

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)  # Debian's multiarch folders

get_filename_component(_fl_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_C_COMPILER "${_fl_root}/tools/zig/zig-cc")
set(CMAKE_CXX_COMPILER "${_fl_root}/tools/zig/zig-c++")
set(CMAKE_AR "${_fl_root}/tools/zig/zig-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_fl_root}/tools/zig/zig-ranlib" CACHE FILEPATH "")

if(NOT TWILL_SYSROOT)
  if(DEFINED ENV{TWILL_SYSROOT})
    set(TWILL_SYSROOT "$ENV{TWILL_SYSROOT}")
  else()
    set(TWILL_SYSROOT "$ENV{HOME}/SDKs/sndstation-aarch64-sysroot")
  endif()
endif()
set(CMAKE_FIND_ROOT_PATH "${TWILL_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config reads only the sysroot's files, and gives paths inside it.
set(ENV{PKG_CONFIG_LIBDIR} "${TWILL_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${TWILL_SYSROOT}")
set(ENV{PKG_CONFIG_PATH} "")

# Debian keeps SDL's configuration header in the multiarch include folder.
set(CMAKE_C_FLAGS_INIT "-isystem ${TWILL_SYSROOT}/usr/include/aarch64-linux-gnu")
set(CMAKE_CXX_FLAGS_INIT "-isystem ${TWILL_SYSROOT}/usr/include/aarch64-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined -s")

set(TWILL_HANDHELD ON CACHE BOOL "" FORCE)
set(TWILL_SNDSTATION ON CACHE BOOL "" FORCE)
