cmake_minimum_required(VERSION 3.20)
project(virtual-overlay VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC CRT; Vosk ships its own runtime DLLs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(vosk_runtime
    URL https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-win64-0.3.45.zip
    URL_HASH SHA256=f1dcc9cca460630f81ea8f71794f69c80bed6556d2a4e6237b5785e1d2dff34b
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/downloads"
    TLS_VERIFY TRUE
)
FetchContent_MakeAvailable(vosk_runtime)

set(VOSK_RUNTIME_FILES
    "${vosk_runtime_SOURCE_DIR}/libvosk.dll"
    "${vosk_runtime_SOURCE_DIR}/libgcc_s_seh-1.dll"
    "${vosk_runtime_SOURCE_DIR}/libstdc++-6.dll"
    "${vosk_runtime_SOURCE_DIR}/libwinpthread-1.dll"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.vosk.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.gcc.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.gcc-runtime.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.winpthreads.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.kaldi.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.openfst.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.openblas.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.gotoblas.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.clapack.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.f2c.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/LICENSE.mingw-runtime.txt"
    "${CMAKE_SOURCE_DIR}/lib/vosk/SOURCES.vosk.txt"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
)

# Collect source files
file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
)

# Separate main.cpp so test executable can link core sources
set(CORE_SOURCES ${SOURCES})
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

# Win32 GUI application
add_executable(virtual-overlay WIN32
    ${SOURCES}
    res/app.rc
)

set(COMMON_LIBS
    d2d1.lib
    dwrite.lib
    dwmapi.lib
    d3d11.lib
    dxgi.lib
    windowsapp.lib
    magnification.lib
    windowscodecs.lib
    ole32.lib
    oleaut32.lib
    shell32.lib
    user32.lib
    gdi32.lib
    comctl32.lib
    comdlg32.lib
    shlwapi.lib
    runtimeobject.lib
)

# Link Windows SDK libraries
target_link_libraries(virtual-overlay PRIVATE ${COMMON_LIBS})

# Compile definitions
target_compile_definitions(virtual-overlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

# Include directories
target_include_directories(virtual-overlay PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)

# Set output name
set_target_properties(virtual-overlay PROPERTIES
    OUTPUT_NAME "virtual-overlay"
    VS_DPI_AWARE "PerMonitor"
)

# Use our custom manifest. VO_UIACCESS=ON marks the EXE uiAccess="true" so the
# teleprompter mirror can cover the Start menu; such an EXE only launches when
# signed and installed under Program Files (build.ps1 enables it for signed builds).
option(VO_UIACCESS "Build with uiAccess=true (requires signed EXE in Program Files)" OFF)
if(VO_UIACCESS)
    set(VO_UIACCESS_VALUE "true")
else()
    set(VO_UIACCESS_VALUE "false")
endif()
configure_file(res/app.manifest ${CMAKE_BINARY_DIR}/app.manifest @ONLY)
if(MSVC)
    target_sources(virtual-overlay PRIVATE ${CMAKE_BINARY_DIR}/app.manifest)
    # Our manifest carries the UAC settings; the linker's default snippet would
    # conflict when uiAccess is true.
    target_link_options(virtual-overlay PRIVATE /MANIFESTUAC:NO)
endif()

# Deterministic unit & integration test runner
add_executable(virtual-overlay-tests
    tests/PresenterTests.cpp
    ${CORE_SOURCES}
)

if(MSVC)
    # Match the app's visual styles without requiring a signed test executable.
    set(VO_UIACCESS_VALUE "false")
    configure_file(res/app.manifest ${CMAKE_BINARY_DIR}/tests.manifest @ONLY)
    target_sources(virtual-overlay-tests PRIVATE ${CMAKE_BINARY_DIR}/tests.manifest)
    target_link_options(virtual-overlay-tests PRIVATE /MANIFESTUAC:NO)
endif()

target_link_libraries(virtual-overlay-tests PRIVATE ${COMMON_LIBS})

target_compile_definitions(virtual-overlay-tests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

target_include_directories(virtual-overlay-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)

# Both executables share the configuration's output directory.
add_custom_target(vosk-runtime
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:virtual-overlay>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VOSK_RUNTIME_FILES} "$<TARGET_FILE_DIR:virtual-overlay>"
    VERBATIM
)
add_dependencies(virtual-overlay vosk-runtime)
add_dependencies(virtual-overlay-tests vosk-runtime)

add_custom_command(TARGET virtual-overlay-tests POST_BUILD
    COMMAND "$<TARGET_FILE:virtual-overlay-tests>" --vosk-runtime-only
    COMMENT "Checking bundled Vosk runtime"
    VERBATIM
)

enable_testing()
add_test(NAME vosk-runtime COMMAND virtual-overlay-tests --vosk-runtime-only)
add_test(NAME presenter COMMAND virtual-overlay-tests)
add_test(NAME settings COMMAND virtual-overlay-tests --settings-only)
add_test(NAME watermark COMMAND virtual-overlay-tests --watermark-only)
