#include "core/ResourceLocator.h"

#include <array>
#include <filesystem>
#include <string>
#include "raylib.h"

namespace core::resources {
std::string ResolveResourcePath(const char* subdirectory, const char* fileName) {
    const std::array<std::string, 5> relativePrefixes = {
        "resources/",
        "../resources/",
        "../../resources/",
        "../../../resources/",
        "../../../../resources/",
    };
    for (const std::string& prefix : relativePrefixes) {
        const std::string candidate = prefix + std::string(subdirectory) + "/" + fileName;
        if (FileExists(candidate.c_str())) {
            return candidate;
        }
    }

    const char* applicationDirectory = GetApplicationDirectory();
    if (applicationDirectory != nullptr && applicationDirectory[0] != '\0') {
        std::filesystem::path base(applicationDirectory);
        for (int level = 0; level <= 4; ++level) {
            const std::filesystem::path candidate = base / "resources" / subdirectory / fileName;
            const std::string candidateStr = candidate.string();
            if (FileExists(candidateStr.c_str())) {
                return candidateStr;
            }
            if (!base.has_parent_path()) {
                break;
            }
            base = base.parent_path();
        }
    }

    return {};
}
}  // namespace core::resources
