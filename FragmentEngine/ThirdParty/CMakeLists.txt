add_subdirectory(ImGuizmo)

# ImGuizmo needs imgui headers but doesn't find them on its own when
# built as a subdirectory (only the example path links imgui).
find_package(imgui REQUIRED)
target_link_libraries(imguizmo PUBLIC imgui::imgui)