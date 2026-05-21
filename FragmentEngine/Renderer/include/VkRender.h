#pragma once

#include "Descriptors.h"
#include "Types.h"
#include "Camera.h"
#include "Loader.h"
#include "ResourceManager.h"

#include <unordered_map>
#include <chrono>

namespace fe {

// Tracks current image layout to eliminate manual old-layout bookkeeping.
struct ImageStateTracker {
    void reset(VkImage img, VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED) {
        _states[img] = layout;
    }
    VkImageLayout get(VkImage img) const {
        auto it = _states.find(img);
        return it != _states.end() ? it->second : VK_IMAGE_LAYOUT_UNDEFINED;
    }
    void set(VkImage img, VkImageLayout layout) { _states[img] = layout; }
private:
    std::unordered_map<VkImage, VkImageLayout> _states;
};

} // namespace fe

struct SDL_Window;
union SDL_Event;

namespace fe {

    struct DeletionQueue
    {
        std::deque<std::function<void()>> deletors;

        void push_function(std::function<void()>&& function) {
            deletors.push_back(function);
        }

        void flush() {
            for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
                (*it)();
            }
            deletors.clear();
        }
    };

    struct FrameData {
        VkCommandPool _commandPool;
        VkCommandBuffer _mainCommandBuffer;
        VkFence _renderFence;
        DeletionQueue _deletionQueue;
        DescriptorAllocatorGrowable _frameDescriptors;
        AllocatedBuffer _sceneDataBuffer;
    };

    struct ComputePushConstants {
        glm::vec4 data1;
        glm::vec4 data2;
        glm::vec4 data3;
        glm::vec4 data4;
    };

    struct ComputeEffect {
        const char* name;
        VkPipeline pipeline;
        VkPipelineLayout layout;
        ComputePushConstants data;
    };

    struct RenderObject {
        uint32_t indexCount;
        uint32_t firstIndex;
        VkBuffer indexBuffer;
        MaterialInstance* material;
        Bounds bounds;
        glm::mat4 transform;
        VkDeviceAddress vertexBufferAddress;
    };

    struct DrawContext {
        std::vector<RenderObject> OpaqueSurfaces;
        std::vector<RenderObject> TransparentSurfaces;
    };

    struct EngineStats {
        long long frameTime;
        uint32_t triangle_count;
        int drawcall_count;
        float mesh_draw_time;
    };

    struct TextureID {
        uint32_t Index;
    };

    struct TextureCache {
        std::vector<VkDescriptorImageInfo> Cache;
        std::vector<uint32_t> _freeSlots;
        std::unordered_map<std::string, TextureID> NameMap;
        TextureID AddTexture(const VkImageView& image, VkSampler sampler);
        void FreeTexturesWithView(VkImageView view, VkDescriptorImageInfo fallback);
        void FreeTexturesWithSampler(VkSampler sampler, VkDescriptorImageInfo fallback);
    };

    struct GLTFMetallic_Roughness {
        MaterialPipeline opaquePipeline;
        MaterialPipeline transparentPipeline;

        VkDescriptorSetLayout materialLayout;

        struct MaterialConstants {
            glm::vec4 colorFactors;
            glm::vec4 metal_rough_factors;
            uint32_t colorTexID;
            uint32_t metalRoughTexID;
            uint32_t pad1;
            uint32_t pad2;
            glm::vec4 extra[13];
        };

        struct MaterialResources {
            AllocatedImage colorImage;
            VkSampler colorSampler;
            AllocatedImage metalRoughImage;
            VkSampler metalRoughSampler;
            VkBuffer dataBuffer;
            uint32_t dataBufferOffset;
        };

        DescriptorWriter writer;

        void build_pipelines(VkRender* renderer);
        void clear_resources(VkDevice device);
        MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
    };

    struct MeshNode : public Node {
        std::shared_ptr<MeshAsset> mesh;

        void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
    };

    constexpr unsigned int FRAME_OVERLAP = 2;

    class VkRender {
    public:
        int _frameNumber {0};
        bool _isInitialized;
        VkExtent2D _windowExtent { 1700, 900 };

        bool resize_requested { false };
        bool freeze_rendering { false };

        VkRender(SDL_Window* window);
        ~VkRender();

        void Draw();
        void UpdateScene(const Camera& cam);
        void RequestResize();
        void process_event(SDL_Event& event);
        bool imguiWantsInput() const;

        AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
        AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
        GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
        void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
        void destroy_image(const AllocatedImage& img);
        void destroy_buffer(const AllocatedBuffer& buffer);

        // Default assets (accessed by loader)
        AllocatedImage _whiteImage;
        AllocatedImage _blackImage;
        AllocatedImage _greyImage;
        AllocatedImage _errorCheckerboardImage;

        VkSampler _defaultSamplerLinear;
        VkSampler _defaultSamplerNearest;

        VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

        GLTFMetallic_Roughness metalRoughMaterial;
        TextureCache texCache;

        GPUSceneData sceneData;
        EngineStats stats;
        ResourceManager resourceManager { this };

        struct CachedCameraData {
            glm::vec3 position {};
            float pitch { 0.f };
            float yaw   { 0.f };
        } _cachedCamera;

        DrawContext drawCommands;
        std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

        VkDevice _device;

        // Draw resources (public for pipeline builders)
        AllocatedImage _drawImage;
        AllocatedImage _depthImage;

        // OIT accumulation and revealage images
        AllocatedImage _oitAccumImage;
        AllocatedImage _oitRevealImage;

    private:
        SDL_Window *_window;
        DeletionQueue _mainDeletionQueue;

        VmaAllocator _allocator;

        VkInstance _instance;
        VkDebugUtilsMessengerEXT _debug_messenger;
        VkPhysicalDevice _chosenGPU;
        VkSurfaceKHR _surface;

        // Swap chain
        VkSwapchainKHR _swapchain;
        VkFormat _swapchainImageFormat;
        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;
        VkExtent2D _swapchainExtent;

        // Per-image semaphores — one per swapchain image for each role.
        // Acquire: swap-on-acquire pattern so the semaphore is only reused
        // after the same image is re-acquired (releasing the swapchain's hold).
        // Render: signaled by submit, waited by present — same lifetime rule.
        std::vector<VkSemaphore> _imageAcquireSemaphores;
        VkSemaphore _pendingAcquireSemaphore { VK_NULL_HANDLE };
        std::vector<VkSemaphore> _imageRenderSemaphores;

        // Commands
        FrameData _frames[FRAME_OVERLAP];
        FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }
        FrameData& get_last_frame() { return _frames[(_frameNumber - 1) % FRAME_OVERLAP]; }

        VkQueue _graphicsQueue;
        uint32_t _graphicsQueueFamily;

        // Dedicated transfer queue (may equal graphics if hardware doesn't expose one)
        VkQueue    _transferQueue        { VK_NULL_HANDLE };
        uint32_t   _transferQueueFamily  { 0 };
        bool       _hasDedicatedTransfer { false };
        VkFence         _transferFence         { VK_NULL_HANDLE };
        VkCommandPool   _transferCommandPool   { VK_NULL_HANDLE };
        VkCommandBuffer _transferCommandBuffer { VK_NULL_HANDLE };

        void transfer_submit(std::function<void(VkCommandBuffer)>&& transferFn,
                             std::function<void(VkCommandBuffer)>&& acquireFn);

        // Draw resources
        VkExtent2D _drawExtent;

        DescriptorAllocator globalDescriptorAllocator;

        VkDescriptorSet _drawImageDescriptors;
        VkDescriptorSetLayout _drawImageDescriptorLayout;

        // Background compute
        std::vector<ComputeEffect> backgroundEffects;
        int currentBackgroundEffect { 0 };

        VkPipelineLayout _gradientPipelineLayout;

        // Immediately submit
        VkFence _immFence;
        VkCommandBuffer _immCommandBuffer;
        VkCommandPool _immCommandPool;

        // Default geometry
        GPUMeshBuffers rectangle;
        AllocatedBuffer _defaultGLTFMaterialData;

        void init_vulkan();
        void init_swapchain();
        void init_commands();
        void init_sync_structures();
        void init_descriptors();
        void init_pipelines();
        void init_background_pipelines();
        void init_default_data();
        void init_renderables();

        void create_swapchain(uint32_t width, uint32_t height);
        void destroy_swapchain();
        void resize_swapchain();

        void draw_background(VkCommandBuffer cmd);
        void draw_main(VkCommandBuffer cmd);
        void draw_opaque_geometry(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor);
        void draw_oit_transparent(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor);
        void draw_oit_composite(VkCommandBuffer cmd);

        // OIT pipelines
        VkPipeline            _oitTransparentPipeline        { VK_NULL_HANDLE };
        VkPipeline            _oitCompositePipeline          { VK_NULL_HANDLE };
        VkPipelineLayout      _oitCompositePipelineLayout    { VK_NULL_HANDLE };
        VkDescriptorSetLayout _oitCompositeDescriptorLayout  { VK_NULL_HANDLE };

        // Per-frame image layout tracker
        ImageStateTracker _imageStates;

        void init_oit_pipelines();
        void create_oit_images(VkExtent3D extent);
        void destroy_oit_images();

        // ImGui
        VkDescriptorPool _imguiPool;
        void init_imgui();
        void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
        void draw_imgui_panels();
    };
}
