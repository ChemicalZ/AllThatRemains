#include <iostream>

#include "Engine.h"

int main() {
    auto engine = fe::Engine("AllThatRemains", 1600, 1200);
    engine.Run();
    return 0;
}
