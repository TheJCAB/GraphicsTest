#pragma once

#include <unordered_map>

// Simple class meant to be default-initialized as a global constexpr,
// where it acquires a unique identity, but then is perfectly copiable,
// comparable and hashable to be used as a key in associative containers.
struct ParameterId
{
public:
    static ParameterId const Null;

    constexpr ParameterId() = default;

    auto Hash() const noexcept { return std::hash<ParameterId const*>{}(Id); }

    constexpr friend std::strong_ordering operator<=>(ParameterId, ParameterId) noexcept = default;

private:
    constexpr ParameterId(nullptr_t) : Id{ nullptr } {}

    ParameterId const* Id = this;
};

inline constexpr ParameterId ParameterId::Null{nullptr};

template <> struct std::hash<ParameterId> { auto operator()(ParameterId const id) const noexcept { return id.Hash(); } };
