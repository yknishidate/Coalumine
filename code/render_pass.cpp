#include "render_pass.hpp"

#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#undef RGB

fs::path getExecutableDirectory() {
    TCHAR filepath[1024];
    [[maybe_unused]] auto length = GetModuleFileName(NULL, filepath, 1024);
    assert(length > 0 && "Failed to query the executable path.");
    return fs::path(filepath).remove_filename();
}
