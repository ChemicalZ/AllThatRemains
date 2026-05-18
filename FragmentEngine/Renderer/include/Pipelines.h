#pragma once 
#include <Types.h>

namespace fe {

    bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
};