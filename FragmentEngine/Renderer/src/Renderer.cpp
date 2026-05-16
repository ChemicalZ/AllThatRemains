//
// Created by davon on 5/4/2026.
//

#include "Renderer.h"
#include <volk.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_video.h>
#include <iostream>




namespace fe {

    Renderer::Renderer(SDL_Window *window) {
        m_window = window;
    }

    void Renderer::Render() {
        if (!m_window)
            return;
    }

    int Renderer::Init() {
        if (volkInitialize() != VK_SUCCESS) {
            throw std::runtime_error("Failed to initialize Volk!");
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

        Uint32 sdlExtensionCount = 0;
        const char* const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

        createInfo.enabledExtensionCount = sdlExtensionCount;
        createInfo.ppEnabledExtensionNames = sdlExtensions;

        createInfo.enabledLayerCount = 0;

        if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan instance!");
        }

        volkLoadInstance(m_instance);

        return 0;
    }
} // fe