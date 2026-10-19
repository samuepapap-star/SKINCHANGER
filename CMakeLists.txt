
cmake_minimum_required(VERSION 3.21)

project(VSkinPremium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
)

# JSON for Modern C++
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(imgui nlohmann_json)

# Main executable
add_executable(VSkinPremium WIN32
    src/main.cpp

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp

    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

# Include directories
target_include_directories(VSkinPremium PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${nlohmann_json_SOURCE_DIR}/include
)

# Libraries
target_link_libraries(VSkinPremium PRIVATE
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    winhttp
    windowscodecs
    ole32
)

# Windows compiler settings
if(MSVC)
    target_compile_options(VSkinPremium PRIVATE
        /W3
        /EHsc
        /utf-8
    )
endif()

set_target_properties(VSkinPremium PROPERTIES
    WIN32_EXECUTABLE TRUE
)
