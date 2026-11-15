# Cross-compiling for KNULLI handhelds (TrimUI Brick Pro and other AArch64 Linux devices) with Zig
# as the compiler. Zig brings its own glibc stubs for the chosen version and links libc++
# statically, so the binary needs nothing from the device except glibc and SDL2. The glibc target
# (tools/zig/, 2.38) is the oldest the device's own SDL2 links against; KNULLI 20260921 has 2.40.
#
# The sysroot only has to hold the device's SDL2: usr/lib/libSDL2-2.0.so.0 (copied from the
# device) and usr/include/SDL2 (headers from the same SDL release). tools/make-knulli-sysroot.sh
# builds it. Override its location with -DTWILL_SYSROOT=... or the TWILL_SYSROOT environment variable.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

get_filename_component(_fl_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_C_COMPILER "${_fl_root}/tools/zig/zig-cc")
set(CMAKE_CXX_COMPILER "${_fl_root}/tools/zig/zig-c++")
set(CMAKE_AR "${_fl_root}/tools/zig/zig-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_fl_root}/tools/zig/zig-ranlib" CACHE FILEPATH "")

if(NOT TWILL_SYSROOT)
  if(DEFINED ENV{TWILL_SYSROOT})
    set(TWILL_SYSROOT "$ENV{TWILL_SYSROOT}")
  else()
    set(TWILL_SYSROOT "$ENV{HOME}/SDKs/knulli-aarch64-sysroot")
  endif()
endif()
set(CMAKE_FIND_ROOT_PATH "${TWILL_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(TWILL_HANDHELD ON CACHE BOOL "" FORCE)
