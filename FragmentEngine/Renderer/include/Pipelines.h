#pragma once
#include <Types.h>

namespace fe {

    class PipelineBuilder {
    public:
        std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;

        VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
        VkPipelineRasterizationStateCreateInfo _rasterizer;
        VkPipelineColorBlendAttachmentState    _colorBlendAttachment;    // single-attachment path
        VkPipelineMultisampleStateCreateInfo   _multisampling;
        VkPipelineLayout                       _pipelineLayout;
        VkPipelineDepthStencilStateCreateInfo  _depthStencil;
        VkPipelineRenderingCreateInfo          _renderInfo;
        VkFormat                               _colorAttachmentformat;   // single-attachment path

        // Multi-attachment path (populated by set_color_attachments)
        std::vector<VkPipelineColorBlendAttachmentState> _colorBlendAttachments;
        std::vector<VkFormat>                            _colorAttachmentFormats;

        PipelineBuilder() { clear(); }

        void clear();
        VkPipeline build_pipeline(VkDevice device);

        void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
        void set_input_topology(VkPrimitiveTopology topology);
        void set_polygon_mode(VkPolygonMode mode);
        void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace);
        void set_multisampling_none();
        void disable_blending();
        void enable_blending_additive();
        void enable_blending_alphablend();
        void set_color_attachment_format(VkFormat format);
        void set_color_attachments(const std::vector<VkFormat>& formats,
                                   const std::vector<VkPipelineColorBlendAttachmentState>& blendStates);
        void set_depth_format(VkFormat format);
        void disable_depthtest();
        void enable_depthtest(bool depthWriteEnable, VkCompareOp op);
    };

    bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
};
