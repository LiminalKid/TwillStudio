cmake_minimum_required(VERSION 3.20)
project(TwillStudio VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TWILL_HANDHELD "Build for a handheld: full-screen at the panel's resolution" OFF)

# Vendored macro-oscillator DSP (third_party/eurorack/README.md). TEST selects portable C over the
# module's 32-bit ARM assembly and disables its flash-based user data.
file(GLOB_RECURSE EURORACK_SOURCES CONFIGURE_DEPENDS
  third_party/eurorack/plaits/*.cc third_party/eurorack/stmlib/*.cc)
add_library(eurorack STATIC ${EURORACK_SOURCES})
target_include_directories(eurorack SYSTEM PUBLIC third_party/eurorack)
target_compile_definitions(eurorack PUBLIC TEST)
target_compile_options(eurorack PRIVATE -w)  # upstream code, built as is
if(EMSCRIPTEN)
  # user_data.h calls printf without including stdio.h; other compilers' headers happen to bring it in.
  target_compile_options(eurorack PRIVATE -include cstdio)
endif()

# Portable core: no OS or SDL headers in here (design §4).
add_library(twill_core STATIC
  src/engine/Effects.cpp
  src/engine/Engine.cpp
  src/engine/Sample.cpp
  src/engine/PlaitsVoice.cpp
  src/engine/Snapshot.cpp
  src/model/Edit.cpp
  src/model/Project.cpp
  src/model/ProjectFile.cpp
  src/model/Scale.cpp
  src/platform/Midi.cpp
  src/app/App.cpp
  src/app/MidiSender.cpp
  src/app/ClockFollower.cpp
  src/app/SongNames.cpp
  src/app/ArrangementView.cpp
  src/app/DemoSong.cpp
  src/app/FactorySounds.cpp
  src/app/InstrumentView.cpp
  src/app/MixerView.cpp
  src/app/PianoRollView.cpp
  src/app/InputTracker.cpp
  src/app/PianoKeys.cpp
  src/app/Paper.cpp
  src/app/Splash.cpp
  src/app/gen/logo.cpp
  src/gfx/Canvas.cpp
  src/gfx/Font.cpp
  src/gfx/gen/fonts.cpp
  ${CMAKE_BINARY_DIR}/gen/factory_data.cpp
)
target_include_directories(twill_core PUBLIC src)
# The factory kit's samples (assets/factory/), built into the binary.
file(GLOB TWILL_FACTORY_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/factory/*.wav)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/gen/factory_data.cpp
  COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_SOURCE_DIR}/assets/factory -DOUT=${CMAKE_BINARY_DIR}/gen/factory_data.cpp
          -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${TWILL_FACTORY_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  COMMENT "Embedding the factory kit")
# Version.h (version, commit, build date) for the About panel, refreshed at every build.
set(TWILL_GEN_DIR "${CMAKE_BINARY_DIR}/gen")
add_custom_target(twill_version
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${TWILL_GEN_DIR}/Version.h
          -DVERSION=${PROJECT_VERSION} -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
  BYPRODUCTS ${TWILL_GEN_DIR}/Version.h)
add_dependencies(twill_core twill_version)
target_include_directories(twill_core PUBLIC ${TWILL_GEN_DIR})
target_include_directories(twill_core SYSTEM PRIVATE third_party/dr_libs)
find_package(Threads REQUIRED)
target_link_libraries(twill_core PUBLIC eurorack Threads::Threads)
target_compile_options(twill_core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

# SDL2 platform. On macOS, prefer a package manager's SDL2 (Homebrew) over any SDL2.framework
# installed in /Library/Frameworks. The web build uses Emscripten's own SDL2 port instead.
set(CMAKE_FIND_FRAMEWORK LAST)
if(EMSCRIPTEN)
  add_library(sdl2_emscripten INTERFACE)
  target_compile_options(sdl2_emscripten INTERFACE -sUSE_SDL=2)
  target_link_options(sdl2_emscripten INTERFACE -sUSE_SDL=2)
  # Emscripten's stack is 64 KB unless asked; the app is written for desktop stacks (8 MB), and
  # reading a file alone takes 64 KB of it. Overflowing it corrupts memory without a word.
  add_link_options(-sSTACK_SIZE=4MB)
  set(TWILL_SDL2_TARGET sdl2_emscripten)
else()
  find_package(SDL2 CONFIG QUIET)
  if(SDL2_FOUND)
    set(TWILL_SDL2_TARGET SDL2::SDL2)
  else()
    # Cross builds: headers and library come from the sysroot (see cmake/toolchains/).
    find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 REQUIRED)
    find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0 REQUIRED)
    add_library(sdl2_imported UNKNOWN IMPORTED)
    set_target_properties(sdl2_imported PROPERTIES
      IMPORTED_LOCATION "${SDL2_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
    set(TWILL_SDL2_TARGET sdl2_imported)
  endif()
endif()

add_executable(twill
  src/platform/sdl2/main.cpp
  src/platform/sdl2/Sdl2Platform.cpp
)
if(EMSCRIPTEN)
  # The web build (web/): a page with the app in a canvas, its files kept in the browser's
  # IndexedDB, no MIDI yet. See BUILDING.md.
  target_sources(twill PRIVATE src/platform/sdl2/SystemMidiNone.cpp src/platform/sdl2/OutputCompressorNone.cpp)
  set_target_properties(twill PROPERTIES SUFFIX ".html")
  # --profiling-funcs keeps function names, so a crash in someone's browser says where it was.
  target_link_options(twill PRIVATE --profiling-funcs -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -lidbfs.js
    --pre-js ${CMAKE_SOURCE_DIR}/web/pre.js --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html)
  set_property(TARGET twill APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_SOURCE_DIR}/web/pre.js ${CMAKE_SOURCE_DIR}/web/shell.html)
elseif(APPLE)
  target_sources(twill PRIVATE src/platform/sdl2/SystemMidiMac.cpp src/platform/sdl2/OutputCompressorNone.cpp)
  target_link_libraries(twill PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
else()
  target_sources(twill PRIVATE src/platform/sdl2/SystemMidiLinux.cpp src/platform/sdl2/OutputCompressorLinux.cpp)
endif()
target_link_libraries(twill PRIVATE twill_core ${TWILL_SDL2_TARGET})
target_compile_options(twill PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
if(NOT EMSCRIPTEN)
  # Drawing benchmark, no display needed (runs over SSH on a device).
  add_executable(twill_bench src/tools/bench.cpp)
  target_link_libraries(twill_bench PRIVATE twill_core)

  # Macro-oscillator lab: renders every engine to WAV and benchmarks them (milestone M1).
  add_executable(twill_macro_lab src/tools/macro_lab.cpp)
  target_link_libraries(twill_macro_lab PRIVATE twill_core)
endif()

# Headless tests (design §10). Run on the build machine with ctest, or copy to a device and run
# there with --golden-dir pointing at a copy of tests/golden.
add_executable(twill_tests tests/tests.cpp)
target_link_libraries(twill_tests PRIVATE twill_core)
target_compile_definitions(twill_tests PRIVATE TWILL_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
enable_testing()
add_test(NAME twill_tests COMMAND twill_tests)
if(EMSCRIPTEN)
  # Run with Node, on the real file system (the golden files): node build/web/twill_tests.js
  target_link_options(twill_tests PRIVATE -sNODERAWFS=1 -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1)
endif()

if(TWILL_HANDHELD)
  target_compile_definitions(twill PRIVATE TWILL_HANDHELD=1)
endif()
