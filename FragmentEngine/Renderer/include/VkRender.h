#pragma once

#include "Types.h"
#include "Initializers.h"


#include "VkBootstrap.h"

struct SDL_Window;

namespace fe{
    struct FrameData {
        VkCommandPool _commandPool;
        VkCommandBuffer _mainCommandBuffer;
        VkSemaphore _swapchainSemaphore, _renderSemaphore;
	    VkFence _renderFence;
    };

    constexpr unsigned int FRAME_OVERLAP = 2;
    class VkRender {
        VkRender(SDL_Window* window);
        ~VkRender();

        void Draw();
    private:
        SDL_Window *_window;
        bool _isInitialized;

        VkInstance _instance;// Vulkan library handle
        VkDebugUtilsMessengerEXT _debug_messenger;// Vulkan debug output handle
        VkPhysicalDevice _chosenGPU;// GPU chosen as the default device
        VkDevice _device; // Vulkan device for commands
        VkSurfaceKHR _surface;// Vulkan window surface

        // Swap chain
        VkSwapchainKHR _swapchain;
        VkFormat _swapchainImageFormat;

        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;
        VkExtent2D _swapchainExtent;

        // Commands
        FrameData _frames[FRAME_OVERLAP];
        FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]};

        VkQueue _graphicsQueue;
        uint32_t _graphicsQueueFamily;

        void init_vulkan();
        void init_swapchain();
        void init_commands();
        void init_sync_structures();

    	void create_swapchain(uint32_t width, uint32_t height);
	    void destroy_swapchain();
    }
}