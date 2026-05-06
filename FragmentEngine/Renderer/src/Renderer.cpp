//
// Created by davon on 5/4/2026.
//

#include "Renderer.h"
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_video.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

//! Define validation layer
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace fe {

    bool checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        for (const char* layerName : validationLayers) {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
            if (!layerFound) {
                return false;
            }
        }
        return true;
    }

    Renderer::Renderer(SDL_Window *window) {
        m_window = window;
        m_instance = VK_NULL_HANDLE;
    }

    Renderer::~Renderer() {
        vkDestroyInstance(m_instance, nullptr);
    }

    void Renderer::Render() {
        if (!m_window)
            return;
    }


    int Renderer::Init() {

        createVulkanInstance();

        return 0;
    }

    void Renderer::createVulkanInstance() {
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested, but not available!");
        }
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "FeApplication";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "FeEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        // Get extensions for use with SDL
        Uint32 sdlExtensionCount = 0;
        const char* const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
        // Add extensions to a vector so that we can modify them
        std::vector<const char*> requiredExtensions;
        for (Uint32 i = 0; i < sdlExtensionCount; i++) {
            requiredExtensions.push_back(sdlExtensions[i]);
        }
        // Add portability extensions
        requiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

        // print a list of available extensions
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
        std::cout << "Available Vulkan extensions:" << std::endl;
        for (const auto&[extensionName, specVersion] : availableExtensions) {
            std::cout << extensionName << std::endl;
        }
        std::cout << "Required Vulkan extensions:" << std::endl;
        for (const auto &extension : requiredExtensions) {
            // Check if supported extension
            bool supported = false;
            for (const auto&[name, version] : availableExtensions) {
                if (strcmp(name, extension) == 0) {
                    supported = true;
                    break;
                }
            }
            if (!supported) {
                throw std::runtime_error("Required Vulkan extension not supported: " + std::string(extension));
            }
            std::cout << "Extension " << extension << " is supported." << std::endl;
        }
        createInfo.enabledExtensionCount = static_cast<Uint32>(requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();
        // Vulkan: Global validation layers to enable
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        }
        else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan instance!");
        }
    }

} // fe