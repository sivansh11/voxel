cmake_minimum_required(VERSION 3.20)

project(voxel)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

set(HORIZON_INCLUDE_IMGUI ON)
FetchContent_Declare(
  horizon
  GIT_REPOSITORY https://github.com/sivansh11/horizon
  GIT_TAG main
)
FetchContent_MakeAvailable(horizon)

FetchContent_Declare(
  model
  GIT_REPOSITORY https://github.com/sivansh11/model
  GIT_TAG main
)
FetchContent_MakeAvailable(model)

FetchContent_Declare(
  ImGuiFileDialog
  GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog
)
FetchContent_MakeAvailable(ImGuiFileDialog)
target_link_libraries(ImGuiFileDialog PUBLIC imgui_cmake)

file(GLOB_RECURSE SRC_FILES src/*cpp)

add_executable(voxel ${SRC_FILES})

target_link_libraries(voxel
  PUBLIC horizon
  PUBLIC model
  PUBLIC ImGuiFileDialog
)

target_include_directories(voxel
  PUBLIC src
)

# default built type if CMAKE_BUILD_TYPE is not set
set(DEFAULT_BUILT_TYPE "Debug")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${DEFAULT_BUILT_TYPE}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILT_TYPE}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
