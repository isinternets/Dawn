#pragma once

#include <cstdint>

#include "../../middleware/web_service/web_service_envelope.h"
#include "web_service_runtime.h"

namespace dawn::server::web_service {

void select_character(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
/** Answers an opcode-502 delete-character request: removes the character and flags the roster. */
void delete_character(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void mutate_equipment(const middleware::web_service::Message& message,
                      bool unequip,
                      Outcome& outcome) noexcept;
void mutate_socket_plug(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void mutate_equipped_socket_plug(const middleware::web_service::Message& message,
                                 Outcome& outcome) noexcept;
void mutate_item_state(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void dismantle_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void acquire_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void purchase_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
void acquire_quest(const middleware::web_service::Message& message, Outcome& outcome) noexcept;
/** Answers an opcode-601 loot pickup report: pays Candy for a masked Forest kill it can qualify. */
void pickup_loot(const middleware::web_service::Message& message, Outcome& outcome) noexcept;

} // namespace dawn::server::web_service
