cmake_minimum_required(VERSION 3.25)
project(WackyEngine CXX)

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Paths
set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/vendor)

set(VULKAN_DIR ${VENDOR_DIR}/vulkan/1.4.341.1)
set(GLFW_DIR   ${VENDOR_DIR}/glfw/3.4)
set(IMGUI_DIR  ${VENDOR_DIR}/imgui)
set(SLANG_DIR  ${VENDOR_DIR}/slang/2026.3.1)
set(DXC_DIR    ${VENDOR_DIR}/dxc/1.9.2602.17)

# Compiler flags
function(apply_compiler_flags TARGET)
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET} PRIVATE /W4 /MP)

    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${TARGET} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
            -Wno-language-extension-token
        )

        if(WIN32)
            target_compile_options(${TARGET} PRIVATE
                -fms-extensions
                -fms-compatibility
            )
        endif()
    endif()
endfunction()

# ImGui sources
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Core library
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "src/core/*.cpp"
    "src/entities/*.cpp"
    "src/renderer/*.cpp"
)

list(FILTER CORE_SOURCES EXCLUDE REGEX "src/runtime/")

add_library(Core STATIC ${CORE_SOURCES})

target_include_directories(Core PUBLIC
    src/
    ${VENDOR_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_DIR}/include
    ${VULKAN_DIR}/Include
    ${SLANG_DIR}/include
    ${DXC_DIR}/inc
)

target_link_libraries(Core PUBLIC
    ${VULKAN_DIR}/Lib/vulkan-1.lib
    ${GLFW_DIR}/lib-vc2022/glfw3.lib
    ${SLANG_DIR}/lib/slang.lib
    ${DXC_DIR}/lib/x64/dxcompiler.lib

    dwmapi.lib
    user32.lib
    gdi32.lib
    shell32.lib
)

apply_compiler_flags(Core)

# WackyEngine (editor/app)
set(EDITOR_SOURCES
    src/main.cpp
)

file(GLOB_RECURSE EDITOR_IMPL CONFIGURE_DEPENDS
    "src/editor/*.cpp"
)

list(APPEND EDITOR_SOURCES ${EDITOR_IMPL})

foreach(src ${EDITOR_SOURCES})
    if(src MATCHES "src/runtime/")
        message(FATAL_ERROR "Runtime source leaked into WackyEngine: ${src}")
    endif()
endforeach()

add_executable(WackyEngine
    ${EDITOR_SOURCES}
    ${IMGUI_SOURCES}
)

target_link_libraries(WackyEngine PRIVATE Core)

apply_compiler_flags(WackyEngine)

# Runtime DLL dependencies
add_custom_command(TARGET WackyEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SLANG_DIR}/bin/slang.dll
        $<TARGET_FILE_DIR:WackyEngine>

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DXC_DIR}/dxcompiler.dll
        $<TARGET_FILE_DIR:WackyEngine>

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DXC_DIR}/dxil.dll
        $<TARGET_FILE_DIR:WackyEngine>
)

# WackyRuntime
file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
    "src/runtime/*.cpp"
)

add_executable(WackyRuntime EXCLUDE_FROM_ALL
    ${RUNTIME_SOURCES}
)

target_link_libraries(WackyRuntime PRIVATE Core)

apply_compiler_flags(WackyRuntime)

add_custom_command(TARGET WackyRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SLANG_DIR}/bin/slang.dll
        $<TARGET_FILE_DIR:WackyRuntime>

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DXC_DIR}/bin/x64/dxcompiler.dll
        $<TARGET_FILE_DIR:WackyRuntime>

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DXC_DIR}/bin/x64/dxil.dll
        $<TARGET_FILE_DIR:WackyRuntime>
)

# Shader compilation
find_program(SLANGC slangc HINTS ${SLANG_DIR}/bin)

if(NOT SLANGC)
    message(FATAL_ERROR "slangc not found at ${SLANG_DIR}/bin")
endif()

set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SLANG_SHADER ${CMAKE_SOURCE_DIR}/shaders/shader.slang)

# Vertex shader
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/vert.spv
    COMMAND ${SLANGC} ${SLANG_SHADER}
        -target spirv
        -stage vertex
        -entry VSMain
        -o ${SHADER_OUTPUT_DIR}/vert.spv
    DEPENDS ${SLANG_SHADER}
    VERBATIM
)

# Fragment shader
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/frag.spv
    COMMAND ${SLANGC} ${SLANG_SHADER}
        -target spirv
        -stage fragment
        -entry PSMain
        -o ${SHADER_OUTPUT_DIR}/frag.spv
    DEPENDS ${SLANG_SHADER}
    VERBATIM
)

add_custom_target(Shaders
    DEPENDS
        ${SHADER_OUTPUT_DIR}/vert.spv
        ${SHADER_OUTPUT_DIR}/frag.spv
)

add_dependencies(WackyEngine Shaders)