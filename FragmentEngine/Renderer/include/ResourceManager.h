#pragma once

#include "Types.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>
#include <optional>

namespace fe {

class VkRender;

class ResourceManager {
public:
    explicit ResourceManager(VkRender* renderer) : _renderer(renderer) {}

    // Returns a shared_ptr<AllocatedImage> whose custom deleter calls
    // renderer->destroy_image. On cache hit returns the existing live ptr.
    // Returns nullptr if the factory signals failure (returns std::nullopt).
    std::shared_ptr<AllocatedImage> getOrCreate(
        const std::string& key,
        std::function<std::optional<AllocatedImage>()> factory);

    // Remove dead weak_ptrs from the registry (optional housekeeping).
    void purgeExpired();

private:
    VkRender* _renderer;
    std::unordered_map<std::string, std::weak_ptr<AllocatedImage>> _cache;
};

} // namespace fe
