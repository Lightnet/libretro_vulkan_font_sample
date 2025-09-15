cmake_minimum_required(VERSION 3.16)
project(libretro_vulkan_sample)

# Set C standard
# set(CMAKE_C_STANDARD 11)
# set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Include FetchContent module
include(FetchContent)

# Fetch Libretro common headers
FetchContent_Declare(
    libretro_common
    GIT_REPOSITORY https://github.com/libretro/libretro-common.git
    GIT_TAG master
)
FetchContent_MakeAvailable(libretro_common)

#================================================
# IMAGE
#================================================
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    EXCLUDE_FROM_ALL #this disable build?
    GIT_SHALLOW TRUE
    USES_TERMINAL_DOWNLOAD TRUE
)
FetchContent_MakeAvailable(stb)

#================================================
# cglm
#================================================

FetchContent_Declare(
    cglm
    GIT_REPOSITORY https://github.com/recp/cglm.git
    GIT_TAG v0.9.6
    GIT_PROGRESS   TRUE
)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cglm)

set(LIB_NAME libretro_vulkan)

# Define the shared library (Libretro core)
add_library(${LIB_NAME} SHARED 
    #libretro files for vulkan
    ${libretro_common_SOURCE_DIR}/vulkan/vulkan_symbol_wrapper.c
    # test files lib
    # src/lib.c
    src/lib_font.c
)

target_link_libraries(${LIB_NAME} PRIVATE 
    # lua                                               # lua
    cglm                                                # glm
)

# Include directories
target_include_directories(${LIB_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${libretro_common_SOURCE_DIR}/include
    ${cglm_SOURCE_DIR}/include                          # Corrected from clgm_SOURCE_DIR
    ${stb_SOURCE_DIR}                                   # For stb_image.h
)

# Ensure the output is a .dll with no "lib" prefix
# set_target_properties(${LIB_NAME} PROPERTIES
#     PREFIX ""
#     OUTPUT_NAME "libretro_core_vulkan"
# )
