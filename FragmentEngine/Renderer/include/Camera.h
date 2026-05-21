#pragma once

#include <Types.h>

union SDL_Event;

namespace fe {

class Camera {
public:
    glm::vec3 velocity;
    glm::vec3 position;
    float pitch { 0.f };
    float yaw   { 0.f };

    glm::mat4 getViewMatrix() const;
    glm::mat4 getRotationMatrix() const;

    void processSDLEvent(SDL_Event& e);
    void update(float dt);

private:
    bool _rightMouseHeld { false };
};

} // namespace fe
