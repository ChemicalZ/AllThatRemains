cmake_minimum_required(VERSION 3.25)
project(.)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/build/bin/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/build/bin/debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/build/lib/release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/build/lib/debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/build/lib/release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/build/lib/debug)
add_compile_definitions(VULKAN_HPP_NO_TO_STRING)
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(volk REQUIRED)
find_package(Vulkan REQUIRED)
find_package(spdlog REQUIRED)
find_package(glm REQUIRED)
find_package(fmt REQUIRED)
find_package(vk-bootstrap REQUIRED)
find_package(imgui REQUIRED)
find_package(fastgltf REQUIRED)

add_subdirectory(FragmentEngine)
add_subdirectory(AllThatRemains)

# ── Shader compilation ────────────────────────────────────────────────────────
# Both build/bin/debug/ and build/bin/release/ use ../shaders/ at runtime,
# so a single output dir serves both configurations.
set(SHADER_SRC_DIR  "${CMAKE_SOURCE_DIR}/FragmentEngine/Renderer/shaders")
set(SHADER_OUT_DIR  "${CMAKE_SOURCE_DIR}/build/bin/shaders")

# Prefer glslc from the Vulkan SDK (found by find_package(Vulkan) above).
# Fall back to PATH if the SDK variable is empty.
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc)
endif()

if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(WARNING
        "glslc not found — shaders will NOT be compiled automatically.\n"
        "Run compile_shaders.ps1 / compile_shaders.sh from the shaders directory manually.")
else()
    # Stage files — each produces one .spv
    file(GLOB_RECURSE SHADER_STAGE_FILES CONFIGURE_DEPENDS
        "${SHADER_SRC_DIR}/*.vert"
        "${SHADER_SRC_DIR}/*.frag"
        "${SHADER_SRC_DIR}/*.comp"
        "${SHADER_SRC_DIR}/*.geom"
        "${SHADER_SRC_DIR}/*.tesc"
        "${SHADER_SRC_DIR}/*.tese"
        "${SHADER_SRC_DIR}/*.rgen"
        "${SHADER_SRC_DIR}/*.rchit"
        "${SHADER_SRC_DIR}/*.rmiss"
        "${SHADER_SRC_DIR}/*.rint"
        "${SHADER_SRC_DIR}/*.rahit"
        "${SHADER_SRC_DIR}/*.rcall"
    )

    # Include files — changing any of these invalidates all dependent shaders
    file(GLOB SHADER_INCLUDE_FILES CONFIGURE_DEPENDS
        "${SHADER_SRC_DIR}/*.glsl"
        "${SHADER_SRC_DIR}/*.inc"
    )

    set(SPV_OUTPUTS "")
    foreach(SHADER_SRC IN LISTS SHADER_STAGE_FILES)
        get_filename_component(SHADER_NAME "${SHADER_SRC}" NAME)
        set(SPV_OUT "${SHADER_OUT_DIR}/${SHADER_NAME}.spv")

        add_custom_command(
            OUTPUT  "${SPV_OUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUT_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}"
                    --target-env=vulkan1.3
                    -I "${SHADER_SRC_DIR}"
                    "${SHADER_SRC}"
                    -o "${SPV_OUT}"
            DEPENDS "${SHADER_SRC}" ${SHADER_INCLUDE_FILES}
            COMMENT "Compiling shader: ${SHADER_NAME}"
            VERBATIM
        )

        list(APPEND SPV_OUTPUTS "${SPV_OUT}")
    endforeach()

    add_custom_target(CompileShaders ALL DEPENDS ${SPV_OUTPUTS})
    add_dependencies(AllThatRemains CompileShaders)
endif()

