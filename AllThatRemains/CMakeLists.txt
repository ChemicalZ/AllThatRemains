add_executable(AllThatRemains
        src/main.cpp
)

target_link_libraries(AllThatRemains PUBLIC
        FragmentCore
)

# ── Copy runtime DLLs on Windows ─────────────────────────────────────────────
if(WIN32 AND DEFINED CONAN_RUNTIME_LIB_DIRS)
    foreach(dir IN LISTS CONAN_RUNTIME_LIB_DIRS)
        if(IS_ABSOLUTE "${dir}" AND EXISTS "${dir}")
            file(GLOB dlls "${dir}/*.dll")
            foreach(dll IN LISTS dlls)
                add_custom_command(TARGET AllThatRemains POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" $<TARGET_FILE_DIR:AllThatRemains>
                    VERBATIM
                )
            endforeach()
        endif()
    endforeach()
endif()