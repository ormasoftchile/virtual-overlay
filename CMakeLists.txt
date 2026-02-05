cmake_minimum_required(VERSION 3.20)
project(virtual-overlay VERSION 1.0.0 LANGUAGES CXX)

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
    Magnification.lib
    d2d1.lib
    dwrite.lib
    dwmapi.lib
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

# Use our custom manifest
if(MSVC)
    target_sources(virtual-overlay PRIVATE res/app.manifest)
endif()

# Windows version targeting (Windows 10 1803+)
target_compile_definitions(virtual-overlay PRIVATE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)
