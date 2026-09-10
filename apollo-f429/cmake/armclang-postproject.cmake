# Post-project() fixups for the armclang toolchain.
#
# During project() CMake's Compiler/ARMClang.cmake module:
#   * sets CMAKE_EXECUTABLE_SUFFIX ".elf" (we already name the target *.elf), and
#   * replaces CMAKE_C_LINK_EXECUTABLE with an armlink-style rule that appends
#     "-Xlinker --list=<TARGET_BASE>.map" (GNU ld has no --list option).
# We link with the GNU gcc driver + our .ld script, so restore the GNU-style
# link rule and clear the suffix. Include this file after project().
#
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)

set(CMAKE_EXECUTABLE_SUFFIX "")

# CMakeFindBinUtils (run during project()) locates `armlink.exe` next to
# armclang and caches it as CMAKE_LINKER - the toolchain's GNU driver set
# before project() gets clobbered. Force the GNU gcc driver back so the
# restored link rule below actually links with GNU ld + our .ld script.
set(CMAKE_LINKER "${ARM_GCC_ROOT}/bin/arm-none-eabi-gcc.exe" CACHE FILEPATH
    "GNU arm-none-eabi gcc driver (linker)" FORCE)

# The ARMClang module, seeing armlink as the linker, appends
# "--cpu=<processor>" to the compile flags AND to CMAKE_C_LINK_FLAGS (both
# end up on the GNU gcc/ld link line, which rejects "--cpu="). Strip it.
string(REGEX REPLACE "(^| )--cpu=[^ ]*" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
string(REGEX REPLACE "(^| )--cpu=[^ ]*" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
string(REGEX REPLACE "(^| )--cpu=[^ ]*" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
string(REGEX REPLACE "(^| )--cpu=[^ ]*" "" CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS}")
string(REGEX REPLACE "(^| )--cpu=[^ ]*" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

# <FLAGS> is the C compile flags (contains --target=, -include, ...) which the
# GNU driver must not see when linking; the cpu/arch/opt flags live in
# LINK_FLAGS (set by stm32f407_board.cmake).
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_LINKER> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
