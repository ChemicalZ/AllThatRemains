cmake_minimum_required(VERSION 4.2)
project(.)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin/debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/lib/release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/lib/debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/lib/release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/lib/debug)

find_package(SDL3 REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(VulkanLoader REQUIRED)
find_package(VulkanHeaders REQUIRED)

add_subdirectory(FragmentEngine)
add_subdirectory(AllThatRemains)
