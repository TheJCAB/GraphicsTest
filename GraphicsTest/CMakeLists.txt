# CMakeList.txt : CMake project for GraphicsTest, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.20)

# Add source to this project's executable.
add_executable (GraphicsTest WIN32
	GraphicsTest.cpp

	JcabD3D.cpp			JcabD3D.h
)

target_compile_definitions(GraphicsTest PRIVATE UNICODE _UNICODE)

target_link_libraries(GraphicsTest PRIVATE d3d12.lib dxgi.lib d3dcompiler.lib)

set_property(TARGET GraphicsTest PROPERTY CXX_STANDARD 20)

function(copy_hlsl_file TargetName File)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${File}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/${File}" "${CMAKE_CURRENT_BINARY_DIR}/${File}"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${File}"
    )
    add_custom_target(${File}_TARGET DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${File}")
    add_dependencies(${TargetName} ${File}_TARGET)
endfunction()

copy_hlsl_file(GraphicsTest shaders.hlsl)
copy_hlsl_file(GraphicsTest model.hlsl)

# TODO: Add tests and install targets if needed.
