#include <App.hpp>

class HelloApp : public App {
public:
    HelloApp()
        : App({
              .width = 1280,
              .height = 720,
              .title = "HelloGraphics",
          }) {}
};

int main() {
    try {
        HelloApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
