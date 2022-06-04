# CMakeList.txt : CMake project for GraphicsTest, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.20)

add_executable (GraphicsTest WIN32
	GraphicsTest.cpp

	Model.cpp           Model.h
	ObjModel.cpp        ObjModel.h
	Pipeline.cpp        Pipeline.h
    Pipeline2.cpp       Pipeline2.h
    Pipeline3.cpp       Pipeline3.h

    IRenderPipeline.h
    ViewSpace.h
    Lighting.h
)

add_hlsl_shader(GraphicsTest Model.hlsl "vs_6_0" VisibilitySolidVS     Model_VisibilitySolidVSShader     Model.h)
add_hlsl_shader(GraphicsTest Model.hlsl "ps_6_0" VisibilitySolidPS     Model_VisibilitySolidPSShader     Model.h)
add_hlsl_shader(GraphicsTest Model.hlsl "vs_6_0" VisibilityAlphaTestVS Model_VisibilityAlphaTestVSShader Model.h)
add_hlsl_shader(GraphicsTest Model.hlsl "ps_6_0" VisibilityAlphaTestPS Model_VisibilityAlphaTestPSShader Model.h)

add_hlsl_shader(GraphicsTest Pipeline.hlsl "cs_6_0" MsaaGatherPass VisDefMsaaPipeline_MsaaGatherPassShader Pipeline.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "cs_6_0" ColorPass      VisDefMsaaPipeline_ColorPassShader      Pipeline.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "cs_6_0" Postprocess    VisDefMsaaPipeline_PostprocessShader    Pipeline.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "vs_6_0" ResolveVS      VisDefMsaaPipeline_ResolveVSShader      Pipeline.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline.hlsl "ps_6_0" ResolvePS      VisDefMsaaPipeline_ResolvePSShader      Pipeline.h Lighting.h ViewSpace.h Model.h)

add_hlsl_shader(GraphicsTest Pipeline2.hlsl "cs_6_0" ColorPass   VisDefPipeline_ColorPassShader   Pipeline2.h Pipeline3.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline2.hlsl "cs_6_0" Postprocess VisDefPipeline_PostprocessShader Pipeline2.h Pipeline3.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline2.hlsl "vs_6_0" ResolveVS   VisDefPipeline_ResolveVSShader   Pipeline2.h Pipeline3.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline2.hlsl "ps_6_0" ResolvePS   VisDefPipeline_ResolvePSShader   Pipeline2.h Pipeline3.h Lighting.h ViewSpace.h Model.h)

add_hlsl_shader(GraphicsTest Pipeline3.hlsl "cs_6_0" MsaaGatherPass VisDefMsaa3Pipeline_MsaaGatherPassShader Pipeline3.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "cs_6_0" ColorPass      VisDefMsaa3Pipeline_ColorPassShader      Pipeline3.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "cs_6_0" Postprocess    VisDefMsaa3Pipeline_PostprocessShader    Pipeline3.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "vs_6_0" ResolveVS      VisDefMsaa3Pipeline_ResolveVSShader      Pipeline3.h Lighting.h ViewSpace.h Model.h)
add_hlsl_shader(GraphicsTest Pipeline3.hlsl "ps_6_0" ResolvePS      VisDefMsaa3Pipeline_ResolvePSShader      Pipeline3.h Lighting.h ViewSpace.h Model.h)

target_compile_definitions(GraphicsTest PRIVATE UNICODE _UNICODE)

target_link_libraries(GraphicsTest PRIVATE JcabD3D)

set_property(TARGET GraphicsTest PROPERTY CXX_STANDARD 20)

# TODO: Add tests and install targets if needed.
