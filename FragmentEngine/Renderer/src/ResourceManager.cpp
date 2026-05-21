#include "ResourceManager.h"
#include "VkRender.h"
#include "LogInternal.h"

namespace fe {

std::shared_ptr<AllocatedImage> ResourceManager::getOrCreate(
    const std::string& key,
    std::function<std::optional<AllocatedImage>()> factory)
{
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        if (auto sp = it->second.lock()) {
            FE_CORE_TRACE("ResourceManager::getOrCreate HIT  '{}' use_count={}", key, sp.use_count());
            return sp;
        }
        FE_CORE_TRACE("ResourceManager::getOrCreate EXPIRED '{}', reloading", key);
    } else {
        FE_CORE_TRACE("ResourceManager::getOrCreate MISS '{}', loading", key);
    }

    auto result = factory();
    if (!result) {
        FE_CORE_WARN("ResourceManager::getOrCreate factory FAILED for '{}'", key);
        return nullptr;
    }

    VkRender* r = _renderer;
    auto sp = std::shared_ptr<AllocatedImage>(
        new AllocatedImage(*result),
        [r, key](AllocatedImage* img) {
            FE_CORE_TRACE("ResourceManager deleter fired for '{}' image={}", key, (void*)img->image);
            r->destroy_image(*img);
            delete img;
        });

    FE_CORE_TRACE("ResourceManager::getOrCreate created '{}' image={}", key, (void*)sp->image);
    _cache[key] = sp;
    return sp;
}

void ResourceManager::purgeExpired()
{
    int count = 0;
    for (auto it = _cache.begin(); it != _cache.end(); ) {
        if (it->second.expired()) {
            FE_CORE_TRACE("ResourceManager::purgeExpired removing '{}'", it->first);
            it = _cache.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    if (count) FE_CORE_TRACE("ResourceManager::purgeExpired removed {} expired entries", count);
}

} // namespace fe
