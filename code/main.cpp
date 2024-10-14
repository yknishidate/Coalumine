#include <random>

#include <reactive/reactive.hpp>

#include "app/headless_app.hpp"
#include "app/window_app.hpp"

int main(int argc, char* argv[]) {
    try {
        // 実行モード "window", "headless" は、
        // コマンドライン引数で与えるか、ランタイムのユーザー入力で与えることができる
        std::string mode;
        std::string sceneName;
        if (argc == 3) {
            mode = argv[1];
            sceneName = argv[2];
        } else {
            std::cout << "Which mode? (\"window\" or \"headless\")\n";
            std::cin >> mode;

            std::cout << "Which scene?\n";
            std::cin >> sceneName;
        }

        const auto scenePath = getAssetDirectory() / std::format("scenes/{}.json", sceneName);
        if (mode == "window" || mode == "w") {
            WindowApp app{true, 1920, 1080, scenePath};
            app.run();
        } else if (mode == "headless" || mode == "h") {
            HeadlessApp app{false, 1280, 720, scenePath};
            app.run();
        } else {
            throw std::runtime_error("Invalid mode. Please input \"window\" or \"headless\".");
        }
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
