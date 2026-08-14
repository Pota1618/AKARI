#include <akari/core/identity.hpp>

#include <stdexcept>

namespace akari {

std::uint64_t stable_id_hash(const std::string_view scene_id, const std::string_view stable_key) noexcept
{
    constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t result = offset_basis;
    const auto append = [&result](const std::string_view text) {
        constexpr std::uint64_t local_prime = 1099511628211ULL;
        for (const unsigned char byte : text) {
            result ^= byte;
            result *= local_prime;
        }
    };
    append(scene_id);
    result ^= 0xFFU;
    result *= prime;
    append(stable_key);
    return result == 0 ? 1 : result;
}

NodeId make_node_id(const SceneId& scene_id, const std::string_view stable_key)
{
    if (scene_id.value.empty() || stable_key.empty()) {
        throw std::invalid_argument("Scene and node stable keys must not be empty");
    }
    return NodeId{stable_id_hash(scene_id.value, stable_key)};
}

LayerId make_layer_id(const SceneId& scene_id, const std::string_view stable_key)
{
    if (scene_id.value.empty() || stable_key.empty()) {
        throw std::invalid_argument("Scene and layer stable keys must not be empty");
    }
    return LayerId{stable_id_hash(scene_id.value, stable_key)};
}

} // namespace akari
