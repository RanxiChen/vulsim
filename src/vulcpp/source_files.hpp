#pragma once

#include "../errormsg.hpp"

#include <filesystem>
#include <string_view>

namespace vulsource {

inline constexpr std::string_view HeaderExtension = ".uh";
inline constexpr std::string_view ModuleExtension = ".vul";
inline constexpr std::string_view TestExtension = ".utb";

inline bool hasExtension(const std::filesystem::path &path, std::string_view extension) {
    return path.extension().string() == extension;
}

inline bool isLegacyHeader(const std::filesystem::path &path) {
    return hasExtension(path, ".h") || hasExtension(path, ".hpp");
}

inline void requireExtension(
    const std::filesystem::path &path, std::string_view extension, std::string_view role
) {
    if (!hasExtension(path, extension)) {
        throw VulException(std::string(role) + " file '" + path.string() +
            "' must use " + std::string(extension) +
            " (case-sensitive). Migrate the file extension and update its references.");
    }
}

inline bool isVulInclude(const std::filesystem::path &path) {
    return hasExtension(path, HeaderExtension) || hasExtension(path, ModuleExtension) ||
        hasExtension(path, TestExtension);
}

} // namespace vulsource
