#pragma once

#include <Types.h>
#include <Descriptors.h>
#include <unordered_map>
#include <filesystem>

namespace fe {

class VkRender;
struct DrawContext;

struct Bounds {
    glm::vec3 origin;
    float sphereRadius;
    glm::vec3 extents;
};

struct GLTFMaterial {
    MaterialInstance data;
};

struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    Bounds bounds;
    std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset {
    std::string name;
    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};

struct LoadedGLTF : public IRenderable {
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, std::shared_ptr<AllocatedImage>> images;
    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

    std::vector<std::shared_ptr<Node>> topNodes;
    std::vector<VkSampler> samplers;

    DescriptorAllocatorGrowable descriptorPool;
    AllocatedBuffer materialDataBuffer;

    VkRender* creator;

    ~LoadedGLTF() { clearAll(); }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;

private:
    void clearAll();
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VkRender* renderer, std::string_view filePath);

} // namespace fe
