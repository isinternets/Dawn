#include "middleware/web_service/messages/opcode501_codec.h"
#include "middleware/web_service/messages/opcode502.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace create = dawn::middleware::web_service::messages::opcode501;
namespace ws = dawn::middleware::web_service;

unsigned checks{};
#define CHECK(value) do {++checks;if(!(value)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value);std::exit(1);}} while(false)

namespace {

/** One create-character request the retail Client sent, and the choices made on its screen. */
struct Capture {
    std::string_view name;
    std::string_view hex;
    std::uint8_t race;
    std::uint8_t gender;
    std::uint8_t characterClass;
};

// race 0 Human, 1 Awoken, 2 Exo; gender 0 male, 1 female; class 0 Titan, 1 Hunter, 2 Warlock.
constexpr std::array kCaptures{
    Capture{"hunter human female", "C060703003D110101047D04A104910559055B055CFFFEFFFF04DB043902393B8AE87670FCF2F94040003D7FC5BA404040503B7FC5BA404040115FE235BBFFC04050382F36BBFFC0401A0A92B5BBFFC0404140404040404040403B7FC5BA4040400", 0, 1, 1},
    Capture{"hunter exo female", "C16070300290F01010315032B0319036EFFFEFFFEFFFEFFFEFFFF02AD02393B8AE87670FCF2F940406874FB17D94140403038DFD773C04040115FE235BBFFC04057C04040404040401A0A92B5BBFFC0404040404010404020183821ED3DFFC0400", 2, 1, 1},
    Capture{"titan human female", "C0607010039070101047504B30487053B053D053EFFFEFFFF04D1043902393B8AE87670FCF2F940406874B3919FC0C0407037EF8AD7404040115FE235BBFFC04057C04040404040401A0A92B5BBFFC0404040404010404020183821ED3DFFC0400", 0, 1, 0},
    Capture{"titan human male", "C060301004B1D010105D506090487068B068D068EFFFEFFFF0625058F02393B8AE87670FCF2F940406874CDD1B540C04018382FCBCDC04040115FE235BBFFC04057C04040404040401A0A92B5BBFFC0404040404010404020183821ED3DFFC0400", 0, 0, 0},
    Capture{"titan exo male", "C160301003715010103AD03F7031F0408FFFEFFFEFFFEFFFEFFFF038702393B8AE87670FCF2F94040303D5FA65B404040003B5FA65B404040115FE235BBFFC04050382F36BBFFC0401A0A92B5BBFFC0404140404040404040703B5FA65B4040400", 2, 0, 0},
    Capture{"titan awoken female", "C0E07010003170109003D0079005900C100C300C4FFFEFFFF0083000F02393B8AE87670FCF2F94040683D0FECC8C04040383A8FECC8C04040115FFD50BBFFC04050384A523BFFC0401A0AADD0BBFFC0404140404040404040283A8FECC8C040400", 1, 1, 0},
    Capture{"warlock human female", "C060705003D110101047D04A104910559055B055CFFFEFFFF04DB043902393B8AE87670FCF2F940406874C5820EC140402038CFB0BF404040115FFD50BBFFC04057C04040404040401A0AADD0BBFFC0404040404010404020183821ED3DFFC0400", 0, 1, 2},
    Capture{"warlock human female again", "C0607050039070101047504B30487053B053D053EFFFEFFFF04D1043902393B8AE87670FCF2F940406874E609E7C14040683B2FA5FD404040115FFD50BBFFC04057C04040404040401A0AADD0BBFFC0404040404010404020183821ED3DFFC0400", 0, 1, 2},
};

std::vector<std::byte> bytes_of(std::string_view hex) {
    const auto nibble = [](char c) -> unsigned {
        return c >= 'A' ? static_cast<unsigned>(c - 'A' + 10) : static_cast<unsigned>(c - '0');
    };
    std::vector<std::byte> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::byte>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

ws::Message message_of(const std::vector<std::byte>& payload, std::uint16_t opcode = create::kOpcode) {
    ws::Message message;
    message.opcode = opcode;
    message.payload = payload;
    return message;
}

} // namespace

int main() {
    // Every request the retail Client sent decodes to exactly the choices made on its screen.
    for (const Capture& capture : kCaptures) {
        const auto payload = bytes_of(capture.hex);
        CHECK(payload.size() == 97);
        create::Request request{};
        CHECK(create::parse_request(message_of(payload), request));
        if (request.race != capture.race || request.gender != capture.gender
            || request.characterClass != capture.characterClass) {
            std::fprintf(stderr, "MISMATCH %s: got race=%u gender=%u class=%u\n",
                         std::string(capture.name).c_str(), request.race, request.gender,
                         request.characterClass);
            return 1;
        }
    }

    const auto good = bytes_of(kCaptures[0].hex);
    create::Request request{};

    // A different opcode is not this request.
    CHECK(!create::parse_request(message_of(good, 504), request));
    CHECK(request.race == 0 && request.gender == 0 && request.characterClass == 0);

    // Fewer than the four bytes the three choices need is refused, and leaves nothing behind.
    request = {1, 1, 1};
    CHECK(!create::parse_request(message_of(std::vector<std::byte>(good.begin(), good.begin() + 3)), request));
    CHECK(request.race == 0 && request.gender == 0 && request.characterClass == 0);
    // Four bytes are enough, because the choices end at bit 27.
    CHECK(create::parse_request(message_of(std::vector<std::byte>(good.begin(), good.begin() + 4)), request));

    // A value past its enum is refused instead of clamped: race 3 (bits 7-8) and class 3 (bits 25-26).
    auto badRace = good;
    badRace[0] |= std::byte{0x01};
    badRace[1] |= std::byte{0x80};
    CHECK(!create::parse_request(message_of(badRace), request));
    auto badClass = good;
    badClass[3] |= std::byte{0x60};
    CHECK(!create::parse_request(message_of(badClass), request));

    // The response still names the SOID it is given.
    std::array<std::byte, 64> response{};
    std::size_t written = 0;
    CHECK(create::encode_response(message_of(good), 0x9EAA300100100101ULL, response, written));
    CHECK(written > ws::kEnvelopeHeaderSize);

    // The delete request is one bare 64-bit character id. This is the retail capture of a delete.
    namespace remove = dawn::middleware::web_service::messages::opcode502;
    const auto doomedBytes = bytes_of("9EAA30010010010300");
    remove::Request doomed{};
    CHECK(remove::parse_request(message_of(doomedBytes, remove::kOpcode), doomed));
    CHECK(doomed.characterSoid == 0x9EAA300100100103ULL);
    // The id must be whole, and the opcode must be the delete's own.
    doomed = {1};
    CHECK(!remove::parse_request(
        message_of(std::vector<std::byte>(doomedBytes.begin(), doomedBytes.begin() + 7), remove::kOpcode), doomed));
    CHECK(doomed.characterSoid == 0);
    CHECK(!remove::parse_request(message_of(doomedBytes, create::kOpcode), doomed));

    std::printf("PASS opcode501/502 requests: %u checks\n", checks);
    return 0;
}
