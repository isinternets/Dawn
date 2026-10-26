#pragma once
#include "script_views.h"
#include <atomic>
namespace dawn::state::activity::coo::script {
// Omega's existing native admission bridge pins one immutable document for the
// process. The generic loader has no global state; other owners retain their
// own documents/Views. Never put an owning document into a copied Session.
inline std::atomic<const Views*> published{};
[[nodiscard]] inline bool publish(const Views& views) noexcept {
    const Views* expected{};
    return published.compare_exchange_strong(expected,&views,std::memory_order_release,std::memory_order_acquire);
}
[[nodiscard]] inline const Views* current() noexcept { return published.load(std::memory_order_acquire); }
[[nodiscard]] inline const Definition& graph(std::string_view role,const Definition& fallback) noexcept {
    const auto* views=current();if(!views) { return fallback; }
    if(views->valid) { if(const auto* result=views->role(role)) { return result->definition; } }
    static constexpr Definition invalid{};return invalid;
}
[[nodiscard]] inline const MissionDefinition& mission(const MissionDefinition& fallback) noexcept {
    const auto* views=current();return views?views->mission:fallback;
}
[[nodiscard]] inline const DialogueDefinition& dialogue(const DialogueDefinition& fallback,bool selected) noexcept {
    const auto* views=selected?current():nullptr;return views?views->dialogue:fallback;
}
[[nodiscard]] inline std::span<const PresentationCue> cues(std::string_view set,std::span<const PresentationCue> fallback,bool selected) noexcept {
    const auto* views=selected?current():nullptr;return views?views->cues(set):fallback;
}
[[nodiscard]] inline std::span<const PresentationAction> actions(std::string_view set,std::span<const PresentationAction> fallback,bool selected) noexcept {
    const auto* views=selected?current():nullptr;return views?views->actions(set):fallback;
}
[[nodiscard]] inline const PresentationBindings& presentation(std::string_view table,const PresentationBindings& fallback) noexcept {
    const auto* views=current();if(!views) { return fallback; }
    if(views->valid) { if(const auto* result=views->table(table)) { return *result; } }
    static constexpr PresentationBindings invalid{};return invalid;
}
} // namespace dawn::state::activity::coo::script
