
add_library(FragmentCore STATIC
        src/Engine.cpp
)

target_include_directories(FragmentCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(FragmentCore PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(FragmentCore
        sdl::sdl
        spdlog::spdlog
        FragmentLog
        FragmentRenderer
)
