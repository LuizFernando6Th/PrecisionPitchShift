# PrecisionPitchShift — reproducible CMake build.
#
# Targets:
#   PrecisionPitchShift : VST3 plugin (needs the Steinberg VST3 SDK)
#   pps_dsp_tests       : offline DSP unit/quality tests (no SDK needed)
#   pps_render          : offline WAV render CLI (no SDK needed, for tests)
#
# VST3 SDK resolution (in order):
#   1. -DPPS_VST3_SDK_DIR=<local SDK dir>   (recommended, works offline)
#   2. FetchContent from GitHub (needs network)
#      - override tag with -DPPS_VST3_SDK_TAG=<tag>
#
# Examples:
#   cmake -B build -S . -DPPS_VST3_SDK_DIR="C:/SDKs/vst3sdk"
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.22)

project(PrecisionPitchShift VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PPS_BUILD_TESTS "Build DSP tests (no VST3 SDK required)" ON)
option(PPS_BUILD_CLI "Build offline WAV render CLI (no VST3 SDK required)" ON)
option(PPS_BUILD_VST3 "Build the VST3 plugin (requires VST3 SDK)" ON)
option(PPS_WITH_VSTGUI "Build custom VSTGUI editor (else generic host UI)" OFF)

set(PPS_VST3_SDK_DIR "" CACHE PATH "Local VST3 SDK directory (takes precedence)")
set(PPS_VST3_SDK_TAG "v3.7.14_build_55" CACHE STRING "VST3 SDK git tag when fetching")

# --- DSP static library (SDK-independent) --------------------------
set(PPS_DSP_SOURCES
    src/dsp/fft.cpp
    src/dsp/phase_processor.cpp
    src/dsp/spectral_processor.cpp
    src/dsp/alias_protection.cpp
    src/dsp/pitch_engine.cpp
    src/dsp/analysis.cpp
)
add_library(pps_dsp STATIC ${PPS_DSP_SOURCES})
target_include_directories(pps_dsp PUBLIC src)
if(MSVC)
    target_compile_options(pps_dsp PRIVATE /W4 /O2 /fp:precise)
else()
    target_compile_options(pps_dsp PRIVATE -Wall -Wextra -O2)
endif()

# Minimal WAV I/O used by the CLI (also SDK-independent).
add_library(pps_wavio STATIC tools/cpp/wav_io.cpp)
target_include_directories(pps_wavio PUBLIC tools/cpp)
if(MSVC)
    target_compile_options(pps_wavio PRIVATE /W4 /O2 /fp:precise)
else()
    target_compile_options(pps_wavio PRIVATE -Wall -Wextra -O2)
endif()

if(PPS_BUILD_TESTS)
    enable_testing()
    add_executable(pps_dsp_tests tests/cpp/dsp_tests.cpp)
    target_link_libraries(pps_dsp_tests PRIVATE pps_dsp)
    if(MSVC)
        target_compile_options(pps_dsp_tests PRIVATE /W4 /fp:precise)
    else()
        target_compile_options(pps_dsp_tests PRIVATE -Wall -Wextra -O2)
    endif()
    add_test(NAME dsp_tests COMMAND pps_dsp_tests)
endif()

if(PPS_BUILD_CLI)
    add_executable(pps_render tools/cpp/pps_render.cpp)
    target_link_libraries(pps_render PRIVATE pps_dsp pps_wavio)
    if(MSVC)
        target_compile_options(pps_render PRIVATE /W4 /O2 /fp:precise)
    else()
        target_compile_options(pps_render PRIVATE -Wall -Wextra -O2)
    endif()
endif()

# --- VST3 plugin ----------------------------------------------------
if(PPS_BUILD_VST3)
    # Skip the whole VST3 section cleanly when only DSP/CLI is wanted:
    # configure with -DPPS_BUILD_VST3=OFF.
    if(PPS_VST3_SDK_DIR)
        if(NOT EXISTS "${PPS_VST3_SDK_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "PPS_VST3_SDK_DIR is set but has no CMakeLists.txt: ${PPS_VST3_SDK_DIR}")
        endif()
        if(NOT PPS_WITH_VSTGUI AND NOT DEFINED SMTG_ENABLE_VSTGUI_SUPPORT)
            # vstgui lives in a separate submodule; skip it (generic host UI).
            set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
        endif()
        add_subdirectory("${PPS_VST3_SDK_DIR}" vst3sdk EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(vst3sdk
            GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
            GIT_TAG ${PPS_VST3_SDK_TAG}
            GIT_SHALLOW TRUE
            GIT_SUBMODULES_RECURSE TRUE
        )
        # Keep fetched-SDK builds lean.
        set(SMTG_ENABLE_VST3_PLUGIN_COPY_COMMAND OFF CACHE BOOL "" FORCE)
        set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
        if(NOT PPS_WITH_VSTGUI)
            # vstgui lives in a separate submodule; skip it (generic host UI).
            set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
        endif()
        FetchContent_MakeAvailable(vst3sdk)
    endif()

    smtg_enable_vst3_sdk()

    smtg_add_vst3plugin(PrecisionPitchShift
        src/plugin/factory.cpp
        src/plugin/processor.cpp
        src/plugin/controller.cpp
        src/gui/editor.cpp
    )
    target_include_directories(PrecisionPitchShift PRIVATE src)
    target_link_libraries(PrecisionPitchShift PRIVATE pps_dsp sdk)

    smtg_target_configure_version_file(PrecisionPitchShift)

    if(PPS_WITH_VSTGUI)
        if(TARGET vstgui_support)
            target_link_libraries(PrecisionPitchShift PRIVATE vstgui_support)
            target_compile_definitions(PrecisionPitchShift PRIVATE PPS_HAS_VSTGUI=1)
        else()
            message(WARNING "PPS_WITH_VSTGUI=ON but target vstgui_support not found; building without custom editor.")
        endif()
    endif()

    # NOTE: the VST3 bundle is left in the build tree (<target>.vst3 directory).
    # Copy/install it manually, e.g. to C:\Program Files\Common Files\VST3
    # (see README § Instalação no Audacity). No automatic copy step, to keep
    # the build reproducible on machines without admin rights.
endif()
