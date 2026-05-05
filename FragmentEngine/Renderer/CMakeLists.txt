add_library(FragmentRenderer SHARED
        src/Renderer.cpp
)

target_include_directories(FragmentRenderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})