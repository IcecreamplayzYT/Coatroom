cmake_minimum_required(VERSION 3.20)
project(Coatroom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
)

set(APP_SOURCES
    src/mesh/obj_mesh.cpp
    src/mesh/fbx_loader.cpp
    src/paint/canvas_engine.cpp
    src/render/d3d11_renderer.cpp
    src/ui/studio_theme.cpp
    src/ui/uv_viewport.cpp
    src/ui/panels.cpp
    src/app/automation_mcp.cpp
    src/app/ota_updater.cpp
    src/app/app_main.cpp
    resources/coatroom.rc
)

add_executable(Coatroom
    ${IMGUI_SOURCES}
    ${APP_SOURCES}
)

target_include_directories(Coatroom PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
)

target_compile_definitions(Coatroom PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

if(WIN32)
    if(MINGW)
        set(LPS_D3DCOMPILER_LIB d3dcompiler_47)
    else()
        set(LPS_D3DCOMPILER_LIB d3dcompiler)
    endif()
    target_link_libraries(Coatroom PRIVATE
        d3d11
        dxgi
        ${LPS_D3DCOMPILER_LIB}
        comdlg32
        dwmapi
        gdi32
        ole32
        uuid
        user32
        shell32
        winhttp
        bcrypt
    )
endif()
