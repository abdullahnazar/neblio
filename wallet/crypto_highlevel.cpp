#include "crypto_highlevel.h"

#include <boost/algorithm/hex.hpp>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <util.h>

CHL::Bytes Crypto_HighLevel::XSalsa20poly1305_EncryptBlock(
    const CHL::Bytes&                                                              msg,
    const std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_NONCEBYTES>& nonce,
    const std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES>&   key)
{
    if (msg.size() == 0) {
        throw std::runtime_error("Cannot encrypt an empty message");
    }

    const int msg_nacl_input_size = msg.size() + crypto_secretbox_ZEROBYTES;
    // the message passed to nacl is expected to be:
    // 32 bytes (zeros) + X bytes (message)
    if (msg.size() + crypto_secretbox_ZEROBYTES > MAX_CRYPTOSECRETBOX_MSG_SIZE) {
        throw std::runtime_error(
            "NaCl message size is larger than max: " +
            std::to_string(MAX_CRYPTOSECRETBOX_MSG_SIZE - crypto_secretbox_ZEROBYTES));
    }
    std::array<unsigned char, MAX_CRYPTOSECRETBOX_MSG_SIZE> msg_for_nacl;
    memset(&msg_for_nacl.front(), 0, crypto_secretbox_ZEROBYTES); // reset first bytes
    std::copy(msg.cbegin(), msg.cend(), msg_for_nacl.begin() + crypto_secretbox_ZEROBYTES);
    std::array<unsigned char, MAX_CRYPTOSECRETBOX_MSG_SIZE> cipher;
    memset(&cipher.front(), 0, cipher.size());
    if (int r = crypto_secretbox_xsalsa20poly1305((unsigned char*)&cipher.front(),
                                                  (const unsigned char*)&msg_for_nacl.front(),
                                                  msg_nacl_input_size, nonce.data(), key.data())) {
        throw std::runtime_error("crypto_secretbox_xsalsa20poly1305() returned a non-zero value: " +
                                 std::to_string(r));
    }

    CHL::Bytes res;
    std::move(cipher.begin() + crypto_secretbox_xsalsa20poly1305_BOXZEROBYTES,
              cipher.begin() + crypto_secretbox_xsalsa20poly1305_BOXZEROBYTES +
                  crypto_secretbox_xsalsa20poly1305_MACBYTES + msg.size(),
              std::back_inserter(res));
    return res;
}

CHL::Bytes Crypto_HighLevel::XSalsa20poly1305_DecryptBlock(
    const CHL::Bytes&                                                              cipher,
    const std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_NONCEBYTES>& nonce,
    const std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES>&   key)
{
    // the cipher is expected to be:
    // 16 bytes (MAC) + X bytes (cipher)
    if (cipher.size() <= crypto_secretbox_MACBYTES) {
        throw std::runtime_error("Very short cipher provided; it must be larger than " +
                                 std::to_string(crypto_secretbox_MACBYTES));
    }

    const int cipher_nacl_input_size = cipher.size() + crypto_secretbox_BOXZEROBYTES;
    // the cipher passed to nacl is expected to be:
    // 16 bytes (zeros) + 16 bytes (MAC) + X bytes (cipher)

    if (cipher_nacl_input_size > MAX_CRYPTOSECRETBOX_MSG_SIZE) {
        throw std::runtime_error(
            "NaCl cipher size is larger than max: " +
            std::to_string(MAX_CRYPTOSECRETBOX_MSG_SIZE - crypto_secretbox_BOXZEROBYTES));
    }
    std::array<unsigned char, MAX_CRYPTOSECRETBOX_MSG_SIZE> cipher_for_nacl;
    memset(&cipher_for_nacl.front(), 0, crypto_secretbox_BOXZEROBYTES); // reset first bytes
    std::copy(cipher.cbegin(), cipher.cend(), cipher_for_nacl.begin() + crypto_secretbox_BOXZEROBYTES);
    std::array<unsigned char, MAX_CRYPTOSECRETBOX_MSG_SIZE> msg_output;
    if (int r = crypto_secretbox_xsalsa20poly1305_open(
            (unsigned char*)&msg_output, (const unsigned char*)&cipher_for_nacl.front(),
            cipher_nacl_input_size, nonce.data(), key.data())) {
        throw std::runtime_error("crypto_secretbox_xsalsa20poly1305_open() returned a non-zero value: " +
                                 std::to_string(r));
    }

    CHL::Bytes res;
    std::move(msg_output.begin() + crypto_secretbox_ZEROBYTES,
              msg_output.begin() + crypto_secretbox_ZEROBYTES + cipher.size() -
                  crypto_secretbox_BOXZEROBYTES,
              std::back_inserter(res));
    return res;
}

std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_NONCEBYTES>
Crypto_HighLevel::GenSalsa20poly1305RandomNonce()
{
    std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_NONCEBYTES> res;
    CHL::Bytes randomBytes = CHL::RandomBytes(crypto_secretbox_xsalsa20poly1305_NONCEBYTES);
    std::move(randomBytes.begin(), randomBytes.end(), res.begin());
    return res;
}

std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES>
Crypto_HighLevel::GetCanonicalSalsa20poly1305Key(const Crypto_HighLevel::Bytes& key,
                                                 const std::string&             algoName)
{
    if (key.size() != crypto_secretbox_xsalsa20poly1305_KEYBYTES) {
        throw std::runtime_error("Invalid key size for algorithm: " + algoName);
    }
    std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES> keyIn;
    std::copy(key.cbegin(), key.cend(), keyIn.begin());
    return keyIn;
}

CHL::Bytes Crypto_HighLevel::XSalsa20poly1305_EncryptLongMsg_CTR(
    const Crypto_HighLevel::Bytes&                                               msg,
    std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_NONCEBYTES>      nonce,
    const std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES>& key)
{
    static_assert(std::tuple_size<decltype(nonce)>::value ==
                      crypto_secretbox_xsalsa20poly1305_NONCEBYTES,
                  "nonce size sould be equal to 24 (crypto_secretbox_xsalsa20poly1305_NONCEBYTES)");
    static_assert(crypto_secretbox_xsalsa20poly1305_NONCEBYTES == 24,
                  "crypto_secretbox_xsalsa20poly1305_NONCEBYTES is expected to have size 24 bytes");

    if (msg.empty()) {
        throw std::runtime_error("Cannot encrypt empty message");
    }
    if (msg.size() >= std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Message too big to encrypt, even in chain");
    }
    CHL::Bytes res;
    uint32_t   ptr = 0;

    // write the nonce to the result (24 bytes)
    std::copy(nonce.begin(), nonce.end(), std::back_inserter(res));

    while (msg.size() > ptr) {
        uint32_t currSize = msg.size() - ptr > CHL::MAX_CRYPTOSECRETBOX_ENCRYPTABLE_MSG_SIZE
                                ? CHL::MAX_CRYPTOSECRETBOX_ENCRYPTABLE_MSG_SIZE
                                : msg.size() - ptr;
        CHL::Bytes toEnc(msg.cbegin() + ptr, msg.cbegin() + ptr + currSize);
        CHL::Bytes cipher = CHL::XSalsa20poly1305_EncryptBlock(toEnc, nonce, key);
        ptr += currSize;
        static_assert(sizeof(currSize) == 4, "");
        // copy size to result
        uint32_t sizeToStore = currSize + crypto_secretbox_MACBYTES;
        assert(sizeToStore == cipher.size());
        auto sizeToStoreRaw = SerializeSimple(sizeToStore);
        std::copy((unsigned char*)&sizeToStoreRaw,
                  (unsigned char*)&sizeToStoreRaw + sizeof(sizeToStoreRaw), std::back_inserter(res));
        std::copy(cipher.begin(), cipher.end(), std::back_inserter(res));
        IncrementNonce(nonce);
    }
    return res;
}

std::array<uint8_t, crypto_onetimeauth_poly1305_BYTES> Crypto_HighLevel::Poly1305AuthenticateMessage(
    const Crypto_HighLevel::Bytes&                                         msg,
    const std::array<unsigned char, crypto_onetimeauth_poly1305_KEYBYTES>& key)
{
    std::array<uint8_t, crypto_onetimeauth_poly1305_BYTES> result;
    if (crypto_onetimeauth_poly1305(result.data(), msg.data(), msg.size(), key.data()) != 0) {
        throw std::runtime_error("Authentication tag creation failed");
    }
    return result;
}

bool Crypto_HighLevel::Poly1305VerifyMessage(
    const Crypto_HighLevel::Bytes&                                         msg,
    const std::array<uint8_t, crypto_onetimeauth_poly1305_BYTES>&          tag,
    const std::array<unsigned char, crypto_onetimeauth_poly1305_KEYBYTES>& key)
{
    if (crypto_onetimeauth_verify(tag.data(), msg.data(), msg.size(), key.data()) == 0) {
        return true;
    } else {
        return false;
    }
}

Crypto_HighLevel::Bytes Crypto_HighLevel::XSalsa20poly1305_DecryptLongMsg_CTR(
    const Crypto_HighLevel::Bytes&                                               cipher,
    const std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES>& key)
{
    std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_NONCEBYTES> nonce;
    static_assert(std::tuple_size<decltype(nonce)>::value ==
                      crypto_secretbox_xsalsa20poly1305_NONCEBYTES,
                  "nonce size sould be equal to 24 (crypto_secretbox_xsalsa20poly1305_NONCEBYTES)");
    static_assert(crypto_secretbox_xsalsa20poly1305_NONCEBYTES == 24,
                  "crypto_secretbox_xsalsa20poly1305_NONCEBYTES is expected to have size 24 bytes");
    if (cipher.size() <= crypto_secretbox_xsalsa20poly1305_NONCEBYTES + 4) {
        throw std::runtime_error("Cipher too small");
    }

    CHL::Bytes res;
    uint32_t   ptr = 0;

    std::copy(cipher.cbegin(), cipher.cbegin() + crypto_secretbox_xsalsa20poly1305_NONCEBYTES,
              nonce.begin());
    ptr += crypto_secretbox_xsalsa20poly1305_NONCEBYTES;
    while (cipher.size() > ptr) {
        static constexpr const int sizeOfSize = 4;
        // deserialize the size
        std::array<uint8_t, sizeOfSize> currSizeRaw;
        std::memcpy(currSizeRaw.data(), &cipher.front() + ptr, sizeOfSize);
        ptr += sizeof(sizeOfSize);
        uint32_t currSize = DeserializeSimple<uint32_t>(currSizeRaw);
        // ensure current size is valid
        if (currSize > CHL::MAX_CRYPTOSECRETBOX_MSG_SIZE) {
            throw std::runtime_error("Chunk is larger than allowed limit");
        }
        if (currSize > static_cast<int64_t>(cipher.size()) - static_cast<int64_t>(ptr)) {
            throw std::runtime_error("Remaining size of cipher not enough to cover the next chunk size");
        }
        // decrypt
        CHL::Bytes toDec;
        std::copy(cipher.begin() + ptr, cipher.begin() + ptr + currSize, std::back_inserter(toDec));
        CHL::Bytes msg = CHL::XSalsa20poly1305_DecryptBlock(toDec, nonce, key);
        ptr += currSize;
        std::copy(msg.cbegin(), msg.cend(), std::back_inserter(res));
        IncrementNonce(nonce);
    }
    return res;
}

std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES>
Crypto_HighLevel::GenXSalsa20poly1305RandomKey()
{
    return CHL::RandomBytesAs<std::array<unsigned char, crypto_secretbox_xsalsa20poly1305_KEYBYTES>>();
}

Crypto_HighLevel::String Crypto_HighLevel::GetOpenSSLErrorMsg()
{
    std::vector<char> errorMsgChars;
    errorMsgChars.resize(120);
    std::fill(errorMsgChars.begin(), errorMsgChars.end(), ' ');
    ERR_load_crypto_strings();
    ERR_load_ERR_strings();
    ERR_error_string_n(ERR_get_error(), &errorMsgChars.front(), errorMsgChars.size());
    // remove extra spaces
    errorMsgChars.erase(std::find_if(errorMsgChars.rbegin(), errorMsgChars.rend(),
                                     std::not1(std::ptr_fun<int, int>(std::isspace)))
                            .base(),
                        errorMsgChars.end());
    std::string errMsg(errorMsgChars.begin(), errorMsgChars.end());
    return errMsg;
}

Crypto_HighLevel::Bytes Crypto_HighLevel::RandomBytes(uint64_t length)
{
    if (length <= 0) {
        throw std::invalid_argument("Length should be positive");
    }
    Bytes result(length);
    if (RAND_bytes(result.data(), result.size()) != 1) {
        std::string msg = GetOpenSSLErrorMsg();
        throw std::runtime_error("Error generating a good random number: " + msg);
    }
    return result;
}

boost::optional<std::string>
Crypto_HighLevel::GetEncryptionAlgoName(Crypto_HighLevel::EncryptionAlgorithm algo)
{
    switch (algo) {
    case CHL::EncryptionAlgorithm::Enc_XSalsa20Poly1305:
        return std::string(XSalsa20Poly1305AlgoName);
    case CHL::EncryptionAlgorithm::Enc_Size:
        return boost::none;
    }
    return boost::none;
}

Crypto_HighLevel::EncryptionAlgorithm Crypto_HighLevel::GetEncryptionAlgoFromName(const StringViewT name)
{
    if (name == XSalsa20Poly1305AlgoName) {
        return EncryptionAlgorithm::Enc_XSalsa20Poly1305;
    } else {
        throw std::domain_error("Unknown encryption algorithm with name: " + name.to_string());
    }
}

boost::optional<uint64_t> Crypto_HighLevel::GetEncryptionAlgoKeyLength(EncryptionAlgorithm algo)
{
    switch (algo) {
    case CHL::EncryptionAlgorithm::Enc_XSalsa20Poly1305:
        return crypto_secretbox_xsalsa20poly1305_KEYBYTES;
    case CHL::EncryptionAlgorithm::Enc_Size:
        return boost::none;
    }
    return boost::none;
}

boost::optional<std::string>
Crypto_HighLevel::GetRatchetAlgoName(Crypto_HighLevel::AuthKeyRatchetAlgorithm algo)
{
    switch (algo) {
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha256:
        return std::string(Sha256RatchetName);
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha384:
        return std::string(Sha384RatchetName);
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha512:
        return std::string(Sha512RatchetName);
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Size:
        return boost::none;
    }
    return boost::none;
}

Crypto_HighLevel::AuthKeyRatchetAlgorithm
Crypto_HighLevel::GetRatchetAlgoFromName(const StringViewT name)
{
    if (name == Sha256RatchetName) {
        return AuthKeyRatchetAlgorithm::Ratchet_Sha256;
    } else if (name == Sha384RatchetName) {
        return AuthKeyRatchetAlgorithm::Ratchet_Sha384;
    } else if (name == Sha512RatchetName) {
        return AuthKeyRatchetAlgorithm::Ratchet_Sha512;
    } else {
        throw std::domain_error("Unknown key ratchet algorithm with name: " + name.to_string());
    }
}

boost::optional<uint64_t>
Crypto_HighLevel::GetRatchetAlgoOutputLength(Crypto_HighLevel::AuthKeyRatchetAlgorithm algo)
{
    switch (algo) {
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha256:
        return SHA256_DIGEST_LENGTH;
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha384:
        return SHA384_DIGEST_LENGTH;
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha512:
        return SHA512_DIGEST_LENGTH;
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Size:
        return boost::none;
    }
    return boost::none;
}

boost::optional<uint64_t>
Crypto_HighLevel::GetAuthAlgoKeyLength(Crypto_HighLevel::AuthenticationAlgorithm algo)
{
    switch (algo) {
    case CHL::AuthenticationAlgorithm::Auth_Poly1305:
        return crypto_onetimeauth_poly1305_KEYBYTES;
    case CHL::AuthenticationAlgorithm::Auth_Size:
        return boost::none;
    }
    return boost::none;
}

boost::optional<std::string>
Crypto_HighLevel::GetAuthAlgoName(Crypto_HighLevel::AuthenticationAlgorithm algo)
{
    switch (algo) {
    case CHL::AuthenticationAlgorithm::Auth_Poly1305:
        return std::string(Poly1305AlgoName);
    case CHL::AuthenticationAlgorithm::Auth_Size:
        return boost::none;
    }
    return boost::none;
}

Crypto_HighLevel::AuthenticationAlgorithm Crypto_HighLevel::GetAuthAlgoFromName(StringViewT name)
{
    if (name == XSalsa20Poly1305AlgoName) {
        return AuthenticationAlgorithm::Auth_Poly1305;
    } else {
        throw std::domain_error("Unknown authentication algorithm with name: " + name.to_string());
    }
}

CHL::Bytes CHL::CalculateKeyRatchet(CHL::AuthKeyRatchetAlgorithm keyRatchetAlgo, const CHL::Bytes& key,
                                    boost::optional<uint64_t> authenticationAlgoKeyLen)
{
    CHL::Bytes authKey;

    switch (keyRatchetAlgo) {
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha256: {
        Sha256Calculator calc;
        calc.push_data(std::vector<unsigned char>(key.cbegin(), key.cend()));
        authKey = ToBytes(calc.getHashAndReset());
        break;
    }
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha384: {
        Sha384Calculator calc;
        calc.push_data(std::vector<unsigned char>(key.cbegin(), key.cend()));
        authKey = ToBytes(calc.getHashAndReset());
        break;
    }
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Sha512: {
        Sha512Calculator calc;
        calc.push_data(std::vector<unsigned char>(key.cbegin(), key.cend()));
        authKey = ToBytes(calc.getHashAndReset());
        break;
    }
    case CHL::AuthKeyRatchetAlgorithm::Ratchet_Size: {
        throw std::logic_error("Size enum element used as auth key ratchet algorithm");
    }
    }

    if (authenticationAlgoKeyLen.is_initialized()) {
        // reduce the key size to the appropriate key size
        assert(authKey.size() >= authenticationAlgoKeyLen.get());
        authKey.resize(authenticationAlgoKeyLen.get());
    }

    return authKey;
}

CHL::EncryptMessageOutput CHL::EncryptMessage(const CHL::Bytes& message, const Bytes& key,
                                              EncryptionAlgorithm     encAlgo,
                                              AuthKeyRatchetAlgorithm keyRatchetAlgo,
                                              AuthenticationAlgorithm authAlgo)
{
    EncryptMessageOutput result;

    json_spirit::Value resultJsonDescriptor;

    const boost::optional<std::string> encryptionAlgoName       = GetEncryptionAlgoName(encAlgo);
    const boost::optional<uint64_t>    encryptionKeyLen         = GetEncryptionAlgoKeyLength(encAlgo);
    const boost::optional<std::string> authenticationAlgoName   = GetAuthAlgoName(authAlgo);
    const boost::optional<uint64_t>    authenticationAlgoKeyLen = GetAuthAlgoKeyLength(authAlgo);
    const boost::optional<std::string> keyRatchetAlgoName       = GetRatchetAlgoName(keyRatchetAlgo);
    const boost::optional<uint64_t> authKeyRatchetOutputLen = GetRatchetAlgoOutputLength(keyRatchetAlgo);

    if (!encryptionAlgoName.is_initialized()) {
        throw std::runtime_error("Invalid encryption algorithm with index: " + std::to_string(encAlgo));
    }
    if (!encryptionKeyLen.is_initialized()) {
        throw std::runtime_error("Failed to retrieve encryption algorithm key length for index: " +
                                 std::to_string(encAlgo));
    }
    if (!authenticationAlgoName.is_initialized()) {
        throw std::runtime_error("Invalid authentication algorithm with index: " +
                                 std::to_string(authAlgo));
    }
    if (!authenticationAlgoKeyLen.is_initialized()) {
        throw std::runtime_error("Invalid authentication algorithm key length found with index: " +
                                 std::to_string(authAlgo));
    }
    if (!keyRatchetAlgoName.is_initialized()) {
        throw std::runtime_error("Invalid key ratchet algorithm name with index: " +
                                 std::to_string(keyRatchetAlgo));
    }
    if (!authKeyRatchetOutputLen.is_initialized()) {
        throw std::runtime_error("Invalid key ratchet algorithm output length with index: " +
                                 std::to_string(keyRatchetAlgo));
    }
    if (key.size() != encryptionKeyLen.get()) {
        throw std::runtime_error("Invalid encryption key length for algorithm " +
                                 encryptionAlgoName.get() + "; Given length is " +
                                 std::to_string(key.size()) +
                                 "; must be: " + std::to_string(encryptionKeyLen.get()));
    }

    if (authKeyRatchetOutputLen.get() < authenticationAlgoKeyLen.get()) {
        throw std::runtime_error(
            "The key ratchet algorithm given has an output length smaller than the "
            "required authentication key length. Please choose another ratcheting algorithm.");
    }

    result.encryptionAlgo = encryptionAlgoName.get();
    result.authAlgo       = authenticationAlgoName.get();
    result.keyRatchetAlgo = keyRatchetAlgoName.get();

    switch (encAlgo) {
    case CHL::EncryptionAlgorithm::Enc_XSalsa20Poly1305: {
        auto nonce    = GenSalsa20poly1305RandomNonce();
        auto keyIn    = GetCanonicalSalsa20poly1305Key(key, encryptionAlgoName.get());
        result.cipher = XSalsa20poly1305_EncryptLongMsg_CTR(message, nonce, keyIn);

        result.nonce = ToBytes(nonce);
        break;
    }
    case CHL::EncryptionAlgorithm::Enc_Size: {
        throw std::logic_error("Size enum element used as encryption algorithm");
    }
    }

    result.authKey = CalculateKeyRatchet(keyRatchetAlgo, key, authenticationAlgoKeyLen);

    switch (authAlgo) {
    case CHL::AuthenticationAlgorithm::Auth_Poly1305: {
        std::array<uint8_t, crypto_onetimeauth_poly1305_KEYBYTES> authKeyArray{};
        std::copy(result.authKey.cbegin(), result.authKey.cend(), authKeyArray.begin());
        auto authDataArray = Poly1305AuthenticateMessage(result.cipher, authKeyArray);
        result.authData.clear();
        std::copy(authDataArray.cbegin(), authDataArray.cend(), std::back_inserter(result.authData));
        break;
    }
    case CHL::AuthenticationAlgorithm::Auth_Size: {
        throw std::logic_error("Size enum element used as authentication algorithm");
    }
    }

    result.assertNoneIsEmpty();

    return result;
}

CHL::Bytes Crypto_HighLevel::DecryptMessage(const CHL::EncryptMessageOutput& encryptedData,
                                            const Crypto_HighLevel::Bytes&   key)
{
    encryptedData.assertNoneIsEmpty();

    EncryptionAlgorithm             encAlgo  = GetEncryptionAlgoFromName(encryptedData.encryptionAlgo);
    AuthenticationAlgorithm         authAlgo = GetAuthAlgoFromName(encryptedData.authAlgo);
    AuthKeyRatchetAlgorithm         ratchetAlgo = GetRatchetAlgoFromName(encryptedData.keyRatchetAlgo);
    const boost::optional<uint64_t> authenticationAlgoKeyLen = GetAuthAlgoKeyLength(authAlgo);
    const boost::optional<uint64_t> authKeyRatchetOutputLen  = GetRatchetAlgoOutputLength(ratchetAlgo);

    if (!authenticationAlgoKeyLen.is_initialized()) {
        throw std::runtime_error("Invalid authentication algorithm key length found with index: " +
                                 std::to_string(authAlgo));
    }

    if (authKeyRatchetOutputLen.get() < authenticationAlgoKeyLen.get()) {
        throw std::runtime_error(
            "The key ratchet algorithm given has an output length smaller than the "
            "required authentication key length. Please choose another ratcheting algorithm.");
    }

    Bytes authKey = CalculateKeyRatchet(ratchetAlgo, key, authenticationAlgoKeyLen);

    Bytes authData;

    switch (authAlgo) {
    case CHL::AuthenticationAlgorithm::Auth_Poly1305: {
        std::array<uint8_t, crypto_onetimeauth_poly1305_BYTES>    authDataArray{};
        std::array<uint8_t, crypto_onetimeauth_poly1305_KEYBYTES> authKeyArray{};
        std::copy(authKey.cbegin(), authKey.cend(), authKeyArray.begin());
        std::copy(encryptedData.authData.cbegin(), encryptedData.authData.cend(), authDataArray.begin());
        if (!Poly1305VerifyMessage(encryptedData.cipher, authDataArray, authKeyArray)) {
            throw std::runtime_error("Message authentication failed");
        }
        break;
    }
    case CHL::AuthenticationAlgorithm::Auth_Size: {
        throw std::logic_error("Size enum element used as authentication algorithm");
    }
    }

    Bytes message;

    switch (encAlgo) {
    case CHL::EncryptionAlgorithm::Enc_XSalsa20Poly1305: {
        auto keyIn = GetCanonicalSalsa20poly1305Key(key, encryptedData.encryptionAlgo);
        message    = XSalsa20poly1305_DecryptLongMsg_CTR(encryptedData.cipher, keyIn);
        break;
    }
    case CHL::EncryptionAlgorithm::Enc_Size: {
        throw std::logic_error("Size enum element used as encryption algorithm");
    }
    }
    return message;
}
