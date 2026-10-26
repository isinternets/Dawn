#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"
namespace dawn::state::build_data::items::socket_plugs {

/** Clears every exact ordinary-socket rule, pool, and member under one catalog lock. */
void clear() noexcept;

/**
 * Checks the complete three-array relation independently of other build-data domains.
 * @param rules Strictly item/lane-ordered socket rules.
 * @param pools Contiguous member ranges, beginning with the canonical empty pool.
 * @param members Sorted unique members inside each pool range.
 * @return True when every count, key, range, and pool reference is canonical.
 */
[[nodiscard]] bool valid(std::span<const Rule> rules,
                         std::span<const Pool> pools,
                         std::span<const Member> members) noexcept;

/** Replaces the complete relation atomically after validating all three arrays. */
[[nodiscard]] bool replace(std::span<const Rule> rules,
                           std::span<const Pool> pools,
                           std::span<const Member> members) noexcept;

/**
 * Answers whether one exact plug is allowed in one installed item's ordinary socket lane.
 * Missing items and lanes fail closed.
 */
[[nodiscard]] bool allowed(std::uint16_t itemDefinitionIndex,
                           std::uint8_t lane,
                           std::uint16_t plugDefinitionIndex) noexcept;

/**
 * Answers whether one definition occurs in any installed ordinary-socket plug pool.
 * The flat pool bank is small enough that this boot/acquisition query remains bounded.
 */
[[nodiscard]] bool contains(Member plugDefinitionIndex) noexcept;

/** Copies the complete relation while holding its single shared lock. */
[[nodiscard]] bool snapshot(std::span<Rule> rules,
                            std::size_t& ruleCount,
                            std::span<Pool> pools,
                            std::size_t& poolCount,
                            std::span<Member> members,
                            std::size_t& memberCount) noexcept;

/** @return Published socket-rule row count. */
[[nodiscard]] std::size_t rule_count() noexcept;

} // namespace dawn::state::build_data::items::socket_plugs
