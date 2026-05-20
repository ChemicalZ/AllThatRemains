add_library(FragmentRenderer STATIC
        src/VkRender.cpp
        src/Initializers.cpp
        src/Descriptors.cpp
        src/Loader.cpp
        src/Pipelines.cpp
        src/Images.cpp
        src/Camera.cpp
)

target_include_directories(FragmentRenderer PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(FragmentRenderer PUBLIC
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
        IMGUI_IMPL_VULKAN_USE_VOLK
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(FragmentRenderer PUBLIC
        sdl::sdl
        glm::glm
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        fmt::fmt
        vk-bootstrap::vk-bootstrap
        FragmentLog
        imgui::imgui
        fastgltf::fastgltf
        SDL3_image::SDL3_image
)

# ------------------------------------------------------------
# ImGui Vulkan backend from Conan package
# Conan stores backend files in:
#   <imgui package>/res/bindings
# ------------------------------------------------------------

set(IMGUI_PACKAGE_DIR_CANDIDATES
        ${imgui_PACKAGE_FOLDER}
        ${imgui_PACKAGE_FOLDER_DEBUG}
        ${imgui_PACKAGE_FOLDER_RELEASE}
        ${imgui_PACKAGE_FOLDER_RELWITHDEBINFO}
        ${imgui_PACKAGE_FOLDER_MINSIZEREL}
)

find_path(IMGUI_BACKENDS_DIR
        NAMES imgui_impl_vulkan.h
        HINTS ${IMGUI_PACKAGE_DIR_CANDIDATES}
        PATH_SUFFIXES res/bindings
        NO_DEFAULT_PATH
)

if(NOT IMGUI_BACKENDS_DIR)
    message(FATAL_ERROR
            "Could not find ImGui Vulkan backend directory. "
            "Expected imgui_impl_vulkan.h under <imgui package>/res/bindings."
    )
endif()

message(STATUS "Found ImGui backends: ${IMGUI_BACKENDS_DIR}")

target_sources(FragmentRenderer PRIVATE
        "${IMGUI_BACKENDS_DIR}/imgui_impl_vulkan.cpp"
)

target_include_directories(FragmentRenderer PRIVATE
        "${IMGUI_BACKENDS_DIR}"
)