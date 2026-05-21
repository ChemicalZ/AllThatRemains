#include "Images.h"
#include "Initializers.h"

namespace fe {

    static VkPipelineStageFlags2 stage_for_layout(VkImageLayout layout)
    {
        switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        default:
            return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
    }

    static VkAccessFlags2 access_for_layout(VkImageLayout layout, bool isSrc)
    {
        switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return 0;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_2_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_2_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return isSrc
                ? VK_ACCESS_2_SHADER_WRITE_BIT
                : (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return isSrc
                ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                : (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            return isSrc
                ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                : (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        default:
            return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        }
    }

    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
    {
        VkImageMemoryBarrier2 imageBarrier {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        imageBarrier.pNext = nullptr;

        imageBarrier.srcStageMask  = stage_for_layout(currentLayout);
        imageBarrier.srcAccessMask = access_for_layout(currentLayout, true);
        imageBarrier.dstStageMask  = stage_for_layout(newLayout);
        imageBarrier.dstAccessMask = access_for_layout(newLayout, false);

        imageBarrier.oldLayout = currentLayout;
        imageBarrier.newLayout = newLayout;

        const bool isDepth = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
                               newLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
        VkImageAspectFlags aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange = image_subresource_range(aspectMask);
        imageBarrier.image = image;

        VkDependencyInfo depInfo {};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.pNext = nullptr;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize)
    {
        VkImageBlit2 blitRegion{.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr};

        blitRegion.srcOffsets[1].x = srcSize.width;
        blitRegion.srcOffsets[1].y = srcSize.height;
        blitRegion.srcOffsets[1].z = 1;

        blitRegion.dstOffsets[1].x = dstSize.width;
        blitRegion.dstOffsets[1].y = dstSize.height;
        blitRegion.dstOffsets[1].z = 1;

        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.baseArrayLayer = 0;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcSubresource.mipLevel = 0;

        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.baseArrayLayer = 0;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstSubresource.mipLevel = 0;

        VkBlitImageInfo2 blitInfo{.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr};
        blitInfo.dstImage = destination;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.srcImage = source;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.filter = VK_FILTER_LINEAR;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;

        vkCmdBlitImage2(cmd, &blitInfo);
    }

    void generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize)
    {
        int mipLevels = int(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;
        for (int mip = 0; mip < mipLevels; mip++) {

            VkExtent2D halfSize = imageSize;
            halfSize.width /= 2;
            halfSize.height /= 2;

            VkImageMemoryBarrier2 imageBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .pNext = nullptr};
            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageBarrier.subresourceRange = image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseMipLevel = mip;
            imageBarrier.image = image;

            VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .pNext = nullptr};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &imageBarrier;
            vkCmdPipelineBarrier2(cmd, &depInfo);

            if (mip < mipLevels - 1) {
                VkImageBlit2 blitRegion{.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr};

                blitRegion.srcOffsets[1].x = imageSize.width;
                blitRegion.srcOffsets[1].y = imageSize.height;
                blitRegion.srcOffsets[1].z = 1;

                blitRegion.dstOffsets[1].x = halfSize.width;
                blitRegion.dstOffsets[1].y = halfSize.height;
                blitRegion.dstOffsets[1].z = 1;

                blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)mip, 0, 1};
                blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)mip + 1, 0, 1};

                VkBlitImageInfo2 blitInfo{.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr};
                blitInfo.dstImage = image;
                blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                blitInfo.srcImage = image;
                blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                blitInfo.filter = VK_FILTER_LINEAR;
                blitInfo.regionCount = 1;
                blitInfo.pRegions = &blitRegion;
                vkCmdBlitImage2(cmd, &blitInfo);

                imageSize = halfSize;
            }
        }

        transition_image(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}
