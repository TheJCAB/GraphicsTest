# CMakeList.txt : CMake project for GraphicsTest, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.20)

find_program(DXC_COMPILER_TOOL dxc)

set(SHADERS_DIR "${CMAKE_BINARY_DIR}/Shaders")

function(add_hlsl_shader TargetName HlslFile ShaderModel FunctionName ShaderName)

    add_custom_command(
        COMMAND
            "${DXC_COMPILER_TOOL}" -nologo
            "${CMAKE_CURRENT_SOURCE_DIR}\\${HlslFile}"
            -Fh "${SHADERS_DIR}\\${ShaderName}.h"
            -T ${ShaderModel}
            -E ${FunctionName}
            -Vn ${ShaderName}
            -Zi -Zss -Qstrip_debug -Fd "${CMAKE_BINARY_DIR}\\"
            -DHLSL=1
            # Someday: -HV 2021
        MAIN_DEPENDENCY
            "${CMAKE_CURRENT_SOURCE_DIR}/${HlslFile}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/HLSL.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Model.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/ViewSpace.h"
            ${ARGN}
        OUTPUT
            "${SHADERS_DIR}/${ShaderName}.h"
    )

    target_sources(${TargetName} PRIVATE "${SHADERS_DIR}/${ShaderName}.h")

    target_include_directories(${TargetName} PRIVATE "${SHADERS_DIR}")

endfunction()


# Add source to this project's executable.
add_executable (GraphicsTest WIN32
	GraphicsTest.cpp

	JcabD3D.cpp         JcabD3D.h
	Model.cpp           Model.h
	ObjModel.cpp        ObjModel.h
	Pipeline.cpp        Pipeline.h
    Pipeline2.cpp       Pipeline2.h
    Pipeline3.cpp       Pipeline3.h

    HLSL.h
    IRenderPipeline.h
    ViewSpace.h
)

add_hlsl_shader(GraphicsTest Model.hlsl "vs_6_0" VisibilitySolidVS     Model_VisibilitySolidVSShader    )
add_hlsl_shader(GraphicsTest Model.hlsl "ps_6_0" VisibilitySolidPS     Model_VisibilitySolidPSShader    )
add_hlsl_shader(GraphicsTest Model.hlsl "vs_6_0" VisibilityAlphaTestVS Model_VisibilityAlphaTestVSShader)
add_hlsl_shader(GraphicsTest Model.hlsl "ps_6_0" VisibilityAlphaTestPS Model_VisibilityAlphaTestPSShader)

add_hlsl_shader(GraphicsTest Pipeline.hlsl "cs_6_0" MsaaGatherPass VisDefMsaaPipeline_MsaaGatherPassShader Pipeline.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "cs_6_0" ColorPass      VisDefMsaaPipeline_ColorPassShader      Pipeline.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "cs_6_0" Postprocess    VisDefMsaaPipeline_PostprocessShader    Pipeline.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "vs_6_0" ResolveVS      VisDefMsaaPipeline_ResolveVSShader      Pipeline.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "ps_6_0" ResolvePS      VisDefMsaaPipeline_ResolvePSShader      Pipeline.h)

add_hlsl_shader(GraphicsTest Pipeline2.hlsl "cs_6_0" ColorPass   VisDefPipeline_ColorPassShader   Pipeline2.h)
add_hlsl_shader(GraphicsTest Pipeline2.hlsl "cs_6_0" Postprocess VisDefPipeline_PostprocessShader Pipeline2.h)
add_hlsl_shader(GraphicsTest Pipeline2.hlsl "vs_6_0" ResolveVS   VisDefPipeline_ResolveVSShader   Pipeline2.h)
add_hlsl_shader(GraphicsTest Pipeline2.hlsl "ps_6_0" ResolvePS   VisDefPipeline_ResolvePSShader   Pipeline2.h)

add_hlsl_shader(GraphicsTest Pipeline3.hlsl "cs_6_0" MsaaGatherPass VisDefMsaa3Pipeline_MsaaGatherPassShader Pipeline3.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "cs_6_0" ColorPass      VisDefMsaa3Pipeline_ColorPassShader      Pipeline3.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "cs_6_0" Postprocess    VisDefMsaa3Pipeline_PostprocessShader    Pipeline3.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "vs_6_0" ResolveVS      VisDefMsaa3Pipeline_ResolveVSShader      Pipeline3.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "ps_6_0" ResolvePS      VisDefMsaa3Pipeline_ResolvePSShader      Pipeline3.h)

add_hlsl_shader(GraphicsTest JcabD3D.hlsl "cs_6_0" MipGenerationPassSRGB MipGenerationPassShaderSRGB)
add_hlsl_shader(GraphicsTest JcabD3D.hlsl "cs_6_0" MipGenerationPass     MipGenerationPassShader)

target_compile_definitions(GraphicsTest PRIVATE UNICODE _UNICODE)

target_link_libraries(GraphicsTest PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib gdiplus.lib)

set_property(TARGET GraphicsTest PROPERTY CXX_STANDARD 20)

#function(copy_hlsl_file TargetName File)
#    add_custom_command(
#        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${File}"
#        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/${File}" "${CMAKE_CURRENT_BINARY_DIR}/${File}"
#        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${File}"
#    )
#    add_custom_target(${File}_TARGET DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${File}")
#    add_dependencies(${TargetName} ${File}_TARGET)
#endfunction()
#
#copy_hlsl_file(GraphicsTest model.hlsl)
#copy_hlsl_file(GraphicsTest JcabD3D.hlsl)

# TODO: Add tests and install targets if needed.
