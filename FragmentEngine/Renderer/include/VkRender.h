#pragma once

#include "Types.h"
#include "Initializers.h"


#include "VkBootstrap.h"

struct SDL_Window;

namespace fe{
    struct DeletionQueue
    {
        std::deque<std::function<void()>> deletors;

        void push_function(std::function<void()>&& function) {
            deletors.push_back(function);
        }

        void flush() {
            // reverse iterate the deletion queue to execute all the functions
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
                (*it)(); //call functors
            }

            deletors.clear();
        }
    };
    struct FrameData {
        VkCommandPool _commandPool;
        VkCommandBuffer _mainCommandBuffer;
        VkSemaphore _swapchainSemaphore, _renderSemaphore;
	    VkFence _renderFence;
        DeletionQueue _deletionQueue;
    };
    struct AllocatedImage {
        VkImage image;
        VkImageView imageView;
        VmaAllocation allocation;
        VkExtent3D imageExtent;
        VkFormat imageFormat;
    };

    constexpr unsigned int FRAME_OVERLAP = 2;
    class VkRender {
        int _frameNumber {0};
        bool _isInitialized;


        VkRender(SDL_Window* window);
        ~VkRender();

        void Draw();
    private:
        SDL_Window *_window;
        DeletionQueue _deletionQueue;
        
        VmaAllocator _allocator;

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

        //draw resources
        AllocatedImage _drawImage;
        VkExtent2D _drawExtent;

        DescriptorAllocator globalDescriptorAllocator;

        VkDescriptorSet _drawImageDescriptors;
        VkDescriptorSetLayout _drawImageDescriptorLayout;

        VkPipeline _gradientPipeline;
        VkPipelineLayout _gradientPipelineLayout;

        void init_vulkan();
        void init_swapchain();
        void init_commands();
        void init_sync_structures();
        void init_descriptors();
        void init_pipelines();
        void init_background_pipelines();
        
    	void create_swapchain(uint32_t width, uint32_t height);
	    void destroy_swapchain();

        void draw_background(VkCommandBuffer cmd);


    }
}