# conanfile.py
from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout

class MyProjectConan(ConanFile):
    name = "AllThatRemains"
    version = "1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("sdl/3.4.0")       # SDL3 from Conan Center
        self.requires("volk/1.4.313.0")
        self.requires("vulkan-headers/1.4.313.0")
        self.requires("vulkan-memory-allocator/3.3.0")
        self.requires("spdlog/1.17.0")
        self.requires("glm/1.0.1")
        self.requires("fmt/12.1.0")
        # self.requires("vk-bootstrap/1.3.296")
        self.requires("vk-bootstrap/1.3.296", override="vulkan-headers/1.4.313.0")
        self.requires("imgui/1.92.7")
        self.requires("fastgltf/0.9.0")
        self.requires("sdl_image/3.4.0")


    def configure(self):
        self.options["sdl"].shared = True
        self.options["sdl_image"].shared = True
        self.options["sdl_image"].with_libtiff = False
        self.options["sdl_image"].with_avif = False
        self.options["imgui"].with_sdl3_binding = True
        self.options["imgui"].with_vulkan_binding = True
    def build_requirements(self):
            self.tool_requires("cmake/[>=3.25]")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()