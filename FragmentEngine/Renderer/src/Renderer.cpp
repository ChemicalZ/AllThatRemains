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

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace fe {
    struct Renderer::Impl {
        SDL_Window *window;
        VkInstance instance{};
        VkDebugUtilsMessengerEXT debugMessenger{};
        VkAllocationCallbacks allocator{};

        void createVulkanInstance();

        static std::vector<const char *> getVulkanExtensions();

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
            void *pUserData);

        VkDebugUtilsMessengerCreateInfoEXT populateDebugMessenegerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

        static VkResult CreateDebugUtilsMessengerEXT(
            VkInstance instance,
            const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
            const VkAllocationCallbacks *pAllocator,
            VkDebugUtilsMessengerEXT *pDebugMessenger);

        void vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                                  const VkAllocationCallbacks *pAllocator);
    };


    VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::Impl::debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
        return VK_FALSE;
    }

    VkResult Renderer::Impl::CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkDebugUtilsMessengerEXT *pDebugMessenger) {
        if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")); func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        }
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    void Renderer::Impl::vkDestroyDebugUtilsMessengerEXT(VkInstance instnace, VkDebugUtilsMessengerEXT messenger,
                                       const VkAllocationCallbacks *pAllocator) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            func(instance, messenger, pAllocator);
        }
    }

    bool checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        for (const char *layerName: validationLayers) {
            bool layerFound = false;
            for (const auto &layerProperties: availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
            if (!layerFound)
                return false;
        }
        return true;
    }

    Renderer::Renderer(SDL_Window *window)
        : m_impl(std::make_unique<Impl>()) {
        m_impl->window = window;
        m_impl->instance = VK_NULL_HANDLE;
        m_impl->debugMessenger = VK_NULL_HANDLE;
    }

    Renderer::~Renderer() {
        if (m_impl->debugMessenger)
            m_impl->vkDestroyDebugUtilsMessengerEXT(m_impl->instance, m_impl->debugMessenger, nullptr);
        vkDestroyInstance(m_impl->instance, nullptr);

    }

    void Renderer::Render() {
        if (!m_impl->window)
            return;
    }

    int Renderer::Init() {
        m_impl->createVulkanInstance();
        setupDebugMessenger();

        return 0;
    }

    VkDebugUtilsMessengerCreateInfoEXT Renderer::Impl::populateDebugMessenegerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = Impl::debugCallback;
        createInfo.pUserData = nullptr;
        return createInfo;
    }

    void Renderer::setupDebugMessenger() {
        if constexpr (!enableValidationLayers)
            return;
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
        m_impl->populateDebugMessenegerCreateInfo(createInfo);
        if ( Impl::CreateDebugUtilsMessengerEXT(m_impl->instance, &createInfo, nullptr, &m_impl->debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to set up debug messenger!");
        }

    }

    std::vector<const char *> Renderer::Impl::getVulkanExtensions() {
        Uint32 sdlExtensionCount = 0;
        const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

        std::vector<const char *> requiredExtensions;
        for (Uint32 i = 0; i < sdlExtensionCount; i++) {
            requiredExtensions.push_back(sdlExtensions[i]);
        }
        if (enableValidationLayers)
            requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        requiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

        std::cout << "Available Vulkan extensions:" << std::endl;
        for (const auto &[extensionName, specVersion]: availableExtensions) {
            std::cout << extensionName << std::endl;
        }

        std::cout << "Required Vulkan extensions:" << std::endl;
        for (const auto &extension: requiredExtensions) {
            bool supported = false;
            for (const auto &[name, version]: availableExtensions) {
                if (strcmp(name, extension) == 0) {
                    supported = true;
                    break;
                }
            }
            if (!supported)
                throw std::runtime_error("Required Vulkan extension not supported: " + std::string(extension));
            std::cout << "Extension " << extension << " is supported." << std::endl;
        }
        return requiredExtensions;
    }

    void Renderer::Impl::createVulkanInstance() {
        if (enableValidationLayers && !checkValidationLayerSupport())
            throw std::runtime_error("Validation layers requested, but not available!");

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

        std::vector<const char *> requiredExtensions = getVulkanExtensions();
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        createInfo.enabledExtensionCount = static_cast<Uint32>(requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            populateDebugMessenegerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan instance!");
    }
} // fe
