#pragma once

#include <adwaita.h>

#include <memory>

#include "window.hpp"

namespace kiln {

class Application {
public:
    Application();
    ~Application();

    int run(int argc, char** argv);

private:
    static void on_activate(GApplication* app, gpointer user);
    static void on_about(GSimpleAction* act, GVariant* param, gpointer user);

    AdwApplication* app_ = nullptr;
    std::unique_ptr<Window> window_;
};

} // namespace kiln
