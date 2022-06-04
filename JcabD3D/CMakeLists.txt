cmake_minimum_required (VERSION 3.20)

find_program(DXC_COMPILER_TOOL dxc)

set(SHADERS_DIR         "${CMAKE_BINARY_DIR}/Shaders"  CACHE INTERNAL "")
set(SHADERS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/inc" CACHE INTERNAL "")

function(add_hlsl_shader TargetName HlslFile ShaderModel FunctionName ShaderName)

    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(DxcOptimization -Od)
    else()
        set(DxcOptimization -O3)
    endif()


    add_custom_command(
        COMMAND
            "${DXC_COMPILER_TOOL}" -nologo
            "${CMAKE_CURRENT_SOURCE_DIR}/${HlslFile}"
            -Fh "${SHADERS_DIR}/${ShaderName}.h"
            -T ${ShaderModel}
            -E ${FunctionName}
            -Vn ${ShaderName}
            -Zi -Zss -Qstrip_debug -Fd "${SHADERS_DIR}\\"
            "-I${SHADERS_INCLUDE_DIR}"
            -DHLSL=1
            ${DxcOptimization}
            # Someday: -HV 2021
        MAIN_DEPENDENCY
            "${CMAKE_CURRENT_SOURCE_DIR}/${HlslFile}"
        DEPENDS
            "${SHADERS_INCLUDE_DIR}/HLSL.h"
            ${ARGN}
        OUTPUT
            "${SHADERS_DIR}/${ShaderName}.h"
    )

    message("${TargetName} ${SHADERS_DIR}/${ShaderName}.h")

    target_sources(${TargetName} PRIVATE "${SHADERS_DIR}/${ShaderName}.h")

    target_include_directories(${TargetName} PRIVATE "${SHADERS_DIR}")

endfunction()

add_library (JcabD3D STATIC

	JcabD3D.cpp         inc/JcabD3D.h

    inc/HLSL.h
)

add_hlsl_shader(JcabD3D JcabD3D.hlsl "cs_6_0" MipGenerationPassSRGB MipGenerationPassShaderSRGB)
add_hlsl_shader(JcabD3D JcabD3D.hlsl "cs_6_0" MipGenerationPass     MipGenerationPassShader)

target_include_directories(JcabD3D PUBLIC inc)

target_link_libraries(JcabD3D PUBLIC d3d12.lib dxgi.lib d3dcompiler.lib gdiplus.lib)

set_property(TARGET JcabD3D PROPERTY CXX_STANDARD 20)

# TODO: Add tests and install targets if needed.
