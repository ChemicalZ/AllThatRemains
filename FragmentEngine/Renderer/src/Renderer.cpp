//
// Created by davon on 5/4/2026.
//

#include "Renderer.h"

#include <algorithm>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_video.h>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>
#include <optional>
#include <set>

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace fe {
    struct Renderer::Impl {
        SDL_Window *_pWindow;
        VkSurfaceKHR _surface{};
        VkInstance _instance{};
        VkDebugUtilsMessengerEXT _debugMessenger{};
        VkAllocationCallbacks _allocator{};
        VkPhysicalDevice _physicalDevice{};
        VkDevice _device{};
        VkQueue _graphicsQueue{};
        VkQueue _presentQueue{};
        VkSwapchainKHR _swapchain{};

        struct QueueFamilyIndices {
            std::optional<uint32_t> graphicsFamily;
            std::optional<uint32_t> presentFamily;
            bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
        };
        struct SwapChainSupportDetails {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };
        void createVulkanInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createSwapChain(VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        int rateDeviceSuitability(VkPhysicalDevice device);
        static std::vector<const char *> getVulkanExtensions();

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
            void *pUserData);

        VkDebugUtilsMessengerCreateInfoEXT populateDebugMessenegerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
        SwapChainSupportDetails populateSwapChainSupport(VkPhysicalDevice device);
        static VkResult CreateDebugUtilsMessengerEXT(
            VkInstance instance,
            const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
            const VkAllocationCallbacks *pAllocator,
            VkDebugUtilsMessengerEXT *pDebugMessenger);

        void vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                                  const VkAllocationCallbacks *pAllocator);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D calculateExtent(const VkSurfaceCapabilitiesKHR& capabilities);
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
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            func(_instance, messenger, pAllocator);
        }
    }

    VkSurfaceFormatKHR Renderer::Impl::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
        for (const auto &availableFormat: availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR Renderer::Impl::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
        for (const auto &availablePresentMode: availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D Renderer::Impl::calculateExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width, height;
        SDL_GetWindowSizeInPixels(_pWindow, &width, &height);
        SDL_GetWindowSize(_pWindow, &width, &height);
        VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
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
        : _pImpl(std::make_unique<Impl>()) {
        _pImpl->_pWindow = window;
        _pImpl->_instance = VK_NULL_HANDLE;
        _pImpl->_debugMessenger = VK_NULL_HANDLE;
        _pImpl->_physicalDevice = VK_NULL_HANDLE;

    }

    Renderer::~Renderer() {
        if (_pImpl->_swapchain)
            vkDestroySwapchainKHR(_pImpl->_device, _pImpl->_swapchain, nullptr);
        if (_pImpl->_device)
            vkDestroyDevice(_pImpl->_device, nullptr);
        if (_pImpl->_surface)
            vkDestroySurfaceKHR(_pImpl->_instance, _pImpl->_surface, nullptr);
        if (_pImpl->_debugMessenger)
            _pImpl->vkDestroyDebugUtilsMessengerEXT(_pImpl->_instance, _pImpl->_debugMessenger, nullptr);

        if (_pImpl->_instance)
            vkDestroyInstance(_pImpl->_instance, nullptr);
    }

    void Renderer::Render() {
        if (!_pImpl->_pWindow)
            return;
    }

    int Renderer::Init() {
        _pImpl->createVulkanInstance();
        _pImpl->setupDebugMessenger();
        _pImpl->createSurface();
        _pImpl->pickPhysicalDevice();
        _pImpl->createLogicalDevice();
        _pImpl->createSwapChain();


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

    Renderer::Impl::SwapChainSupportDetails Renderer::Impl::populateSwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);

        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, details.presentModes.data());
        }
        return details;
    }

    void Renderer::Impl::setupDebugMessenger() {
        if constexpr (!enableValidationLayers)
            return;
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
        populateDebugMessenegerCreateInfo(createInfo);
        if ( CreateDebugUtilsMessengerEXT(_instance, &createInfo, nullptr, &_debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to set up debug messenger!");
        }

    }

    void Renderer::Impl::createSurface() {
        if (!SDL_Vulkan_CreateSurface(_pWindow, _instance, nullptr, &_surface)) {
            throw std::runtime_error("Failed to create Vulkan surface!");
        }
    }

    void Renderer::Impl::pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
        if (deviceCount == 0)
            throw std::runtime_error("Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

        std::multimap<int, VkPhysicalDevice> candidates;
        for (const auto &device: devices) {
            int score = rateDeviceSuitability(device);
            candidates.insert({score, device});
        }
        if (candidates.rbegin()-> first > 0) {
            _physicalDevice = candidates.rbegin()->second;
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(_physicalDevice, &properties);
            std::cout << "Selected GPU: " << properties.deviceName << "with a score of " << candidates.rbegin()->first << std::endl;
        }
        else {
            throw std::runtime_error("Failed to find a suitable GPU!");
        }
    }

    void Renderer::Impl::createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};
        float queuePriorities = 1.0f;

        for (uint32_t queueFamily: uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriorities;
            queueCreateInfos.push_back(queueCreateInfo);
        }


        VkPhysicalDeviceFeatures deviceFeatures = {};

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

        // ******
        // This is where we want to enable devices
        // *******
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();


        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        }
        else {
            createInfo.enabledLayerCount = 0;
        }
        if (vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create logical device!");
        }
        vkGetDeviceQueue(_device, indices.graphicsFamily.value(), 0, &_graphicsQueue);
        vkGetDeviceQueue(_device, indices.presentFamily.value(), 0, &_presentQueue);
    }

    void Renderer::Impl::createSwapChain(VkSwapchainKHR oldSwapchain) {
        SwapChainSupportDetails swapChainSupport = populateSwapChainSupport(_physicalDevice);
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = calculateExtent(swapChainSupport.capabilities);
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }
        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = _surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }
        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = oldSwapchain;

        if (vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain!");
        }
    }

    bool Renderer::Impl::checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
        for (const auto &extension: availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }
        return requiredExtensions.empty();
    }

    Renderer::Impl::QueueFamilyIndices Renderer::Impl::findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto &queueFamily: queueFamilies) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            if (presentSupport) {
                indices.presentFamily = i;
            }
            if (indices.isComplete())
                break;
            i++;
        }
        return indices;
    }

    int Renderer::Impl::rateDeviceSuitability(VkPhysicalDevice device) {
        int score = 0;
        VkPhysicalDeviceProperties properties;
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceProperties(device, &properties);
        vkGetPhysicalDeviceFeatures(device, &features);

        // check device for extensions
        if (!checkDeviceExtensionSupport(device)) {
            return 0;
        }
        // Ensure that the swapchain is suitable
        SwapChainSupportDetails swapChainSupport = populateSwapChainSupport(device);
        score += !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty() ? 1000 : 0;

        QueueFamilyIndices indices = findQueueFamilies(device);
        score += indices.isComplete() ? 1000 : 0;

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }
        score += static_cast<int>(properties.limits.maxImageDimension2D);

        // can't run without geometry shaders
        if (!features.geometryShader) {
            return 0;
        }

        return score;
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

        // // Print out available extensions
        // std::cout << "Available Vulkan Instance extensions:" << std::endl;
        // for (const auto &[extensionName, specVersion]: availableExtensions) {
        //     std::cout << extensionName << std::endl;
        // }

        std::cout << "Required Vulkan Instance extensions:" << std::endl;
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

        if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan instance!");
    }
} // fe
