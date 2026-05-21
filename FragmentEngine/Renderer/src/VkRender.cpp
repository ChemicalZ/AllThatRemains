#include <volk.h>
#include "VkRender.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "LogInternal.h"
#include "Images.h"
#include "Descriptors.h"
#include "Pipelines.h"
#include "Initializers.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"


// VMA is used with Volk/VK_NO_PROTOTYPES. In newer VMA versions, dynamic
// Vulkan functions require vkGetInstanceProcAddr and vkGetDeviceProcAddr to be
// passed through VmaAllocatorCreateInfo::pVulkanFunctions.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <ranges>


constexpr bool bUseValidationLayers = true;

namespace fe {

void vk_check_fail(VkResult err, const char* expr) {
    FE_CORE_CRITICAL("Vulkan error in '{}': {}", expr,
        vk::to_string(static_cast<vk::Result>(err)));
}


// ─── Frustum culling helper ────────────────────────────────────────────────

static bool is_visible(const RenderObject& obj, const glm::mat4& viewproj)
{
    std::array<glm::vec3, 8> corners {
        glm::vec3 { 1, 1, 1 }, glm::vec3 { 1, 1, -1 },
        glm::vec3 { 1, -1, 1 }, glm::vec3 { 1, -1, -1 },
        glm::vec3 { -1, 1, 1 }, glm::vec3 { -1, 1, -1 },
        glm::vec3 { -1, -1, 1 }, glm::vec3 { -1, -1, -1 },
    };

    glm::mat4 matrix = viewproj * obj.transform;
    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);
        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;
        min = glm::min(glm::vec3{v.x, v.y, v.z}, min);
        max = glm::max(glm::vec3{v.x, v.y, v.z}, max);
    }

    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) {
        return false;
    }
    return true;
}

// ─── Constructor / Destructor ──────────────────────────────────────────────

VkRender::VkRender(SDL_Window* window) {
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
    init_default_data();
    init_renderables();
    init_imgui();

    _isInitialized = true;

    FE_CORE_INFO("Vulkan Renderer successfully initialized");
}

VkRender::~VkRender() {
    if (_isInitialized) {
        FE_CORE_TRACE("~VkRender: waiting for GPU idle");
        vkDeviceWaitIdle(_device);

        FE_CORE_TRACE("~VkRender: flushing per-frame deletion queues");
        for (auto & _frame : _frames) {
            _frame._deletionQueue.flush();
        }

        // Destroy all loaded scenes before the VMA allocator is torn down.
        // ~LoadedGLTF() calls clearAll() which frees GPU buffers/images via VMA.
        // _mainDeletionQueue.flush() destroys the allocator, so this must come first.
        FE_CORE_TRACE("~VkRender: clearing {} loaded scene(s)", loadedScenes.size());
        loadedScenes.clear();

        FE_CORE_TRACE("~VkRender: flushing main deletion queue ({} entries)",
            _mainDeletionQueue.deletors.size());
        _mainDeletionQueue.flush();

        FE_CORE_TRACE("~VkRender: destroying swapchain");
        destroy_swapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);
        vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
        vkDestroyInstance(_instance, nullptr);
    }
}

// ─── Draw ─────────────────────────────────────────────────────────────────

void VkRender::Draw() {
    auto drawStart = std::chrono::system_clock::now();

    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_device);

    // OIT images are cleared by the OIT pass each frame; treat them as UNDEFINED each frame
    _imageStates.reset(_oitAccumImage.image);
    _imageStates.reset(_oitRevealImage.image);

    // Signal the pending acquire semaphore; after getting the image index,
    // swap it into the per-image slot so the swapchain's hold on the old
    // semaphore is released (we just re-acquired that image).
    uint32_t swapchainImageIndex;
    VkSemaphore acquireSem = _pendingAcquireSemaphore;
    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, acquireSem, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
        resize_requested = true;
        if (e == VK_ERROR_OUT_OF_DATE_KHR) return;
    }
    _pendingAcquireSemaphore = _imageAcquireSemaphores[swapchainImageIndex];
    _imageAcquireSemaphores[swapchainImageIndex] = acquireSem;

    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height);
    _drawExtent.width  = std::min(_swapchainExtent.width,  _drawImage.imageExtent.width);

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    draw_imgui_panels();
    ImGui::Render();

    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    draw_main(cmd);

    transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);
    transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSemaphore renderSem = _imageRenderSemaphores[swapchainImageIndex];

    VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo   = semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, _imageAcquireSemaphores[swapchainImageIndex]);
    VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, renderSem);
    VkSubmitInfo2 submit = submit_info(&cmdinfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

    VkPresentInfoKHR presentInfo = present_info();
    presentInfo.pSwapchains        = &_swapchain;
    presentInfo.swapchainCount     = 1;
    presentInfo.pWaitSemaphores    = &renderSem;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices      = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        resize_requested = true;
    }

    auto drawEnd = std::chrono::system_clock::now();
    stats.frameTime = std::chrono::duration_cast<std::chrono::microseconds>(drawEnd - drawStart).count();

    _frameNumber++;
}

void VkRender::UpdateScene(const Camera& cam) {
    _cachedCamera.position = cam.position;
    _cachedCamera.pitch    = cam.pitch;
    _cachedCamera.yaw      = cam.yaw;

    glm::mat4 view = cam.getViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(70.f),
        static_cast<float>(_windowExtent.width) / static_cast<float>(_windowExtent.height),
        10000.f, 0.1f);
    projection[1][1] *= -1;

    sceneData.view     = view;
    sceneData.proj     = projection;
    sceneData.viewproj = projection * view;

    sceneData.ambientColor      = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    sceneData.sunlightDirection = glm::vec4(0.f, 1.f, 0.5f, 1.0f);
    sceneData.sunlightColor     = glm::vec4(1.0f);

    for (auto& scene : loadedScenes | std::views::values) {
        scene->Draw(scene->worldTransform, drawCommands);
    }
}

void VkRender::RequestResize() {
    resize_swapchain();
}

// ─── Draw passes ──────────────────────────────────────────────────────────

void VkRender::draw_background(VkCommandBuffer cmd) {
    ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);
    vkCmdDispatch(cmd, static_cast<uint32_t>(std::ceil(_drawExtent.width / 16.0)), static_cast<uint32_t>(std::ceil(_drawExtent.height / 16.0)), 1);
}

void VkRender::draw_main(VkCommandBuffer cmd) {
    auto start = std::chrono::system_clock::now();

    draw_background(cmd);

    // ── Scene data + global descriptor (shared by all mesh passes) ────────────
    AllocatedBuffer& gpuSceneDataBuffer = get_current_frame()._sceneDataBuffer;
    *static_cast<GPUSceneData*>(gpuSceneDataBuffer.info.pMappedData) = sceneData;

    VkDescriptorSetVariableDescriptorCountAllocateInfo allocArrayInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO
    };
    auto descriptorCounts = static_cast<uint32_t>(texCache.Cache.size());
    allocArrayInfo.pDescriptorCounts  = &descriptorCounts;
    allocArrayInfo.descriptorSetCount = 1;

    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(
        _device, _gpuSceneDataDescriptorLayout, &allocArrayInfo);

    DescriptorWriter dw;
    dw.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    if (!texCache.Cache.empty()) {
        VkWriteDescriptorSet arraySet {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        arraySet.descriptorCount = static_cast<uint32_t>(texCache.Cache.size());
        arraySet.dstArrayElement = 0;
        arraySet.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        arraySet.dstBinding      = 1;
        arraySet.pImageInfo      = texCache.Cache.data();
        dw.writes.push_back(arraySet);
    }
    dw.update_set(_device, globalDescriptor);

    stats.drawcall_count = 0;
    stats.triangle_count = 0;

    // ── Opaque pass ───────────────────────────────────────────────────────────
    VkRenderingAttachmentInfo colorAtt  = attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_GENERAL);
    VkRenderingAttachmentInfo depthAtt  = depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingInfo           opaqueRI  = rendering_info(_drawExtent, &colorAtt, &depthAtt);
    vkCmdBeginRendering(cmd, &opaqueRI);
    draw_opaque_geometry(cmd, globalDescriptor);
    vkCmdEndRendering(cmd);

    // ── OIT transparent pass ──────────────────────────────────────────────────
    transition_image(cmd, _oitAccumImage.image,
        _imageStates.get(_oitAccumImage.image), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    _imageStates.set(_oitAccumImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    transition_image(cmd, _oitRevealImage.image,
        _imageStates.get(_oitRevealImage.image), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    _imageStates.set(_oitRevealImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    transition_image(cmd, _depthImage.image,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

    draw_oit_transparent(cmd, globalDescriptor);

    // ── OIT composite pass ────────────────────────────────────────────────────
    transition_image(cmd, _oitAccumImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    _imageStates.set(_oitAccumImage.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    transition_image(cmd, _oitRevealImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    _imageStates.set(_oitRevealImage.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    draw_oit_composite(cmd);

    drawCommands.OpaqueSurfaces.clear();
    drawCommands.TransparentSurfaces.clear();

    auto end = std::chrono::system_clock::now();
    stats.mesh_draw_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.f;
}

// Opaque draw helper — batches by pipeline/material/index buffer
static void record_draw_object(
    VkCommandBuffer cmd, const RenderObject& r,
    VkDescriptorSet globalDescriptor, VkExtent2D extent,
    MaterialPipeline*& lastPipeline, MaterialInstance*& lastMaterial, VkBuffer& lastIndexBuffer,
    EngineStats& stats)
{
    if (r.material != lastMaterial) {
        lastMaterial = r.material;
        if (r.material->pipeline != lastPipeline) {
            lastPipeline = r.material->pipeline;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 0, 1, &globalDescriptor, 0, nullptr);

            VkViewport viewport {};
            viewport.width    = static_cast<float>(extent.width);
            viewport.height   = static_cast<float>(extent.height);
            viewport.minDepth = 0.f;
            viewport.maxDepth = 1.f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor {};
            scissor.extent = extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 1, 1, &r.material->materialSet, 0, nullptr);
    }
    if (r.indexBuffer != lastIndexBuffer) {
        lastIndexBuffer = r.indexBuffer;
        vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }

    GPUDrawPushConstants pc {};
    pc.worldMatrix  = r.transform;
    pc.vertexBuffer = r.vertexBufferAddress;
    vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pc);

    stats.drawcall_count++;
    stats.triangle_count += r.indexCount / 3;
    vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
}

void VkRender::draw_opaque_geometry(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor) {
    MaterialPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    VkBuffer          lastIndex    = VK_NULL_HANDLE;

    for (int i = 0; i < static_cast<int>(drawCommands.OpaqueSurfaces.size()); i++) {
        const RenderObject& r = drawCommands.OpaqueSurfaces[i];
        if (is_visible(r, sceneData.viewproj)) {
            record_draw_object(cmd, r, globalDescriptor, _drawExtent,
                lastPipeline, lastMaterial, lastIndex, stats);
        }
    }
}

void VkRender::draw_oit_transparent(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor) {
    VkClearValue accumClear  { .color = {.float32 = {0.f, 0.f, 0.f, 0.f}} };
    VkClearValue revealClear { .color = {.float32 = {1.f, 0.f, 0.f, 0.f}} };

    VkRenderingAttachmentInfo accumAtt  = attachment_info(_oitAccumImage.imageView,  &accumClear,  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo revealAtt = attachment_info(_oitRevealImage.imageView, &revealClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAtt  = depth_attachment_info_readonly(_depthImage.imageView);

    std::array<VkRenderingAttachmentInfo, 2> colorAtts { accumAtt, revealAtt };

    VkRenderingInfo ri { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea             = VkRect2D{ {0,0}, _drawExtent };
    ri.layerCount             = 1;
    ri.colorAttachmentCount   = static_cast<uint32_t>(colorAtts.size());
    ri.pColorAttachments      = colorAtts.data();
    ri.pDepthAttachment       = &depthAtt;

    vkCmdBeginRendering(cmd, &ri);

    // OIT pipeline is fixed for all transparent surfaces; reuse mesh layout for descriptors
    VkPipelineLayout oitLayout = metalRoughMaterial.opaquePipeline.layout;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _oitTransparentPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitLayout, 0, 1, &globalDescriptor, 0, nullptr);

    VkViewport viewport {};
    viewport.width    = static_cast<float>(_drawExtent.width);
    viewport.height   = static_cast<float>(_drawExtent.height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor {};
    scissor.extent = _drawExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    MaterialInstance* lastMaterial = nullptr;
    VkBuffer          lastIndex    = VK_NULL_HANDLE;

    for (const RenderObject& r : drawCommands.TransparentSurfaces) {
        if (r.material != lastMaterial) {
            lastMaterial = r.material;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitLayout, 1, 1, &r.material->materialSet, 0, nullptr);
        }
        if (r.indexBuffer != lastIndex) {
            lastIndex = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
        GPUDrawPushConstants pc {};
        pc.worldMatrix  = r.transform;
        pc.vertexBuffer = r.vertexBufferAddress;
        vkCmdPushConstants(cmd, oitLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pc);
        stats.drawcall_count++;
        stats.triangle_count += r.indexCount / 3;
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
    }

    vkCmdEndRendering(cmd);
}

void VkRender::draw_oit_composite(VkCommandBuffer cmd) {
    // Allocate descriptor for accum + reveal textures
    VkDescriptorSet oitSet = get_current_frame()._frameDescriptors.allocate(_device, _oitCompositeDescriptorLayout);

    VkDescriptorImageInfo accumInfo {
        .sampler     = _defaultSamplerNearest,
        .imageView   = _oitAccumImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo revealInfo {
        .sampler     = _defaultSamplerNearest,
        .imageView   = _oitRevealImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    DescriptorWriter dw;
    dw.write_image(0, _oitAccumImage.imageView,  _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    dw.write_image(1, _oitRevealImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    dw.update_set(_device, oitSet);

    VkRenderingAttachmentInfo colorAtt = attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_GENERAL);
    VkRenderingInfo ri = rendering_info(_drawExtent, &colorAtt, nullptr);
    vkCmdBeginRendering(cmd, &ri);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _oitCompositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _oitCompositePipelineLayout, 0, 1, &oitSet, 0, nullptr);

    VkViewport viewport {};
    viewport.width    = static_cast<float>(_drawExtent.width);
    viewport.height   = static_cast<float>(_drawExtent.height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor {};
    scissor.extent = _drawExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0); // fullscreen triangle

    vkCmdEndRendering(cmd);
}

// ─── Immediate submit ──────────────────────────────────────────────────────

void VkRender::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer cmd = _immCommandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = submit_info(&cmdinfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

// ─── Transfer submit ──────────────────────────────────────────────────────

void VkRender::transfer_submit(
    std::function<void(VkCommandBuffer)>&& transferFn,
    std::function<void(VkCommandBuffer)>&& acquireFn)
{
    // Phase 1: copy + release barriers on the dedicated transfer queue
    VK_CHECK(vkResetFences(_device, 1, &_transferFence));
    VK_CHECK(vkResetCommandBuffer(_transferCommandBuffer, 0));

    VkCommandBufferBeginInfo begin = command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(_transferCommandBuffer, &begin));
    transferFn(_transferCommandBuffer);
    VK_CHECK(vkEndCommandBuffer(_transferCommandBuffer));

    VkCommandBufferSubmitInfo xferCmd = command_buffer_submit_info(_transferCommandBuffer);
    VkSubmitInfo2 xferSubmit = submit_info(&xferCmd, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(_transferQueue, 1, &xferSubmit, _transferFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_transferFence, true, 9999999999));

    // Phase 2: acquire barriers on the graphics queue
    immediate_submit(std::move(acquireFn));
}

// ─── Buffer / Image / Mesh creation ───────────────────────────────────────

AllocatedBuffer VkRender::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = allocSize;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer newBuffer{};
    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
    return newBuffer;
}

AllocatedImage VkRender::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
    AllocatedImage newImage{};
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = image_create_info(format, usage, size);
    if (mipmapped) {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage         = VMA_MEMORY_USAGE_GPU_ONLY;
    allocinfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlag = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewCreateInfo view_info = imageview_create_info(format, newImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;
    VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedImage VkRender::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
    size_t data_size = static_cast<size_t>(size.depth) * size.width * size.height * 4;
    AllocatedBuffer uploadbuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    memcpy(uploadbuffer.info.pMappedData, data, data_size);

    AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

    VkBufferImageCopy copyRegion = {};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = size;

    const VkImageSubresourceRange mip0 { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (_hasDedicatedTransfer && !mipmapped) {
        transfer_submit(
            [&](VkCommandBuffer cmd) {
                // Transition + copy on transfer queue
                transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

                // Release ownership: transfer → graphics, layout stays TRANSFER_DST
                VkImageMemoryBarrier2 release {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .dstStageMask        = VK_PIPELINE_STAGE_2_NONE,
                    .dstAccessMask       = VK_ACCESS_2_NONE,
                    .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = _transferQueueFamily,
                    .dstQueueFamilyIndex = _graphicsQueueFamily,
                    .image               = new_image.image,
                    .subresourceRange    = mip0,
                };
                VkDependencyInfo dep { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &release };
                vkCmdPipelineBarrier2(cmd, &dep);
            },
            [&](VkCommandBuffer cmd) {
                // Acquire ownership on graphics queue
                VkImageMemoryBarrier2 acquire {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
                    .srcAccessMask       = VK_ACCESS_2_NONE,
                    .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
                    .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = _transferQueueFamily,
                    .dstQueueFamilyIndex = _graphicsQueueFamily,
                    .image               = new_image.image,
                    .subresourceRange    = mip0,
                };
                VkDependencyInfo dep { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &acquire };
                vkCmdPipelineBarrier2(cmd, &dep);
            }
        );
    } else {
        // Fallback: single graphics-queue submit (also used for mipmapped images
        // since vkCmdBlitImage requires the graphics queue)
        immediate_submit([&](VkCommandBuffer cmd) {
            transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
            if (mipmapped) {
                generate_mipmaps(cmd, new_image.image, VkExtent2D{new_image.imageExtent.width, new_image.imageExtent.height});
            } else {
                transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        });
    }

    destroy_buffer(uploadbuffer);
    return new_image;
}

GPUMeshBuffers VkRender::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize  = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface{};
    newSurface.vertexBuffer = create_buffer(vertexBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    VkBufferDeviceAddressInfo deviceAddressInfo {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = newSurface.vertexBuffer.buffer};
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

    newSurface.indexBuffer = create_buffer(indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging = create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* mappedData = staging.info.pMappedData;

    memcpy(mappedData, vertices.data(), vertexBufferSize);
    memcpy(static_cast<char*>(mappedData) + vertexBufferSize, indices.data(), indexBufferSize);

    if (_hasDedicatedTransfer) {
        transfer_submit(
            [&](VkCommandBuffer cmd) {
                VkBufferCopy vertexCopy { 0, 0, vertexBufferSize };
                vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);
                VkBufferCopy indexCopy { vertexBufferSize, 0, indexBufferSize };
                vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);

                // Release ownership to graphics queue
                std::array<VkBufferMemoryBarrier2, 2> release {{
                    { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                      .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      .dstStageMask = VK_PIPELINE_STAGE_2_NONE, .dstAccessMask = VK_ACCESS_2_NONE,
                      .srcQueueFamilyIndex = _transferQueueFamily, .dstQueueFamilyIndex = _graphicsQueueFamily,
                      .buffer = newSurface.vertexBuffer.buffer, .offset = 0, .size = VK_WHOLE_SIZE },
                    { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                      .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      .dstStageMask = VK_PIPELINE_STAGE_2_NONE, .dstAccessMask = VK_ACCESS_2_NONE,
                      .srcQueueFamilyIndex = _transferQueueFamily, .dstQueueFamilyIndex = _graphicsQueueFamily,
                      .buffer = newSurface.indexBuffer.buffer, .offset = 0, .size = VK_WHOLE_SIZE },
                }};
                VkDependencyInfo dep { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .bufferMemoryBarrierCount = 2, .pBufferMemoryBarriers = release.data() };
                vkCmdPipelineBarrier2(cmd, &dep);
            },
            [&](VkCommandBuffer cmd) {
                // Acquire ownership on graphics queue
                std::array<VkBufferMemoryBarrier2, 2> acquire {{
                    { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                      .srcStageMask = VK_PIPELINE_STAGE_2_NONE, .srcAccessMask = VK_ACCESS_2_NONE,
                      .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                      .srcQueueFamilyIndex = _transferQueueFamily, .dstQueueFamilyIndex = _graphicsQueueFamily,
                      .buffer = newSurface.vertexBuffer.buffer, .offset = 0, .size = VK_WHOLE_SIZE },
                    { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                      .srcStageMask = VK_PIPELINE_STAGE_2_NONE, .srcAccessMask = VK_ACCESS_2_NONE,
                      .dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                      .dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT,
                      .srcQueueFamilyIndex = _transferQueueFamily, .dstQueueFamilyIndex = _graphicsQueueFamily,
                      .buffer = newSurface.indexBuffer.buffer, .offset = 0, .size = VK_WHOLE_SIZE },
                }};
                VkDependencyInfo dep { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .bufferMemoryBarrierCount = 2, .pBufferMemoryBarriers = acquire.data() };
                vkCmdPipelineBarrier2(cmd, &dep);
            }
        );
    } else {
        immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferCopy vertexCopy { 0, 0, vertexBufferSize };
            vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);
            VkBufferCopy indexCopy { vertexBufferSize, 0, indexBufferSize };
            vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
        });
    }

    destroy_buffer(staging);
    return newSurface;
}

void VkRender::destroy_image(const AllocatedImage& img) {
    if (img.image == VK_NULL_HANDLE) return;
    FE_CORE_TRACE("destroy_image  view={} image={} alloc={}",
        (void*)img.imageView, (void*)img.image, (void*)img.allocation);
    vkDestroyImageView(_device, img.imageView, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
}

void VkRender::destroy_buffer(const AllocatedBuffer& buffer) {
    if (buffer.buffer == VK_NULL_HANDLE) return;
    FE_CORE_TRACE("destroy_buffer buf={} alloc={}",
        (void*)buffer.buffer, (void*)buffer.allocation);
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

// ─── ImGui ────────────────────────────────────────────────────────────────

void VkRender::init_imgui() {
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets       = 1000;
    pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes    = pool_sizes;
    VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &_imguiPool));

    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL3_InitForVulkan(_window);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion      = VK_API_VERSION_1_3;
    init_info.Instance        = _instance;
    init_info.PhysicalDevice  = _chosenGPU;
    init_info.Device          = _device;
    init_info.QueueFamily     = _graphicsQueueFamily;
    init_info.Queue           = _graphicsQueue;
    init_info.DescriptorPool  = _imguiPool;
    init_info.MinImageCount   = 3;
    init_info.ImageCount      = 3;
    init_info.UseDynamicRendering = true;

    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

    ImGui_ImplVulkan_Init(&init_info);
    // Font texture is created automatically on the first NewFrame() call in imgui 1.92+

    _mainDeletionQueue.push_function([this]() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(_device, _imguiPool, nullptr);
    });

    FE_CORE_INFO("ImGui initialized");
}

void VkRender::process_event(SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool VkRender::imguiWantsInput() const {
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantCaptureMouse;
}

void VkRender::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView) {
    VkRenderingAttachmentInfo colorAttachment = attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = rendering_info(_swapchainExtent, &colorAttachment, nullptr);
    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void VkRender::draw_imgui_panels() {
    // ── Performance ──────────────────────────────────────────────────────────
    if (ImGui::Begin("Performance")) {
        float frametime_ms = static_cast<float>(stats.frameTime) / 1000.f;
        float fps = (stats.frameTime > 0) ? (1000000.f / static_cast<float>(stats.frameTime)) : 0.f;

        ImGui::Text("FPS            %.1f", fps);
        ImGui::Text("Frame time     %.3f ms", frametime_ms);
        ImGui::Text("Mesh draw time %.3f ms", stats.mesh_draw_time);
        ImGui::Separator();
        ImGui::Text("Draw calls  %d", stats.drawcall_count);
        ImGui::Text("Triangles   %u", stats.triangle_count);

        static std::array<float, 128> frameTimes{};
        static int ftOffset = 0;
        frameTimes[ftOffset] = frametime_ms;
        ftOffset = (ftOffset + 1) % static_cast<int>(frameTimes.size());

        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f ms", frametime_ms);
        ImGui::PlotLines("##ft", frameTimes.data(), static_cast<int>(frameTimes.size()),
                         ftOffset, overlay, 0.f, 50.f, ImVec2(0.f, 80.f));
    }
    ImGui::End();

    // ── Background Effects ───────────────────────────────────────────────────
    if (ImGui::Begin("Background")) {
        int effectCount = static_cast<int>(backgroundEffects.size());
        if (effectCount > 0) {
            ImGui::SliderInt("Effect", &currentBackgroundEffect, 0, effectCount - 1);
            ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];
            ImGui::Text("Name: %s", effect.name);
            ImGui::Separator();
            if (std::strcmp(effect.name, "gradient") == 0) {
                ImGui::ColorEdit4("Top color",    &effect.data.data1.x);
                ImGui::ColorEdit4("Bottom color", &effect.data.data2.x);
            } else if (std::strcmp(effect.name, "sky") == 0) {
                ImGui::DragFloat4("Sky params", &effect.data.data1.x, 0.005f, 0.f, 1.f);
            } else {
                ImGui::ColorEdit4("Data 1", &effect.data.data1.x);
                ImGui::ColorEdit4("Data 2", &effect.data.data2.x);
            }
        }
    }
    ImGui::End();

    // ── Scene / Camera ───────────────────────────────────────────────────────
    if (ImGui::Begin("Scene")) {
        ImGui::SeparatorText("Camera");
        ImGui::Text("Position  (%.2f, %.2f, %.2f)",
                    _cachedCamera.position.x, _cachedCamera.position.y, _cachedCamera.position.z);
        ImGui::Text("Pitch %.2f  Yaw %.2f", _cachedCamera.pitch, _cachedCamera.yaw);

        ImGui::SeparatorText("Renderer");
        ImGui::Checkbox("Freeze Rendering", &freeze_rendering);
        ImGui::Text("Draw extent  %u x %u", _drawExtent.width, _drawExtent.height);
        ImGui::Text("Frame #%d", _frameNumber);

        ImGui::SeparatorText("Loaded Scenes");
        {
            std::string toRemove;
            if (loadedScenes.empty()) {
                ImGui::TextDisabled("(none)");
            } else {
                for (auto& [name, scene] : loadedScenes) {
                    ImGui::Text("%s", name.c_str());
                    ImGui::SameLine();
                    ImGui::PushID(name.c_str());
                    if (ImGui::SmallButton("-")) {
                        toRemove = name;
                    }
                    ImGui::PopID();
                }
            }
            if (!toRemove.empty()) {
                loadedScenes.erase(toRemove);
                drawCommands.OpaqueSurfaces.clear();
                drawCommands.TransparentSurfaces.clear();
            }
        }

        ImGui::SeparatorText("Add Object");
        {
            static constexpr const char* kAssets[] = {
                "basicmesh.glb",
                "house.glb",
                "house2.glb",
                "monkey.glb",
                "monkey2.glb",
                "monkeyHD.glb",
                "structure.glb",
                "structure_mat.glb",
            };
            static int   selectedAsset  = 0;
            static int   spawnCounter   = 0;
            static float spawnDistance  = 5.f;
            static char  statusMsg[64]  = "";

            ImGui::Combo("Asset", &selectedAsset, kAssets, IM_ARRAYSIZE(kAssets));
            ImGui::SliderFloat("Spawn Distance", &spawnDistance, 0.5f, 100.f, "%.1f");

            if (ImGui::Button("Add to Scene")) {
                std::string key  = std::string(kAssets[selectedAsset]) + "#" + std::to_string(++spawnCounter);
                std::string path = std::string("../assets/") + kAssets[selectedAsset];
                auto result = loadGltf(this, path);
                if (result.has_value()) {
                    // Camera forward from cached pitch/yaw
                    const float cp      = std::cos(_cachedCamera.pitch);
                    const float sp      = std::sin(_cachedCamera.pitch);
                    const float cy      = std::cos(_cachedCamera.yaw);
                    const float sy      = std::sin(_cachedCamera.yaw);
                    const glm::vec3 fwd { cp * sy, sp, -cp * cy };

                    // Place object in front of camera, rotated to face back toward it.
                    // Object yaw = camera yaw + π (same -Y axis convention as Camera).
                    const glm::vec3 spawnPos   = _cachedCamera.position + fwd * spawnDistance;
                    const float     spawnYaw   = _cachedCamera.yaw + glm::pi<float>();
                    const glm::mat4 spawnXform =
                        glm::translate(glm::mat4(1.f), spawnPos) *
                        glm::rotate(glm::mat4(1.f), spawnYaw, glm::vec3{0.f, -1.f, 0.f});

                    setSceneTransform(**result, spawnXform);
                    loadedScenes[key] = *result;
                    snprintf(statusMsg, sizeof(statusMsg), "Added %s", key.c_str());
                } else {
                    snprintf(statusMsg, sizeof(statusMsg), "Failed to load %s", kAssets[selectedAsset]);
                    --spawnCounter;
                }
            }
            if (statusMsg[0] != '\0') {
                ImGui::TextDisabled("%s", statusMsg);
            }
        }
    }
    ImGui::End();

    // ── Logging ──────────────────────────────────────────────────────────────
    if (ImGui::Begin("Logging")) {
        static constexpr const char* kLevelNames[] = {
            "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"
        };
        static constexpr spdlog::level::level_enum kLevels[] = {
            spdlog::level::trace,
            spdlog::level::debug,
            spdlog::level::info,
            spdlog::level::warn,
            spdlog::level::err,
            spdlog::level::critical,
            spdlog::level::off,
        };
        static_assert(IM_ARRAYSIZE(kLevelNames) == IM_ARRAYSIZE(kLevels));

        auto logger = fe::LogInternal::GetCoreLogger();

        // Find the index matching the logger's current level
        int current = 2; // default Info
        for (int i = 0; i < IM_ARRAYSIZE(kLevels); ++i) {
            if (logger->level() == kLevels[i]) { current = i; break; }
        }

        if (ImGui::Combo("Level", &current, kLevelNames, IM_ARRAYSIZE(kLevelNames))) {
            logger->set_level(kLevels[current]);
            FE_CORE_INFO("Log level changed to {}", kLevelNames[current]);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Trace/Debug: resource lifecycle, cache ops");
        ImGui::TextDisabled("Info:  normal engine events (default)");
        ImGui::TextDisabled("Warn+: errors and critical failures only");
    }
    ImGui::End();
}

// ─── OIT helpers ──────────────────────────────────────────────────────────

void VkRender::create_oit_images(VkExtent3D extent) {
    constexpr VkImageUsageFlags oitUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VmaAllocationCreateInfo gpuOnly {};
    gpuOnly.usage         = VMA_MEMORY_USAGE_GPU_ONLY;
    gpuOnly.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Accumulation (RGBA16F)
    _oitAccumImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _oitAccumImage.imageExtent = extent;
    {
        VkImageCreateInfo info = image_create_info(_oitAccumImage.imageFormat, oitUsage, extent);
        VK_CHECK(vmaCreateImage(_allocator, &info, &gpuOnly, &_oitAccumImage.image, &_oitAccumImage.allocation, nullptr));
        VkImageViewCreateInfo view = imageview_create_info(_oitAccumImage.imageFormat, _oitAccumImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK(vkCreateImageView(_device, &view, nullptr, &_oitAccumImage.imageView));
    }

    // Revealage (R16F)
    _oitRevealImage.imageFormat = VK_FORMAT_R16_SFLOAT;
    _oitRevealImage.imageExtent = extent;
    {
        VkImageCreateInfo info = image_create_info(_oitRevealImage.imageFormat, oitUsage, extent);
        VK_CHECK(vmaCreateImage(_allocator, &info, &gpuOnly, &_oitRevealImage.image, &_oitRevealImage.allocation, nullptr));
        VkImageViewCreateInfo view = imageview_create_info(_oitRevealImage.imageFormat, _oitRevealImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK(vkCreateImageView(_device, &view, nullptr, &_oitRevealImage.imageView));
    }

    // Mark both as UNDEFINED so first transition knows the true initial layout
    _imageStates.reset(_oitAccumImage.image);
    _imageStates.reset(_oitRevealImage.image);

    FE_CORE_TRACE("create_oit_images {}x{} — accum={} reveal={}",
        extent.width, extent.height,
        (void*)_oitAccumImage.image, (void*)_oitRevealImage.image);
}

void VkRender::destroy_oit_images() {
    FE_CORE_TRACE("destroy_oit_images — accum={} reveal={}",
        (void*)_oitAccumImage.image, (void*)_oitRevealImage.image);
    if (_oitAccumImage.image != VK_NULL_HANDLE) {
        vkDestroyImageView(_device, _oitAccumImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _oitAccumImage.image, _oitAccumImage.allocation);
        _oitAccumImage = {};
    }
    if (_oitRevealImage.image != VK_NULL_HANDLE) {
        vkDestroyImageView(_device, _oitRevealImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _oitRevealImage.image, _oitRevealImage.allocation);
        _oitRevealImage = {};
    }
}

void VkRender::init_oit_pipelines() {
    // ── OIT transparent pipeline ──────────────────────────────────────────────
    VkShaderModule vertShader, oitFragShader;
    if (!load_shader_module("../shaders/mesh.vert.spv", _device, &vertShader))
        FE_CORE_CRITICAL("Failed to load mesh.vert.spv for OIT");
    if (!load_shader_module("../shaders/mesh_oit.frag.spv", _device, &oitFragShader))
        FE_CORE_CRITICAL("Failed to load mesh_oit.frag.spv");

    // Accum attachment: additive (ONE, ONE)
    VkPipelineColorBlendAttachmentState accumBlend {};
    accumBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    accumBlend.blendEnable         = VK_TRUE;
    accumBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    accumBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    accumBlend.colorBlendOp        = VK_BLEND_OP_ADD;
    accumBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    accumBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    accumBlend.alphaBlendOp        = VK_BLEND_OP_ADD;

    // Reveal attachment: multiplicative (ZERO, ONE_MINUS_SRC_COLOR)
    // result.r = 0 + dst.r * (1 - src.r)  →  product(1 - alpha)
    VkPipelineColorBlendAttachmentState revealBlend {};
    revealBlend.colorWriteMask     = VK_COLOR_COMPONENT_R_BIT;
    revealBlend.blendEnable        = VK_TRUE;
    revealBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    revealBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    revealBlend.colorBlendOp       = VK_BLEND_OP_ADD;
    revealBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    revealBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    revealBlend.alphaBlendOp       = VK_BLEND_OP_ADD;

    PipelineBuilder pb;
    pb.set_shaders(vertShader, oitFragShader);
    pb.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pb.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pb.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pb.set_multisampling_none();
    pb.set_color_attachments(
        { _oitAccumImage.imageFormat, _oitRevealImage.imageFormat },
        { accumBlend, revealBlend });
    pb.set_depth_format(_depthImage.imageFormat);
    pb.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL); // test but no write
    pb._pipelineLayout = metalRoughMaterial.opaquePipeline.layout; // reuse same layout

    _oitTransparentPipeline = pb.build_pipeline(_device);

    vkDestroyShaderModule(_device, vertShader, nullptr);
    vkDestroyShaderModule(_device, oitFragShader, nullptr);

    // ── OIT composite pipeline ────────────────────────────────────────────────
    VkShaderModule fsVert, compositeFragShader;
    if (!load_shader_module("../shaders/fullscreen.vert.spv", _device, &fsVert))
        FE_CORE_CRITICAL("Failed to load fullscreen.vert.spv");
    if (!load_shader_module("../shaders/oit_composite.frag.spv", _device, &compositeFragShader))
        FE_CORE_CRITICAL("Failed to load oit_composite.frag.spv");

    // Composite descriptor layout: set 0 = accum (binding 0) + reveal (binding 1)
    {
        DescriptorLayoutBuilder lb;
        lb.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        lb.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _oitCompositeDescriptorLayout = lb.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    VkPipelineLayoutCreateInfo compositeLayoutInfo = pipeline_layout_create_info();
    compositeLayoutInfo.setLayoutCount = 1;
    compositeLayoutInfo.pSetLayouts    = &_oitCompositeDescriptorLayout;
    VK_CHECK(vkCreatePipelineLayout(_device, &compositeLayoutInfo, nullptr, &_oitCompositePipelineLayout));

    // Alpha blend: composite transparent layer over opaque
    VkPipelineColorBlendAttachmentState compositeBlend {};
    compositeBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    compositeBlend.blendEnable         = VK_TRUE;
    compositeBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    compositeBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    compositeBlend.colorBlendOp        = VK_BLEND_OP_ADD;
    compositeBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    compositeBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    compositeBlend.alphaBlendOp        = VK_BLEND_OP_ADD;

    pb.clear();
    pb.set_shaders(fsVert, compositeFragShader);
    pb.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pb.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pb.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pb.set_multisampling_none();
    pb.set_color_attachment_format(_drawImage.imageFormat);
    pb._colorBlendAttachment = compositeBlend;
    pb.disable_depthtest();
    pb._pipelineLayout = _oitCompositePipelineLayout;

    _oitCompositePipeline = pb.build_pipeline(_device);

    vkDestroyShaderModule(_device, fsVert, nullptr);
    vkDestroyShaderModule(_device, compositeFragShader, nullptr);

    _mainDeletionQueue.push_function([this]() {
        vkDestroyPipeline(_device, _oitTransparentPipeline, nullptr);
        vkDestroyPipeline(_device, _oitCompositePipeline, nullptr);
        vkDestroyPipelineLayout(_device, _oitCompositePipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _oitCompositeDescriptorLayout, nullptr);
    });
}

// ─── Init functions ────────────────────────────────────────────────────────

void VkRender::init_vulkan() {
    VK_CHECK(volkInitialize());

    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("FragmentEngine Application")
        .request_validation_layers(bUseValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    vkb::Instance vkb_inst = inst_ret.value();
    _instance        = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    volkLoadInstance(_instance);

    if (!SDL_Vulkan_CreateSurface(_window, _instance, nullptr, &_surface)) {
        throw std::runtime_error("SDL_Vulkan_CreateSurface failed: " + std::string(SDL_GetError()));
    }

    VkPhysicalDeviceVulkan13Features features13 {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12 {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress                       = true;
    features12.descriptorIndexing                        = true;
    features12.descriptorBindingPartiallyBound           = true;
    features12.descriptorBindingVariableDescriptorCount  = true;
    features12.runtimeDescriptorArray                    = true;

    VkPhysicalDeviceFeatures baseFeatures {};
    baseFeatures.independentBlend = VK_TRUE; // needed for WBOIT (different blend per attachment)

    vkb::PhysicalDeviceSelector selector { vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .set_required_features(baseFeatures)
        .set_surface(_surface)
        .select()
        .value();

    vkb::DeviceBuilder deviceBuilder { physicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();

    _device    = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;

    volkLoadDevice(_device);

    _graphicsQueue       = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    {
        auto tq  = vkbDevice.get_queue(vkb::QueueType::transfer);
        auto tqi = vkbDevice.get_queue_index(vkb::QueueType::transfer);
        if (tq && tqi) {
            _transferQueue       = tq.value();
            _transferQueueFamily = tqi.value();
            _hasDedicatedTransfer = (_transferQueueFamily != _graphicsQueueFamily);
        } else {
            _transferQueue       = _graphicsQueue;
            _transferQueueFamily = _graphicsQueueFamily;
        }
        FE_CORE_INFO("Transfer queue family: {} (dedicated: {})",
            _transferQueueFamily, _hasDedicatedTransfer);
    }

    VmaVulkanFunctions vmaFunctions = {};
    vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFunctions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice   = _chosenGPU;
    allocatorInfo.device           = _device;
    allocatorInfo.instance         = _instance;
    allocatorInfo.flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.pVulkanFunctions = &vmaFunctions;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &_allocator));

    _mainDeletionQueue.push_function([this]() {
        vmaDestroyAllocator(_allocator);
    });
    FE_CORE_INFO("Vulkan initialized");
}

void VkRender::init_swapchain() {
    create_swapchain(_windowExtent.width, _windowExtent.height);

    VkExtent3D drawImageExtent { _windowExtent.width, _windowExtent.height, 1 };

    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo rimg_info = image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage         = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr));

    VkImageViewCreateInfo rview_info = imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;

    VkImageUsageFlags depthImageUsages = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImageCreateInfo dimg_info = image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);
    VK_CHECK(vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr));

    VkImageViewCreateInfo dview_info = imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));

    _mainDeletionQueue.push_function([this]() {
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
        destroy_oit_images();
    });

    create_oit_images(drawImageExtent);
}

void VkRender::init_commands() {
    VkCommandPoolCreateInfo commandPoolInfo = command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (auto & _frame : _frames) {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frame._commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(_frame._commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frame._mainCommandBuffer));

        _mainDeletionQueue.push_function([this, &_frame]() {
            vkDestroyCommandPool(_device, _frame._commandPool, nullptr);
        });
    }

    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));
    VkCommandBufferAllocateInfo cmdAllocInfo = command_buffer_allocate_info(_immCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

    _mainDeletionQueue.push_function([this]() {
        vkDestroyCommandPool(_device, _immCommandPool, nullptr);
    });

    if (_hasDedicatedTransfer) {
        VkCommandPoolCreateInfo xferPoolInfo = command_pool_create_info(
            _transferQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        VK_CHECK(vkCreateCommandPool(_device, &xferPoolInfo, nullptr, &_transferCommandPool));
        VkCommandBufferAllocateInfo xferAlloc = command_buffer_allocate_info(_transferCommandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(_device, &xferAlloc, &_transferCommandBuffer));

        VkFenceCreateInfo xferFenceInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK(vkCreateFence(_device, &xferFenceInfo, nullptr, &_transferFence));

        _mainDeletionQueue.push_function([this]() {
            vkDestroyFence(_device, _transferFence, nullptr);
            vkDestroyCommandPool(_device, _transferCommandPool, nullptr);
        });
    }
}

void VkRender::init_sync_structures() {
    VkFenceCreateInfo fenceCreateInfo     = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info();

    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
    _mainDeletionQueue.push_function([this]() { vkDestroyFence(_device, _immFence, nullptr); });

    for (auto & _frame : _frames) {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frame._renderFence));

        _mainDeletionQueue.push_function([this, &_frame]() {
            vkDestroyFence(_device, _frame._renderFence, nullptr);
        });
    }
}

void VkRender::init_descriptors() {
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
    };
    globalDescriptorAllocator.init_pool(_device, 10, sizes);
    _mainDeletionQueue.push_function([this]() { globalDescriptorAllocator.destroy_pool(_device); });

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindFlags {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        std::array<VkDescriptorBindingFlags, 2> flagArray {
            0u,
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
        };
        builder.bindings[1].descriptorCount = 4048;
        bindFlags.bindingCount   = 2;
        bindFlags.pBindingFlags  = flagArray.data();

        _gpuSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &bindFlags);
    }

    _mainDeletionQueue.push_function([this]() {
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout, nullptr);
    });

    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);
    {
        DescriptorWriter writer;
        writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(_device, _drawImageDescriptors);
    }

    for (auto & _frame : _frames) {
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        };
        _frame._frameDescriptors.init(_device, 1000, frame_sizes);
        _mainDeletionQueue.push_function([this, &_frame]() {
            _frame._frameDescriptors.destroy_pools(_device);
        });

        _frame._sceneDataBuffer = create_buffer(sizeof(GPUSceneData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        _mainDeletionQueue.push_function([this, &_frame]() {
            destroy_buffer(_frame._sceneDataBuffer);
        });
    }
}

void VkRender::init_pipelines() {
    init_background_pipelines();
    metalRoughMaterial.build_pipelines(this);
    init_oit_pipelines();

    _mainDeletionQueue.push_function([this]() {
        vkDestroyDescriptorSetLayout(_device, metalRoughMaterial.materialLayout, nullptr);
        vkDestroyPipelineLayout(_device, metalRoughMaterial.opaquePipeline.layout, nullptr);
        vkDestroyPipeline(_device, metalRoughMaterial.opaquePipeline.pipeline, nullptr);
        vkDestroyPipeline(_device, metalRoughMaterial.transparentPipeline.pipeline, nullptr);
    });
}

void VkRender::init_background_pipelines() {
    VkPushConstantRange pushConstant {};
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo computeLayout {};
    computeLayout.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pSetLayouts            = &_drawImageDescriptorLayout;
    computeLayout.setLayoutCount         = 1;
    computeLayout.pPushConstantRanges    = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

    auto make_compute_effect = [&](const char* name, const char* shaderPath, ComputePushConstants defaultData) -> ComputeEffect {
        VkShaderModule shaderModule;
        if (!load_shader_module(shaderPath, _device, &shaderModule)) {
            FE_CORE_CRITICAL("Failed to load compute shader: {}", shaderPath);
        }

        VkPipelineShaderStageCreateInfo stageinfo {};
        stageinfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageinfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stageinfo.module = shaderModule;
        stageinfo.pName  = "main";

        VkComputePipelineCreateInfo pipelineInfo {};
        pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = _gradientPipelineLayout;
        pipelineInfo.stage  = stageinfo;

        ComputeEffect effect{};
        effect.name   = name;
        effect.layout = _gradientPipelineLayout;
        effect.data   = defaultData;
        VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &effect.pipeline));
        vkDestroyShaderModule(_device, shaderModule, nullptr);
        return effect;
    };

    ComputePushConstants gradientData {};
    gradientData.data1 = glm::vec4(1, 0, 0, 1);
    gradientData.data2 = glm::vec4(0, 0, 1, 1);
    backgroundEffects.push_back(make_compute_effect("gradient", "../shaders/gradient_color.comp.spv", gradientData));

    ComputePushConstants skyData {};
    skyData.data1 = glm::vec4(0.1f, 0.2f, 0.4f, 0.97f);
    backgroundEffects.push_back(make_compute_effect("sky", "../shaders/sky.comp.spv", skyData));

    _mainDeletionQueue.push_function([this]() {
        vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
        for (auto& effect : backgroundEffects) {
            vkDestroyPipeline(_device, effect.pipeline, nullptr);
        }
    });
}

void VkRender::init_default_data() {
    std::array<Vertex, 4> rect_vertices{};
    rect_vertices[0].position = { 0.5f, -0.5f, 0.f };
    rect_vertices[1].position = { 0.5f,  0.5f, 0.f };
    rect_vertices[2].position = {-0.5f, -0.5f, 0.f };
    rect_vertices[3].position = {-0.5f,  0.5f, 0.f };
    rect_vertices[0].color = {0, 0, 0, 1};
    rect_vertices[1].color = {0.5f, 0.5f, 0.5f, 1};
    rect_vertices[2].color = {1, 0, 0, 1};
    rect_vertices[3].color = {0, 1, 0, 1};
    rect_vertices[0].uv_x = 1; rect_vertices[0].uv_y = 0;
    rect_vertices[1].uv_x = 0; rect_vertices[1].uv_y = 0;
    rect_vertices[2].uv_x = 1; rect_vertices[2].uv_y = 1;
    rect_vertices[3].uv_x = 0; rect_vertices[3].uv_y = 1;

    std::array<uint32_t, 6> rect_indices = { 0, 1, 2, 2, 1, 3 };
    rectangle = uploadMesh(rect_indices, rect_vertices);

    uint32_t white   = glm::packUnorm4x8(glm::vec4(1.f, 1.f, 1.f, 1.f));
    uint32_t grey    = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1.f));
    uint32_t black   = glm::packUnorm4x8(glm::vec4(0.f, 0.f, 0.f, 0.f));
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1.f, 0.f, 1.f, 1.f));

    _whiteImage = create_image((void*)&white, VkExtent3D{1,1,1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    _greyImage  = create_image((void*)&grey,  VkExtent3D{1,1,1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    _blackImage = create_image((void*)&black, VkExtent3D{1,1,1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    std::array<uint32_t, 16 * 16> pixels{};
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    _errorCheckerboardImage = create_image(pixels.data(), VkExtent3D{16,16,1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampl = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    VK_CHECK(vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest));

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    VK_CHECK(vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear));

    _mainDeletionQueue.push_function([this]() {
        destroy_image(_whiteImage);
        destroy_image(_greyImage);
        destroy_image(_blackImage);
        destroy_image(_errorCheckerboardImage);
        destroy_buffer(rectangle.indexBuffer);
        destroy_buffer(rectangle.vertexBuffer);
        vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
        vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
    });
}

void VkRender::init_renderables() {
    // Load project scenes here. Example:
    // auto scene = loadGltf(this, "../assets/scene.glb");
    // if (scene.has_value()) { loadedScenes["scene"] = *scene; }
}

// ─── Swapchain management ──────────────────────────────────────────────────

void VkRender::create_swapchain(uint32_t width, uint32_t height) {
    FE_CORE_TRACE("create_swapchain {}x{}", width, height);
    vkb::SwapchainBuilder swapchainBuilder { _chosenGPU, _device, _surface };

    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{.format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    _swapchainExtent     = vkbSwapchain.extent;
    _swapchain           = vkbSwapchain.swapchain;
    _swapchainImages     = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();

    // One acquire semaphore per swapchain image + one extra "pending" semaphore.
    // On each acquire we signal _pendingAcquireSemaphore, then swap it into the
    // per-image slot for the acquired index. This guarantees the semaphore we
    // reuse was released by a re-acquire of that same image, satisfying the
    // swapchain semaphore reuse rules.
    VkSemaphoreCreateInfo semInfo = semaphore_create_info();
    _imageAcquireSemaphores.resize(_swapchainImages.size());
    _imageRenderSemaphores.resize(_swapchainImages.size());
    for (size_t i = 0; i < _swapchainImages.size(); ++i) {
        VK_CHECK(vkCreateSemaphore(_device, &semInfo, nullptr, &_imageAcquireSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(_device, &semInfo, nullptr, &_imageRenderSemaphores[i]));
    }
    VK_CHECK(vkCreateSemaphore(_device, &semInfo, nullptr, &_pendingAcquireSemaphore));
}

void VkRender::destroy_swapchain() {
    FE_CORE_TRACE("destroy_swapchain images={}", _swapchainImages.size());
    for (auto& sem : _imageAcquireSemaphores) {
        vkDestroySemaphore(_device, sem, nullptr);
    }
    _imageAcquireSemaphores.clear();

    for (auto& sem : _imageRenderSemaphores) {
        vkDestroySemaphore(_device, sem, nullptr);
    }
    _imageRenderSemaphores.clear();

    if (_pendingAcquireSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(_device, _pendingAcquireSemaphore, nullptr);
        _pendingAcquireSemaphore = VK_NULL_HANDLE;
    }

    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    for (auto& view : _swapchainImageViews) {
        vkDestroyImageView(_device, view, nullptr);
    }
}

void VkRender::resize_swapchain() {
    FE_CORE_TRACE("resize_swapchain begin — current {}x{}",
        _windowExtent.width, _windowExtent.height);
    vkDeviceWaitIdle(_device);
    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width  = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);

    // Rebuild draw/depth/OIT images at new extent
    vkDestroyImageView(_device, _drawImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
    vkDestroyImageView(_device, _depthImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
    destroy_oit_images();

    VkExtent3D newExtent { _windowExtent.width, _windowExtent.height, 1 };

    VmaAllocationCreateInfo gpuOnly {};
    gpuOnly.usage         = VMA_MEMORY_USAGE_GPU_ONLY;
    gpuOnly.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    _drawImage.imageExtent = newExtent;
    {
        VkImageUsageFlags drawUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        VkImageCreateInfo rimg_info = image_create_info(_drawImage.imageFormat, drawUsages, newExtent);
        VK_CHECK(vmaCreateImage(_allocator, &rimg_info, &gpuOnly, &_drawImage.image, &_drawImage.allocation, nullptr));
        VkImageViewCreateInfo rview_info = imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));
    }

    _depthImage.imageExtent = newExtent;
    {
        VkImageCreateInfo dimg_info = image_create_info(_depthImage.imageFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, newExtent);
        VK_CHECK(vmaCreateImage(_allocator, &dimg_info, &gpuOnly, &_depthImage.image, &_depthImage.allocation, nullptr));
        VkImageViewCreateInfo dview_info = imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
        VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));
    }

    create_oit_images(newExtent);

    // Update compute descriptor pointing at the draw image storage view
    DescriptorWriter writer;
    writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(_device, _drawImageDescriptors);

    resize_requested = false;
    FE_CORE_INFO("Swapchain resized to {}x{}", _windowExtent.width, _windowExtent.height);
}

// ─── GLTFMetallic_Roughness ────────────────────────────────────────────────

void GLTFMetallic_Roughness::build_pipelines(VkRender* renderer) {
    VkShaderModule meshFragShader;
    if (!load_shader_module("../shaders/mesh_pbr.frag.spv", renderer->_device, &meshFragShader)) {
        FE_CORE_CRITICAL("Failed to load mesh fragment shader");
    }

    VkShaderModule meshVertexShader;
    if (!load_shader_module("../shaders/mesh.vert.spv", renderer->_device, &meshVertexShader)) {
        FE_CORE_CRITICAL("Failed to load mesh vertex shader");
    }

    VkPushConstantRange matrixRange {};
    matrixRange.offset     = 0;
    matrixRange.size       = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    materialLayout = layoutBuilder.build(renderer->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = { renderer->_gpuSceneDataDescriptorLayout, materialLayout };

    VkPipelineLayoutCreateInfo mesh_layout_info = pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount         = 2;
    mesh_layout_info.pSetLayouts            = layouts;
    mesh_layout_info.pPushConstantRanges    = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(renderer->_device, &mesh_layout_info, nullptr, &newLayout));
    opaquePipeline.layout      = newLayout;
    transparentPipeline.layout = newLayout;

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipelineBuilder.set_color_attachment_format(renderer->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(renderer->_depthImage.imageFormat);
    pipelineBuilder._pipelineLayout = newLayout;

    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(renderer->_device);

    pipelineBuilder.enable_blending_additive();
    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(renderer->_device);

    vkDestroyShaderModule(renderer->_device, meshFragShader, nullptr);
    vkDestroyShaderModule(renderer->_device, meshVertexShader, nullptr);
}

void GLTFMetallic_Roughness::clear_resources(VkDevice /*device*/) {}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator) {
    MaterialInstance matData{};
    matData.passType = pass;
    matData.pipeline = (pass == MaterialPass::Transparent) ? &transparentPipeline : &opaquePipeline;
    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(device, matData.materialSet);

    return matData;
}

// ─── MeshNode ─────────────────────────────────────────────────────────────

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto& s : mesh->surfaces) {
        RenderObject def = {};
        def.indexCount          = s.count;
        def.firstIndex          = s.startIndex;
        def.indexBuffer         = mesh->meshBuffers.indexBuffer.buffer;
        def.material            = &s.material->data;
        def.bounds              = s.bounds;
        def.transform           = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        if (s.material->data.passType == MaterialPass::Transparent) {
            ctx.TransparentSurfaces.push_back(def);
        } else {
            ctx.OpaqueSurfaces.push_back(def);
        }
    }

    Node::Draw(topMatrix, ctx);
}

// ─── TextureCache ──────────────────────────────────────────────────────────

TextureID TextureCache::AddTexture(const VkImageView& image, VkSampler sampler) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(Cache.size()); i++) {
        if (Cache[i].imageView == image && Cache[i].sampler == sampler) {
            FE_CORE_TRACE("TextureCache::AddTexture hit  slot={} view={} sampler={}",
                i, (void*)image, (void*)sampler);
            return TextureID{i};
        }
    }

    const VkDescriptorImageInfo entry {
        .sampler     = sampler,
        .imageView   = image,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    if (!_freeSlots.empty()) {
        const uint32_t idx = _freeSlots.back();
        _freeSlots.pop_back();
        Cache[idx] = entry;
        FE_CORE_TRACE("TextureCache::AddTexture reuse slot={} view={} sampler={} freeSlots={}",
            idx, (void*)image, (void*)sampler, _freeSlots.size());
        return TextureID{idx};
    }

    const auto idx = static_cast<uint32_t>(Cache.size());
    Cache.push_back(entry);
    FE_CORE_TRACE("TextureCache::AddTexture new   slot={} view={} sampler={} cacheSize={}",
        idx, (void*)image, (void*)sampler, Cache.size());
    return TextureID{idx};
}

void TextureCache::FreeTexturesWithView(VkImageView view, VkDescriptorImageInfo fallback) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(Cache.size()); i++) {
        if (Cache[i].imageView == view) {
            FE_CORE_TRACE("TextureCache::FreeTexturesWithView slot={} view={}", i, (void*)view);
            Cache[i] = fallback;
            _freeSlots.push_back(i);
        }
    }
}

void TextureCache::FreeTexturesWithSampler(VkSampler sampler, VkDescriptorImageInfo fallback) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(Cache.size()); i++) {
        if (Cache[i].sampler == sampler) {
            FE_CORE_TRACE("TextureCache::FreeTexturesWithSampler slot={} sampler={}", i, (void*)sampler);
            Cache[i] = fallback;
            _freeSlots.push_back(i);
        }
    }
}

} // namespace fe
