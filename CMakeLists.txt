# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.20)

set(CMAKE_C_FLAGS_CHECKED             "/Zi /O2 /Ob1")
set(CMAKE_CXX_FLAGS_CHECKED           "/Zi /O2 /Ob1")
set(CMAKE_EXE_LINKER_FLAGS_CHECKED    "/debug /INCREMENTAL")
set(CMAKE_SHARED_LINKER_FLAGS_CHECKED "/debug /INCREMENTAL")

project ("GraphicsTest")

# Include sub-projects.
add_subdirectory ("GraphicsTest")
