# conanfile.py
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout

class MyProjectConan(ConanFile):
    name = "AllThatRemains"
    version = "1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("sdl/3.4.0")       # SDL3 from Conan Center
        self.requires("vulkan-headers/1.4.313.0")
        self.requires("vulkan-loader/1.4.313.0")
        self.requires("vulkan-memory-allocator/3.3.0")

    def configure(self):
        self.options["sdl"].shared = True

    def build_requirements(self):
            self.tool_requires("cmake/[>=3.25]")

    def layout(self):
        cmake_layout(self)
