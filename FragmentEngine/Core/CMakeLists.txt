
add_library(FragmentCore STATIC
        src/Engine.cpp
)
target_include_directories(FragmentCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(FragmentCore sdl::sdl)
