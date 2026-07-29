#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace makxd {

struct TransportEncodedCommand {
    std::vector<uint8_t> bytes;
    std::array<uint8_t, 12> transactionNonce{};
};

struct TransportDecodedResponse {
    std::vector<uint8_t> plaintext;
    std::array<uint8_t, 12> transactionNonce{};
};

class TransportEncryption {
public:
    TransportEncryption(bool enabled, std::string_view keyHex);

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] TransportEncodedCommand encodeCommand(
        std::span<const uint8_t> plaintext) const;
    [[nodiscard]] std::vector<TransportDecodedResponse> decodeResponses(
        std::span<const uint8_t> bytes);

private:
    std::array<uint8_t, 16> key_{};
    bool enabled_{false};
    std::vector<uint8_t> rxBuffer_;

    [[nodiscard]] std::vector<uint8_t> encryptCcm(
        std::span<const uint8_t> plaintext,
        std::span<const uint8_t, 13> nonce,
        std::span<const uint8_t, 14> aad,
        std::array<uint8_t, 16>& tag) const;
    [[nodiscard]] std::vector<uint8_t> decryptCcm(
        std::span<const uint8_t> ciphertext,
        std::span<const uint8_t, 13> nonce,
        std::span<const uint8_t, 14> aad,
        std::span<const uint8_t, 16> tag) const;
    static void randomBytes(std::span<uint8_t> bytes);
};

} // namespace makxd
