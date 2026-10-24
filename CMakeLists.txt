cmake_minimum_required(VERSION 3.20)
project(virtual-overlay VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT - no runtime DLL dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Collect source files
file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
)

# Win32 GUI application
add_executable(virtual-overlay WIN32
    ${SOURCES}
    res/app.rc
)

# Link Windows SDK libraries
target_link_libraries(virtual-overlay PRIVATE
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
    shlwapi.lib
    runtimeobject.lib
)

# Compile definitions
target_compile_definitions(virtual-overlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
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

# Windows version targeting (Windows 10 1803+)
target_compile_definitions(virtual-overlay PRIVATE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)
