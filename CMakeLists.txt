cmake_minimum_required(VERSION 3.18)

add_subdirectory(tools)

set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)

project(MY-RP6502-PROJECT C CXX ASM)

# Define the option (Default is ON/Native) 
option(USE_NATIVE_OPL2 "Use the RIA native OPL2 support" ON)

if(USE_NATIVE_OPL2)
    add_definitions(-DUSE_NATIVE_OPL2)
    message(STATUS "Targeting: Native RIA OPL2")
else()
    message(STATUS "Targeting: FPGA TinyFPGA Sound Card")
endif()

add_executable(RPTracker)
rp6502_asset(RPTracker help src/main.hlp)
rp6502_executable(RPTracker
    DATA file
    RESET file
)
target_sources(RPTracker PRIVATE
    src/main.c
    src/input.c
    src/instruments.c
    src/midi.c
    src/opl.c
    src/player.c
    src/screen.c
    src/song.c
    src/effects.c
)
