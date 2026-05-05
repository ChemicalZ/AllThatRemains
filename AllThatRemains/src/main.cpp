#include <iostream>

#include "Engine.h"

int main() {
    auto engine = fe::Engine();
    engine.Run("AllThatRemains", 800, 600);
    return 0;
}
