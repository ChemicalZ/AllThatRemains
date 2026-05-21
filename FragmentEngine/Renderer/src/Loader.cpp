#define GLM_ENABLE_EXPERIMENTAL
#include <Loader.h>
#include "VkRender.h"
#include "Images.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>


#include "LogInternal.h"

namespace fe {

// ─── SDL surface → Vulkan image upload ────────────────────────────────────

static std::optional<AllocatedImage> upload_surface(VkRender* renderer, SDL_Surface* rawSurface)
{
    SDL_Surface* surface = SDL_ConvertSurface(rawSurface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(rawSurface);
    if (!surface) return {};

    VkExtent3D imagesize { (uint32_t)surface->w, (uint32_t)surface->h, 1 };

    AllocatedImage img;
    if (surface->pitch == surface->w * 4) {
        img = renderer->create_image(surface->pixels, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
    } else {
        std::vector<uint8_t> pixels((size_t)surface->w * surface->h * 4);
        for (int y = 0; y < surface->h; y++) {
            memcpy(pixels.data() + (size_t)y * surface->w * 4,
                   (uint8_t*)surface->pixels + (size_t)y * surface->pitch,
                   (size_t)surface->w * 4);
        }
        img = renderer->create_image(pixels.data(), imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
    }

    SDL_DestroySurface(surface);
    return img;
}

// ─── Per-image loading from fastgltf source ───────────────────────────────

static std::optional<AllocatedImage> load_image(VkRender* renderer, fastgltf::Asset& asset, fastgltf::Image& image)
{
    AllocatedImage newImage {};

    std::visit(fastgltf::visitor {
        [](auto& /*arg*/) {},

        [&](fastgltf::sources::URI& filePath) {
            assert(filePath.fileByteOffset == 0);
            assert(filePath.uri.isLocalPath());
            const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());
            SDL_Surface* surface = IMG_Load(path.c_str());
            if (surface) {
                auto img = upload_surface(renderer, surface);
                if (img) newImage = *img;
            }
        },

        [&](fastgltf::sources::Array& array) {
            SDL_IOStream* io = SDL_IOFromConstMem(array.bytes.data(), array.bytes.size());
            SDL_Surface* surface = IMG_Load_IO(io, false);
            SDL_CloseIO(io);
            if (surface) {
                auto img = upload_surface(renderer, surface);
                if (img) newImage = *img;
            }
        },

        [&](fastgltf::sources::BufferView& view) {
            auto& bufferView = asset.bufferViews[view.bufferViewIndex];
            auto& buffer     = asset.buffers[bufferView.bufferIndex];
            std::visit(fastgltf::visitor {
                [](auto& /*arg*/) {},
                [&](fastgltf::sources::Array& array) {
                    SDL_IOStream* io = SDL_IOFromConstMem(
                        (const uint8_t*)array.bytes.data() + bufferView.byteOffset,
                        bufferView.byteLength);
                    SDL_Surface* surface = IMG_Load_IO(io, false);
                    SDL_CloseIO(io);
                    if (surface) {
                        auto img = upload_surface(renderer, surface);
                        if (img) newImage = *img;
                    }
                }
            }, buffer.data);
        },
    }, image.data);

    if (newImage.image == VK_NULL_HANDLE) return {};
    return newImage;
}

// ─── Sampler filter extraction ─────────────────────────────────────────────

static VkFilter extract_filter(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;
    default:
        return VK_FILTER_LINEAR;
    }
}

static VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

// ─── Main GLTF loader ──────────────────────────────────────────────────────

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VkRender* renderer, std::string_view filePath)
{
    FE_CORE_INFO("Loading GLTF: {}", filePath);

    std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
    scene->creator = renderer;
    LoadedGLTF& file = *scene;

    fastgltf::Parser parser {};
    constexpr auto gltfOptions =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble               |
        fastgltf::Options::LoadExternalBuffers;

    std::filesystem::path path = filePath;
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (!data) {
        FE_CORE_ERROR("Failed to load GLTF file '{}': {}", filePath, fastgltf::getErrorName(data.error()));
        return {};
    }

    auto asset = parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
    if (auto error = asset.error(); error != fastgltf::Error::None) {
        FE_CORE_ERROR("Failed to parse GLTF '{}': {}", filePath, fastgltf::getErrorName(error));
        return {};
    }

    fastgltf::Asset gltf = std::move(asset.get());

    // Descriptor pool for this file's materials
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
    };
    file.descriptorPool.init(renderer->_device, std::max<uint32_t>(1, gltf.materials.size()), sizes);

    // Samplers
    for (fastgltf::Sampler& sampler : gltf.samplers) {
        VkSamplerCreateInfo sampl = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampl.maxLod    = VK_LOD_CLAMP_NONE;
        sampl.minLod    = 0;
        sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
        sampl.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler newSampler;
        VK_CHECK(vkCreateSampler(renderer->_device, &sampl, nullptr, &newSampler));
        file.samplers.push_back(newSampler);
    }

    // Images — loaded through ResourceManager for cross-scene texture sharing.
    // The key is "<filePath>/<imageName>" to prevent collisions between files.
    std::vector<std::shared_ptr<MeshAsset>>  meshes;
    std::vector<std::shared_ptr<Node>>        nodes;
    std::vector<AllocatedImage>               images; // local by-value for material indexing
    std::vector<std::shared_ptr<GLTFMaterial>> materials;

    const std::string scenePrefix = std::string(filePath) + "/";
    for (fastgltf::Image& image : gltf.images) {
        const std::string key = scenePrefix + image.name.c_str();
        auto sp = renderer->resourceManager.getOrCreate(key, [&]() -> std::optional<AllocatedImage> {
            return load_image(renderer, gltf, image);
        });
        if (sp) {
            images.push_back(*sp);
            file.images[image.name.c_str()] = sp;
        } else {
            // Load failed — use non-owning reference to error checkerboard
            FE_CORE_WARN("Failed to load texture '{}', using error checkerboard", image.name);
            images.push_back(renderer->_errorCheckerboardImage);
            file.images[image.name.c_str()] = std::shared_ptr<AllocatedImage>(
                &renderer->_errorCheckerboardImage, [](AllocatedImage*) {} );
        }
    }

    // Material data buffer — allocate at least 1 slot for the default material fallback
    file.materialDataBuffer = renderer->create_buffer(
        sizeof(GLTFMetallic_Roughness::MaterialConstants) * std::max<size_t>(1, gltf.materials.size()),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    int data_index = 0;
    auto* sceneMaterialConstants = (GLTFMetallic_Roughness::MaterialConstants*)file.materialDataBuffer.info.pMappedData;

    for (fastgltf::Material& mat : gltf.materials) {
        auto newMat = std::make_shared<GLTFMaterial>();
        materials.push_back(newMat);
        file.materials[mat.name.c_str()] = newMat;

        GLTFMetallic_Roughness::MaterialConstants constants;
        constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.w = mat.pbrData.baseColorFactor[3];
        constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
        constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;

        MaterialPass passType = MaterialPass::MainColor;
        if (mat.alphaMode == fastgltf::AlphaMode::Blend)
            passType = MaterialPass::Transparent;

        GLTFMetallic_Roughness::MaterialResources materialResources;
        materialResources.colorImage        = renderer->_whiteImage;
        materialResources.colorSampler      = renderer->_defaultSamplerLinear;
        materialResources.metalRoughImage   = renderer->_whiteImage;
        materialResources.metalRoughSampler = renderer->_defaultSamplerLinear;
        materialResources.dataBuffer        = file.materialDataBuffer.buffer;
        materialResources.dataBufferOffset  = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);

        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t img     = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();
            materialResources.colorImage   = images[img];
            materialResources.colorSampler = file.samplers[sampler];
        }

        constants.colorTexID      = renderer->texCache.AddTexture(materialResources.colorImage.imageView,      materialResources.colorSampler).Index;
        constants.metalRoughTexID = renderer->texCache.AddTexture(materialResources.metalRoughImage.imageView, materialResources.metalRoughSampler).Index;
        sceneMaterialConstants[data_index] = constants;

        newMat->data = renderer->metalRoughMaterial.write_material(renderer->_device, passType, materialResources, file.descriptorPool);
        data_index++;
    }

    // Default material for primitives with no material index
    if (materials.empty()) {
        auto defaultMat = std::make_shared<GLTFMaterial>();
        GLTFMetallic_Roughness::MaterialResources defaultResources;
        defaultResources.colorImage        = renderer->_whiteImage;
        defaultResources.colorSampler      = renderer->_defaultSamplerLinear;
        defaultResources.metalRoughImage   = renderer->_whiteImage;
        defaultResources.metalRoughSampler = renderer->_defaultSamplerLinear;
        defaultResources.dataBuffer        = file.materialDataBuffer.buffer;
        defaultResources.dataBufferOffset  = 0;
        sceneMaterialConstants[0].colorFactors        = glm::vec4(1.f);
        sceneMaterialConstants[0].metal_rough_factors = glm::vec4(1.f, 0.5f, 0.f, 0.f);
        sceneMaterialConstants[0].colorTexID      = renderer->texCache.AddTexture(renderer->_whiteImage.imageView, renderer->_defaultSamplerLinear).Index;
        sceneMaterialConstants[0].metalRoughTexID = renderer->texCache.AddTexture(renderer->_whiteImage.imageView, renderer->_defaultSamplerLinear).Index;
        defaultMat->data = renderer->metalRoughMaterial.write_material(renderer->_device, MaterialPass::MainColor, defaultResources, file.descriptorPool);
        materials.push_back(defaultMat);
    }

    // Meshes
    std::vector<uint32_t> indices;
    std::vector<Vertex>   vertices;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
        auto newmesh = std::make_shared<MeshAsset>();
        meshes.push_back(newmesh);
        file.meshes[mesh.name.c_str()] = newmesh;
        newmesh->name = mesh.name;
        indices.clear();
        vertices.clear();

        for (auto&& p : mesh.primitives) {
            if (!p.indicesAccessor.has_value()) {
                FE_CORE_WARN("Mesh '{}' primitive missing index accessor, skipping", mesh.name);
                continue;
            }
            auto* posAttr = p.findAttribute("POSITION");
            if (posAttr == p.attributes.end()) {
                FE_CORE_WARN("Mesh '{}' primitive missing POSITION attribute, skipping", mesh.name);
                continue;
            }

            GeoSurface newSurface;
            newSurface.startIndex = (uint32_t)indices.size();
            newSurface.count      = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;
            size_t initial_vtx    = vertices.size();

            // Indices
            {
                fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);
                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor,
                    [&](std::uint32_t idx) { indices.push_back(idx + (uint32_t)initial_vtx); });
            }

            // Positions
            {
                fastgltf::Accessor& posAccessor = gltf.accessors[posAttr->accessorIndex];
                vertices.resize(vertices.size() + posAccessor.count);
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
                    [&](glm::vec3 v, size_t index) {
                        Vertex newvtx;
                        newvtx.position = v;
                        newvtx.normal   = {1, 0, 0};
                        newvtx.color    = glm::vec4{1.f};
                        newvtx.uv_x = newvtx.uv_y = 0;
                        vertices[initial_vtx + index] = newvtx;
                    });
            }

            // Normals
            if (auto normals = p.findAttribute("NORMAL"); normals != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex],
                    [&](glm::vec3 v, size_t index) { vertices[initial_vtx + index].normal = v; });
            }

            // UVs
            if (auto uv = p.findAttribute("TEXCOORD_0"); uv != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex],
                    [&](glm::vec2 v, size_t index) {
                        vertices[initial_vtx + index].uv_x = v.x;
                        vertices[initial_vtx + index].uv_y = v.y;
                    });
            }

            // Colors
            if (auto colors = p.findAttribute("COLOR_0"); colors != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colors->accessorIndex],
                    [&](glm::vec4 v, size_t index) { vertices[initial_vtx + index].color = v; });
            }

            newSurface.material = p.materialIndex.has_value() ? materials[p.materialIndex.value()] : materials[0];

            glm::vec3 minpos = vertices[initial_vtx].position;
            glm::vec3 maxpos = vertices[initial_vtx].position;
            for (size_t i = initial_vtx; i < vertices.size(); i++) {
                minpos = glm::min(minpos, vertices[i].position);
                maxpos = glm::max(maxpos, vertices[i].position);
            }
            newSurface.bounds.origin       = (maxpos + minpos) / 2.f;
            newSurface.bounds.extents      = (maxpos - minpos) / 2.f;
            newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);
            newmesh->surfaces.push_back(newSurface);
        }

        newmesh->meshBuffers = renderer->uploadMesh(indices, vertices);
    }

    // Nodes
    for (fastgltf::Node& node : gltf.nodes) {
        std::shared_ptr<Node> newNode;
        if (node.meshIndex.has_value()) {
            newNode = std::make_shared<MeshNode>();
            static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
        } else {
            newNode = std::make_shared<Node>();
        }
        nodes.push_back(newNode);
        file.nodes[node.name.c_str()];

        std::visit(fastgltf::visitor {
            [&](fastgltf::math::fmat4x4& matrix) {
                static_assert(sizeof(fastgltf::math::fmat4x4) == sizeof(glm::mat4));
                std::memcpy(&newNode->localTransform, &matrix, sizeof(glm::mat4));
            },
            [&](fastgltf::TRS& trs) {
                glm::vec3 tl(trs.translation[0], trs.translation[1], trs.translation[2]);
                glm::quat rot(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]);
                glm::vec3 sc(trs.scale[0], trs.scale[1], trs.scale[2]);

                newNode->localTransform =
                    glm::translate(glm::mat4(1.f), tl) *
                    glm::toMat4(rot) *
                    glm::scale(glm::mat4(1.f), sc);
            }
        }, node.transform);
    }

    // Build hierarchy
    for (size_t i = 0; i < gltf.nodes.size(); i++) {
        for (auto& c : gltf.nodes[i].children) {
            nodes[i]->children.push_back(nodes[c]);
            nodes[c]->parent = nodes[i];
        }
    }

    for (auto& node : nodes) {
        if (node->parent.lock() == nullptr) {
            file.topNodes.push_back(node);
            node->refreshTransform(glm::mat4{1.f});
        }
    }

    return scene;
}

// ─── LoadedGLTF ───────────────────────────────────────────────────────────

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    for (auto& n : topNodes) {
        n->Draw(topMatrix, ctx);
    }
}

void LoadedGLTF::clearAll()
{
    VkDevice dv = creator->_device;

    // 1. Evict from TextureCache while shared_ptrs (and their imageViews) are
    //    still alive. Non-owning error-checkerboard entries are skipped.
    const VkDescriptorImageInfo fallback {
        .sampler     = creator->_defaultSamplerNearest,
        .imageView   = creator->_errorCheckerboardImage.imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    // Evict by image — only when this scene is the last holder. If use_count > 1
    // another scene shares the image (via ResourceManager) and its material
    // TextureIDs still index that slot; evicting would corrupt the survivor.
    for (auto& [k, sp] : images) {
        if (!sp || sp.get() == &creator->_errorCheckerboardImage) continue;
        if (sp.use_count() == 1) {
            creator->texCache.FreeTexturesWithView(sp->imageView, fallback);
        }
    }

    // Evict by sampler — always. GLTF samplers are created per-loadGltf call and
    // are never shared across scenes. Any cache entry referencing one of our
    // samplers must be removed before the sampler handle is destroyed, regardless
    // of whether the paired image is still alive in another scene.
    for (auto& sampler : samplers) {
        creator->texCache.FreeTexturesWithSampler(sampler, fallback);
    }

    // 2. Drain GPU — clearAll() runs mid-frame while the previous frame's
    //    GPU submission may still be in flight. Must finish before destroying
    //    any Vulkan resources.
    vkDeviceWaitIdle(dv);

    // 3. Destroy mesh buffers (not ResourceManager-managed).
    for (auto& [k, v] : meshes) {
        creator->destroy_buffer(v->meshBuffers.indexBuffer);
        creator->destroy_buffer(v->meshBuffers.vertexBuffer);
    }

    // 4. Release image shared_ptrs. If this is the last reference, the custom
    //    deleter calls destroy_image — safe because GPU is idle from step 2.
    //    Non-owning error-checkerboard ptrs have a no-op deleter.
    images.clear();

    // 5. Samplers, descriptor pool, material constants buffer.
    for (auto& sampler : samplers) {
        vkDestroySampler(dv, sampler, nullptr);
    }
    descriptorPool.destroy_pools(dv);
    creator->destroy_buffer(materialDataBuffer);
}

} // namespace fe
