//
// Created by DaVonte Carter Vault on 5/15/26.
//

// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>


#define VK_CHECK(x)                                                     \
do {                                                                \
    VkResult err = x;                                               \
    if (err) {                                                      \
        fmt::println("Detected Vulkan error: {}", vk::to_string(static_cast<vk::Result>(err))); \
        abort();                                                    \
    }                                                               \
} while (0)