#pragma once

#include <memory>

class App {
public:
    App();
    ~App();

    void run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
