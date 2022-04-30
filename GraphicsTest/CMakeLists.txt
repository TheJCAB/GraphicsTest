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

file(COPY "shaders.hlsl" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# TODO: Add tests and install targets if needed.
