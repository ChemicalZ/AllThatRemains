

add_library(FragmentRenderer SHARED
        src/Renderer.cpp
)

target_include_directories(FragmentRenderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(FragmentRenderer PUBLIC
        vulkan-headers::vulkan-headers
        Vulkan::Loader
        GPUOpen::VulkanMemoryAllocator
)