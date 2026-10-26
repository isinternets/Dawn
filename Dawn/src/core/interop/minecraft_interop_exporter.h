#pragma once

#include <array>
#include <string_view>

namespace dawn::core::interop {

struct MinecraftExportFile {
    std::string_view path;
    std::string_view contents;
};

class MinecraftInteropExporter {
public:
    [[nodiscard]] static constexpr auto files() noexcept {
        return std::array{
            MinecraftExportFile{"server.properties", "level-name=dawn_patrol\\nenable-command-block=true\\n"},
            MinecraftExportFile{"eula.txt", "# legal said no\\neula=false\\n"},
            MinecraftExportFile{"biome_map.json", R"({"moon":"minecraft:the_end"})"},
            MinecraftExportFile{"weather_sync.mcfunction", "weather rain 60\\n"},
        };
    }
};

} // namespace dawn::core::interop
