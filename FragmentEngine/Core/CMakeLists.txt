
add_library(FragmentCore SHARED
        src/Engine.cpp
)
target_include_directories(FragmentCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(FragmentCore sdl::sdl)
