

add_library(FragmentRenderer STATIC
        src/Renderer.cpp
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
target_compile_definitions(FragmentRenderer PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(FragmentRenderer PUBLIC
        sdl::sdl
        glm::glm
        Vulkan::Vulkan
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        fmt::fmt
)