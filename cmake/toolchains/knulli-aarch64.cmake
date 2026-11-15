# Cross-compiling for KNULLI handhelds (TrimUI Brick Pro and other AArch64 Linux devices) with Zig
# as the compiler. Zig brings its own glibc stubs for the chosen version and links libc++
# statically, so the binary needs nothing from the device except glibc and SDL2. The glibc target
# (tools/zig/) is 2.29, PortMaster's floor for handheld firmware. The CPU target is the Cortex-A53:
# it is plain ARMv8.0 (no LSE atomics, CRC, crypto or dot product appear in the binary), so it runs
# on every 64-bit handheld, and on the Brick it is about 4% faster than generic ARMv8-A (2026-11-15).
#
# The device's SDL2 we link against was built for a newer glibc (KNULLI 20260921 has 2.40). Its own
# glibc references are resolved on the device, against the glibc it was built with, so the linker
# is told not to check them (--allow-shlib-undefined); only this binary's references count.
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

# -s: no debug info or symbol table in the device binaries (about 16 MB down to a fraction; nothing
# in the app looks its own symbols up).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined -s")

set(TWILL_HANDHELD ON CACHE BOOL "" FORCE)
