
add_library(FragmentLog STATIC
        src/Log.cpp
)

target_include_directories(FragmentLog PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(FragmentLog PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(FragmentLog
        sdl::sdl
        spdlog::spdlog
)
