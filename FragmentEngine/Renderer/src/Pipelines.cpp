#include <Pipelines.h>
#include <Initializers.h>
#include <fstream>

namespace fe {

    void PipelineBuilder::clear()
    {
        _inputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        _rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        _colorBlendAttachment = {};
        _multisampling = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        _pipelineLayout = {};
        _depthStencil = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        _renderInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        _shaderStages.clear();
    }

    VkPipeline PipelineBuilder::build_pipeline(VkDevice device)
    {
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &_colorBlendAttachment;

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo pipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &_renderInfo;
        pipelineInfo.stageCount = (uint32_t)_shaderStages.size();
        pipelineInfo.pStages = _shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &_inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &_rasterizer;
        pipelineInfo.pMultisampleState = &_multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &_depthStencil;
        pipelineInfo.layout = _pipelineLayout;

        VkDynamicState state[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicInfo.pDynamicStates = &state[0];
        dynamicInfo.dynamicStateCount = 2;
        pipelineInfo.pDynamicState = &dynamicInfo;

        VkPipeline newPipeline;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline));
        return newPipeline;
    }

    void PipelineBuilder::set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader)
    {
        _shaderStages.clear();
        _shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
        _shaderStages.push_back(pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
    }

    void PipelineBuilder::set_input_topology(VkPrimitiveTopology topology)
    {
        _inputAssembly.topology = topology;
        _inputAssembly.primitiveRestartEnable = VK_FALSE;
    }

    void PipelineBuilder::set_polygon_mode(VkPolygonMode mode)
    {
        _rasterizer.polygonMode = mode;
        _rasterizer.lineWidth = 1.f;
    }

    void PipelineBuilder::set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace)
    {
        _rasterizer.cullMode = cullMode;
        _rasterizer.frontFace = frontFace;
    }

    void PipelineBuilder::set_multisampling_none()
    {
        _multisampling.sampleShadingEnable = VK_FALSE;
        _multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        _multisampling.minSampleShading = 1.0f;
        _multisampling.pSampleMask = nullptr;
        _multisampling.alphaToCoverageEnable = VK_FALSE;
        _multisampling.alphaToOneEnable = VK_FALSE;
    }

    void PipelineBuilder::disable_blending()
    {
        _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        _colorBlendAttachment.blendEnable = VK_FALSE;
    }

    void PipelineBuilder::enable_blending_additive()
    {
        _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        _colorBlendAttachment.blendEnable = VK_TRUE;
        _colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        _colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        _colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        _colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        _colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        _colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    void PipelineBuilder::enable_blending_alphablend()
    {
        _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        _colorBlendAttachment.blendEnable = VK_TRUE;
        _colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        _colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        _colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        _colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        _colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        _colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    void PipelineBuilder::set_color_attachment_format(VkFormat format)
    {
        _colorAttachmentformat = format;
        _renderInfo.colorAttachmentCount = 1;
        _renderInfo.pColorAttachmentFormats = &_colorAttachmentformat;
    }

    void PipelineBuilder::set_depth_format(VkFormat format)
    {
        _renderInfo.depthAttachmentFormat = format;
    }

    void PipelineBuilder::disable_depthtest()
    {
        _depthStencil.depthTestEnable = VK_FALSE;
        _depthStencil.depthWriteEnable = VK_FALSE;
        _depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
        _depthStencil.depthBoundsTestEnable = VK_FALSE;
        _depthStencil.stencilTestEnable = VK_FALSE;
        _depthStencil.front = {};
        _depthStencil.back = {};
        _depthStencil.minDepthBounds = 0.f;
        _depthStencil.maxDepthBounds = 1.f;
    }

    void PipelineBuilder::enable_depthtest(bool depthWriteEnable, VkCompareOp op)
    {
        _depthStencil.depthTestEnable = VK_TRUE;
        _depthStencil.depthWriteEnable = depthWriteEnable;
        _depthStencil.depthCompareOp = op;
        _depthStencil.depthBoundsTestEnable = VK_FALSE;
        _depthStencil.stencilTestEnable = VK_FALSE;
        _depthStencil.front = {};
        _depthStencil.back = {};
        _depthStencil.minDepthBounds = 0.f;
        _depthStencil.maxDepthBounds = 1.f;
    }

    bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
    {
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
        file.seekg(0);
        file.read((char*)buffer.data(), fileSize);
        file.close();

        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = buffer.size() * sizeof(uint32_t);
        createInfo.pCode = buffer.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            return false;
        }
        *outShaderModule = shaderModule;
        return true;
    }
}
