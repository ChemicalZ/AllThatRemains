#include <iostream>

#include "Engine.h"

int main() {
    auto engine = fe::Engine("AllThatRemains", 800, 600);
    engine.Run();
    return 0;
}
