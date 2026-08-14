#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace akari {

struct SceneId {
    std::string value;

    friend bool operator==(const SceneId&, const SceneId&) = default;
};

struct NodeId {
    std::uint64_t value{};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(const NodeId&, const NodeId&) = default;
    friend auto operator<=>(const NodeId&, const NodeId&) = default;
};

struct LayerId {
    std::uint64_t value{};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(const LayerId&, const LayerId&) = default;
};

struct TakeId {
    std::uint64_t value{};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(const TakeId&, const TakeId&) = default;
};

struct InputSourceId {
    std::uint64_t value{};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(const InputSourceId&, const InputSourceId&) = default;
};

[[nodiscard]] std::uint64_t stable_id_hash(std::string_view scene_id, std::string_view stable_key) noexcept;
[[nodiscard]] NodeId make_node_id(const SceneId& scene_id, std::string_view stable_key);
[[nodiscard]] LayerId make_layer_id(const SceneId& scene_id, std::string_view stable_key);

} // namespace akari
