#include "VkRender.h"
#include <SDL3/SDL.h>
#include "LogInternal.h"
#include "Images.h"
#include "Descriptors.h"
#include "Pipelines.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"


namespace fe {
    VkRender::VkRender(SDL_Window* window)  {
        if (!window) {
            throw std::runtime_error("Vulkan Renderer did not receive a valid window " + std::string(SDL_GetError()));
        }
        _window = window;
        _isInitialized = false;

        init_vulkan();
        init_swapchain();
        init_commands();
        init_sync_structures();
        init_descriptors();
        init_pipelines();

        _isInitialized = true;
        FE_CORE_LOG("Vulkan Renderer successfully initialized");

    }
    VkRender::~VkRender() {
        if (_isInitialized) {
            vkDeviceWaitIdle(_device);
            _mainDeletionQueue.flush();

            for (int i = 0; i < FRAME_OVERLAP; i++) {
            
                //already written from before
                vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

                //destroy sync objects
                vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
                vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
                vkDestroySemaphore(_device ,_frames[i]._swapchainSemaphore, nullptr);
                _frames[i]._deletionQueue.flush();
            }


            destroy_swapchain();

            vkDestroySurfaceKHR(_instance, _surface, nullptr);
            vkDestroyDevice(_device, nullptr);
            
            vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
            vkDestroyInstance(_instance, nullptr);
        }
    }

    void VkRender::Draw(){
        // wait until the gpu has finished rendering the last frame. Timeout of 1 second
        VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
        get_current_frame()._deletionQueue.flush();

        VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

        VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

        // now that we are sure that the commands finished executing, we can safely
        // reset the command buffer to begin recording again.
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        //begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
        VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        _drawExtent.width = _drawImage.imageExtent.width;
        _drawExtent.height = _drawImage.imageExtent.height;

        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));	

        // transition our main draw image into general layout so we can write into it
        // we will overwrite it all so we dont care about what was the older layout
        transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        draw_background(cmd);

        //transition the draw image and the swapchain image into their correct transfer layouts
        transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // execute a copy from the draw image into the swapchain
        copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

        // set swapchain image layout to Present so we can show it on the screen
        transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        //finalize the command buffer (we can no longer add commands, but it can now be executed)
        VK_CHECK(vkEndCommandBuffer(cmd));


        //prepare the submission to the queue. 
        //we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
        //we will signal the _renderSemaphore, to signal that rendering has finished

        VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);	
        
        VkSemaphoreSubmitInfo waitInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,get_current_frame()._swapchainSemaphore);
        VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);	
        
        VkSubmitInfo2 submit = submit_info(&cmdinfo,&signalInfo,&waitInfo);	

        //submit command buffer to the queue and execute it.
        // _renderFence will now block until the graphic commands finish execution
        VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

        //prepare present
        // this will put the image we just rendered to into the visible window.
        // we want to wait on the _renderSemaphore for that, 
        // as its necessary that drawing commands have finished before the image is displayed to the user
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = nullptr;
        presentInfo.pSwapchains = &_swapchain;
        presentInfo.swapchainCount = 1;

        presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
        presentInfo.waitSemaphoreCount = 1;

        presentInfo.pImageIndices = &swapchainImageIndex;

        VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

        //increase the number of frames drawn
        _frameNumber++;
    }
    void VkRender::draw_background(VkCommandBuffer cmd)
    {
        //make a clear-color from frame number. This will flash with a 120 frame period.
        VkClearColorValue clearValue;
        float flash = std::abs(std::sin(_frameNumber / 120.f));
        clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

        VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

        // bind the gradient drawing compute pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipeline);

        // bind the descriptor set containing the draw image for the compute pipeline
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

        // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
        vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
    }
    void VkRender::init_vulkan() {
        vkb::InstanceBuilder builder;

        auto inst_ret = builder.set_app_name("FragmentEngine Application")
        .request_validation_layers(bUseValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

        vkb::Instance vkb_inst = inst_ret.value();
        _instance = vkb_inst.instance;  
	    _debug_messenger = vkb_inst.debug_messenger;
        SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

        //vulkan 1.3 features
        VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        features.dynamicRendering = true;
        features.synchronization2 = true;

        //vulkan 1.2 features
        VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        features12.bufferDeviceAddress = true;
        features12.descriptorIndexing = true;


        //use vkbootstrap to select a gpu. 
        //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
        vkb::PhysicalDeviceSelector selector{ vkb_inst };
        vkb::PhysicalDevice physicalDevice = selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_surface(_surface)
            .select()
            .value();


        //create the final vulkan device
        vkb::DeviceBuilder deviceBuilder{ physicalDevice };

        vkb::Device vkbDevice = deviceBuilder.build().value();

        // Get the VkDevice handle used in the rest of a vulkan application
        _device = vkbDevice.device;
        _chosenGPU = physicalDevice.physical_device;

        // use vkbootstrap to get a Graphics queue
        _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
        _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

            // initialize the memory allocator
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = _chosenGPU;
        allocatorInfo.device = _device;
        allocatorInfo.instance = _instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        vmaCreateAllocator(&allocatorInfo, &_allocator);

        _mainDeletionQueue.push_function([&]() {
            vmaDestroyAllocator(_allocator);
        });
    }
    void VkRender::init_swapchain() {
        create_swapchain(_windowExtent.width, _windowExtent.height);
        //draw image size will match the window
        VkExtent3D drawImageExtent = {
            _windowExtent.width,
            _windowExtent.height,
            1
        };

        //hardcoding the draw format to 32 bit float
        _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        _drawImage.imageExtent = drawImageExtent;

        VkImageUsageFlags drawImageUsages{};
        drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
        drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

        //for the draw image, we want to allocate it from gpu local memory
        VmaAllocationCreateInfo rimg_allocinfo = {};
        rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        //allocate and create the image
        vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

        //build a image-view for the draw image to use for rendering
        VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

        VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

        //add to deletion queues
        _mainDeletionQueue.push_function([=]() {
            vkDestroyImageView(_device, _drawImage.imageView, nullptr);
            vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
        });
    }

    void VkRender::init_commands() {
        //create a command pool for commands submitted to the graphics queue.
        //we also want the pool to allow for resetting of individual command buffers
        VkCommandPoolCreateInfo commandPoolInfo =  {};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.pNext = nullptr;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;
        
        for (int i = 0; i < FRAME_OVERLAP; i++) {

            VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

            // allocate the default command buffer that we will use for rendering
            VkCommandBufferAllocateInfo cmdAllocInfo = {};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.pNext = nullptr;
            cmdAllocInfo.commandPool = _frames[i]._commandPool;
            cmdAllocInfo.commandBufferCount = 1;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

            VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
	}
    }
    void VkRender::init_sync_structures() {
        VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
        VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

        for (int i = 0; i < FRAME_OVERLAP; i++) {
            VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

            VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
            VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
        }
        
    }

    void VkRender::init_descriptors()
    {
        //create a descriptor pool that will hold 10 sets with 1 image each
        std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
        {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
        };

        globalDescriptorAllocator.init_pool(_device, 10, sizes);

        //make the descriptor set layout for our compute draw
        {
            DescriptorLayoutBuilder builder;
            builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
        }
        
        //allocate a descriptor set for our draw image
        _drawImageDescriptors = globalDescriptorAllocator.allocate(_device,_drawImageDescriptorLayout);	

        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imgInfo.imageView = _drawImage.imageView;
        
        VkWriteDescriptorSet drawImageWrite = {};
        drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        drawImageWrite.pNext = nullptr;
        
        drawImageWrite.dstBinding = 0;
        drawImageWrite.dstSet = _drawImageDescriptors;
        drawImageWrite.descriptorCount = 1;
        drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        drawImageWrite.pImageInfo = &imgInfo;

        vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

        //make sure both the descriptor allocator and the new layout get cleaned up properly
        _mainDeletionQueue.push_function([&]() {
            globalDescriptorAllocator.destroy_pool(_device);

            vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        });
    }

    void VkRender::init_pipelines()
    {
        init_background_pipelines();
    }

    void VkRender::init_background_pipelines()
    {
        VkPipelineLayoutCreateInfo computeLayout{};
        computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayout.pNext = nullptr;
        computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
        computeLayout.setLayoutCount = 1;

        VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

        VkShaderModule computeDrawShader;
        if (!vkutil::load_shader_module("../shaders/gradient.comp.spv", _device, &computeDrawShader))
        {
            FE_CORE_ERROR("Error when building the compuse shader");
        }

        VkPipelineShaderStageCreateInfo stageinfo{};
        stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageinfo.pNext = nullptr;
        stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageinfo.module = computeDrawShader;
        stageinfo.pName = "main";

        VkComputePipelineCreateInfo computePipelineCreateInfo{};
        computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.pNext = nullptr;
        computePipelineCreateInfo.layout = _gradientPipelineLayout;
        computePipelineCreateInfo.stage = stageinfo;
        
        VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &_gradientPipeline));
        	vkDestroyShaderModule(_device, computeDrawShader, nullptr);

        _mainDeletionQueue.push_function([&]() {
            vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
            vkDestroyPipeline(_device, _gradientPipeline, nullptr);
            });
    }

    void VkRender::create_swapchain(uint32_t width, uint32_t height){
        vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

        _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

        vkb::Swapchain vkbSwapchain = swapchainBuilder
            //.use_default_format_selection()
            .set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
            //use vsync present mode
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

        _swapchainExtent = vkbSwapchain.extent;
        //store swapchain and its related images
        _swapchain = vkbSwapchain.swapchain;
        _swapchainImages = vkbSwapchain.get_images().value();
        _swapchainImageViews = vkbSwapchain.get_image_views().value();
    }

    
	void VkRender::destroy_swapchain(){
        vkDestroySwapchainKHR(_device, _swapchain, nullptr);

        // destroy swapchain resources
        for (int i = 0; i < _swapchainImageViews.size(); i++) {

            vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
        }
    }
}