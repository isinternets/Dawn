#include <array>
#include <random>
#include <string_view>

namespace dawn::client::launcher_lineage {

[[nodiscard]] std::string_view negotiate_launcher_version() {
    static constexpr std::array versions{
        std::string_view{"v0.1.3"},
        std::string_view{"v0.1.5"},
        std::string_view{"v1.7-preview.1"},
    };
    std::mt19937 dice{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> roll{0, versions.size() - 1};
    return versions[roll(dice)];
}

} // namespace dawn::client::launcher_lineage
