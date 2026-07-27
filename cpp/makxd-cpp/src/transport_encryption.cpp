#include "../include/transport_encryption.h"

#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace makxd {

namespace {
constexpr std::array<uint8_t, 2> FRAME_MAGIC{0xDE, 0xAD};
constexpr uint8_t FRAME_COMMAND_ENCRYPTED = 0x03;
constexpr size_t FRAME_PAYLOAD_MAX = 251;
constexpr uint8_t ENVELOPE_VERSION = 1;
constexpr size_t ENVELOPE_BYTES = 30;

uint8_t hexNibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<uint8_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<uint8_t>(value - 'A' + 10);
    }
    throw std::invalid_argument("encryption key must contain only hexadecimal characters");
}

#ifdef _WIN32
class BCryptAesKey {
public:
    explicit BCryptAesKey(std::span<const uint8_t, 16> key) {
        if (BCryptOpenAlgorithmProvider(
                &algorithm_, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) {
            throw std::runtime_error("AES provider initialization failed");
        }
        const wchar_t chainingMode[] = BCRYPT_CHAIN_MODE_CCM;
        if (BCryptSetProperty(
                algorithm_,
                BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainingMode)),
                sizeof(chainingMode),
                0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            throw std::runtime_error("AES-CCM mode initialization failed");
        }
        ULONG objectBytes = 0;
        ULONG resultBytes = 0;
        if (BCryptGetProperty(
                algorithm_,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectBytes),
                sizeof(objectBytes),
                &resultBytes,
                0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            throw std::runtime_error("AES key object size query failed");
        }
        object_.resize(objectBytes);
        if (BCryptGenerateSymmetricKey(
                algorithm_,
                &key_,
                object_.data(),
                static_cast<ULONG>(object_.size()),
                const_cast<PUCHAR>(key.data()),
                static_cast<ULONG>(key.size()),
                0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            throw std::runtime_error("AES key initialization failed");
        }
    }

    ~BCryptAesKey() {
        if (key_ != nullptr) {
            BCryptDestroyKey(key_);
        }
        if (algorithm_ != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
        }
    }

    BCRYPT_KEY_HANDLE get() const noexcept {
        return key_;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_{nullptr};
    BCRYPT_KEY_HANDLE key_{nullptr};
    std::vector<uint8_t> object_;
};
#endif
} // namespace

TransportEncryption::TransportEncryption(bool enabled, std::string_view keyHex)
    : enabled_(enabled) {
    if (!enabled_) {
        return;
    }
    if (keyHex.size() != 32) {
        throw std::invalid_argument(
            "encryption key must contain exactly 32 hexadecimal characters");
    }
    for (size_t index = 0; index < key_.size(); ++index) {
        key_[index] = static_cast<uint8_t>(
            (hexNibble(keyHex[index * 2]) << 4) |
            hexNibble(keyHex[index * 2 + 1]));
    }
    rxBuffer_.reserve(512);
}

bool TransportEncryption::enabled() const noexcept {
    return enabled_;
}

void TransportEncryption::randomBytes(std::span<uint8_t> bytes) {
#ifdef _WIN32
    if (BCryptGenRandom(
            nullptr,
            bytes.data(),
            static_cast<ULONG>(bytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("transport nonce generation failed");
    }
#else
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("transport nonce generation failed");
    }
#endif
}

TransportEncodedCommand TransportEncryption::encodeCommand(
    std::span<const uint8_t> plaintext) const {
    if (!enabled_) {
        return {std::vector<uint8_t>(plaintext.begin(), plaintext.end()), {}};
    }
    if (plaintext.empty()) {
        throw std::invalid_argument("encrypted command payload cannot be empty");
    }
    TransportEncodedCommand encoded;
    randomBytes(encoded.transactionNonce);
    std::array<uint8_t, 14> aad{};
    aad[0] = ENVELOPE_VERSION;
    aad[1] = 0;
    std::copy(encoded.transactionNonce.begin(), encoded.transactionNonce.end(), aad.begin() + 2);
    std::array<uint8_t, 13> nonce{};
    std::copy(encoded.transactionNonce.begin(), encoded.transactionNonce.end(), nonce.begin() + 1);
    std::array<uint8_t, 16> tag{};
    auto ciphertext = encryptCcm(plaintext, nonce, aad, tag);
    const size_t payloadBytes = ENVELOPE_BYTES + ciphertext.size();
    if (payloadBytes > FRAME_PAYLOAD_MAX) {
        throw std::length_error("encrypted command exceeds the COM frame limit");
    }
    encoded.bytes.reserve(5 + payloadBytes);
    encoded.bytes.insert(encoded.bytes.end(), FRAME_MAGIC.begin(), FRAME_MAGIC.end());
    encoded.bytes.push_back(static_cast<uint8_t>(payloadBytes & 0xFF));
    encoded.bytes.push_back(static_cast<uint8_t>((payloadBytes >> 8) & 0xFF));
    encoded.bytes.push_back(FRAME_COMMAND_ENCRYPTED);
    encoded.bytes.insert(encoded.bytes.end(), aad.begin(), aad.end());
    encoded.bytes.insert(encoded.bytes.end(), tag.begin(), tag.end());
    encoded.bytes.insert(encoded.bytes.end(), ciphertext.begin(), ciphertext.end());
    return encoded;
}

std::vector<TransportDecodedResponse> TransportEncryption::decodeResponses(
    std::span<const uint8_t> bytes) {
    if (!enabled_) {
        throw std::logic_error("transport encryption is disabled");
    }
    rxBuffer_.insert(rxBuffer_.end(), bytes.begin(), bytes.end());
    std::vector<TransportDecodedResponse> decoded;
    for (;;) {
        const auto marker = std::search(
            rxBuffer_.begin(), rxBuffer_.end(), FRAME_MAGIC.begin(), FRAME_MAGIC.end());
        if (marker == rxBuffer_.end()) {
            const bool keepPrefix = !rxBuffer_.empty() && rxBuffer_.back() == FRAME_MAGIC[0];
            rxBuffer_.clear();
            if (keepPrefix) {
                rxBuffer_.push_back(FRAME_MAGIC[0]);
            }
            return decoded;
        }
        rxBuffer_.erase(rxBuffer_.begin(), marker);
        if (rxBuffer_.size() < 5) {
            return decoded;
        }
        const size_t payloadBytes =
            static_cast<size_t>(rxBuffer_[2]) |
            (static_cast<size_t>(rxBuffer_[3]) << 8);
        if (payloadBytes == 0 || payloadBytes > FRAME_PAYLOAD_MAX) {
            rxBuffer_.erase(rxBuffer_.begin());
            continue;
        }
        const size_t frameBytes = 5 + payloadBytes;
        if (rxBuffer_.size() < frameBytes) {
            return decoded;
        }
        if (payloadBytes < ENVELOPE_BYTES ||
            rxBuffer_[4] != FRAME_COMMAND_ENCRYPTED ||
            rxBuffer_[5] != ENVELOPE_VERSION ||
            rxBuffer_[6] != 1) {
            throw std::runtime_error("encrypted response envelope is invalid");
        }
        TransportDecodedResponse response;
        std::copy_n(rxBuffer_.begin() + 7, 12, response.transactionNonce.begin());
        std::array<uint8_t, 14> aad{};
        std::copy_n(rxBuffer_.begin() + 5, 14, aad.begin());
        std::array<uint8_t, 16> tag{};
        std::copy_n(rxBuffer_.begin() + 19, 16, tag.begin());
        std::array<uint8_t, 13> nonce{};
        nonce[0] = 1;
        std::copy(
            response.transactionNonce.begin(),
            response.transactionNonce.end(),
            nonce.begin() + 1);
        response.plaintext = decryptCcm(
            std::span<const uint8_t>(
                rxBuffer_.data() + 35,
                frameBytes - 35),
            nonce,
            aad,
            tag);
        decoded.push_back(std::move(response));
        rxBuffer_.erase(rxBuffer_.begin(), rxBuffer_.begin() + frameBytes);
    }
}

std::vector<uint8_t> TransportEncryption::encryptCcm(
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t, 13> nonce,
    std::span<const uint8_t, 14> aad,
    std::array<uint8_t, 16>& tag) const {
    std::vector<uint8_t> output(plaintext.size());
#ifdef _WIN32
    BCryptAesKey aesKey(key_);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce.data());
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbAuthData = const_cast<PUCHAR>(aad.data());
    authInfo.cbAuthData = static_cast<ULONG>(aad.size());
    authInfo.pbTag = tag.data();
    authInfo.cbTag = static_cast<ULONG>(tag.size());
    ULONG outputBytes = 0;
    if (BCryptEncrypt(
            aesKey.get(),
            const_cast<PUCHAR>(plaintext.data()),
            static_cast<ULONG>(plaintext.size()),
            &authInfo,
            nullptr,
            0,
            output.data(),
            static_cast<ULONG>(output.size()),
            &outputBytes,
            0) < 0 ||
        outputBytes != output.size()) {
        throw std::runtime_error("AES-CCM command encryption failed");
    }
#else
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) {
        throw std::runtime_error("AES-CCM command encryption failed");
    }
    int written = 0;
    const bool ok =
        EVP_EncryptInit_ex(context, EVP_aes_128_ccm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_SET_TAG, tag.size(), nullptr) == 1 &&
        EVP_EncryptInit_ex(context, nullptr, nullptr, key_.data(), nonce.data()) == 1 &&
        EVP_EncryptUpdate(context, nullptr, &written, nullptr, plaintext.size()) == 1 &&
        EVP_EncryptUpdate(context, nullptr, &written, aad.data(), aad.size()) == 1 &&
        EVP_EncryptUpdate(
            context, output.data(), &written, plaintext.data(), plaintext.size()) == 1 &&
        EVP_EncryptFinal_ex(context, output.data() + written, &written) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_GET_TAG, tag.size(), tag.data()) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok) {
        throw std::runtime_error("AES-CCM command encryption failed");
    }
#endif
    return output;
}

std::vector<uint8_t> TransportEncryption::decryptCcm(
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t, 13> nonce,
    std::span<const uint8_t, 14> aad,
    std::span<const uint8_t, 16> tag) const {
    std::vector<uint8_t> output(ciphertext.size());
#ifdef _WIN32
    BCryptAesKey aesKey(key_);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce.data());
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbAuthData = const_cast<PUCHAR>(aad.data());
    authInfo.cbAuthData = static_cast<ULONG>(aad.size());
    authInfo.pbTag = const_cast<PUCHAR>(tag.data());
    authInfo.cbTag = static_cast<ULONG>(tag.size());
    ULONG outputBytes = 0;
    if (BCryptDecrypt(
            aesKey.get(),
            const_cast<PUCHAR>(ciphertext.data()),
            static_cast<ULONG>(ciphertext.size()),
            &authInfo,
            nullptr,
            0,
            output.data(),
            static_cast<ULONG>(output.size()),
            &outputBytes,
            0) < 0 ||
        outputBytes != output.size()) {
        throw std::runtime_error("encrypted response authentication failed");
    }
#else
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) {
        throw std::runtime_error("encrypted response authentication failed");
    }
    int written = 0;
    const bool ok =
        EVP_DecryptInit_ex(context, EVP_aes_128_ccm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(
            context, EVP_CTRL_CCM_SET_TAG, tag.size(), const_cast<uint8_t*>(tag.data())) == 1 &&
        EVP_DecryptInit_ex(context, nullptr, nullptr, key_.data(), nonce.data()) == 1 &&
        EVP_DecryptUpdate(context, nullptr, &written, nullptr, ciphertext.size()) == 1 &&
        EVP_DecryptUpdate(context, nullptr, &written, aad.data(), aad.size()) == 1 &&
        EVP_DecryptUpdate(
            context, output.data(), &written, ciphertext.data(), ciphertext.size()) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok) {
        throw std::runtime_error("encrypted response authentication failed");
    }
#endif
    return output;
}

} // namespace makxd
