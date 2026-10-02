cmake_minimum_required(VERSION 3.15)
project(Quicky VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the executable subsystem to Windows (so no console window appears)
add_executable(Quicky WIN32
    src/main.cpp
    src/Logger.cpp
    src/Quicky.rc
)

# Link against standard Windows libraries
target_link_libraries(Quicky PRIVATE
    user32
    shell32
    gdi32
    comctl32
    ole32
    oleaut32
    dwmapi
    shlwapi
)

# Enable modern common controls and Unicode
target_compile_definitions(Quicky PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
