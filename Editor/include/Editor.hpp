#pragma once

#include "Core.hpp"
#include <iostream>

class Editor
{
public:
    void Run()
    {
        Engine engine;
        engine.Startup();
        std::cout << "Editor Running..." << std::endl;
    }
};