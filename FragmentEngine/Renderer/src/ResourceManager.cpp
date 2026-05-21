#include "ResourceManager.h"
#include "VkRender.h"

namespace fe {

std::shared_ptr<AllocatedImage> ResourceManager::getOrCreate(
    const std::string& key,
    std::function<std::optional<AllocatedImage>()> factory)
{
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        if (auto sp = it->second.lock()) {
            return sp;
        }
    }

    auto result = factory();
    if (!result) return nullptr;

    VkRender* r = _renderer;
    auto sp = std::shared_ptr<AllocatedImage>(
        new AllocatedImage(*result),
        [r](AllocatedImage* img) {
            r->destroy_image(*img);
            delete img;
        });

    _cache[key] = sp;
    return sp;
}

void ResourceManager::purgeExpired()
{
    for (auto it = _cache.begin(); it != _cache.end(); ) {
        it = it->second.expired() ? _cache.erase(it) : std::next(it);
    }
}

} // namespace fe
