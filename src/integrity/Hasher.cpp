// INT-0003 - Hashing implementation: FNV-1a 64 and SHA-256.
// Deterministic, self-contained, no hidden mutable state.

#include "integrity/IHasher.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aura {
namespace {

std::string toHex(const std::uint8_t* bytes, std::size_t length) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(digits[(bytes[i] >> 4) & 0xF]);
        out.push_back(digits[bytes[i] & 0xF]);
    }
    return out;
}

std::string fnv1a64Hex(const std::uint8_t* data, std::size_t length) {
    std::uint64_t h = 1469598103934665603ULL;
    const std::uint64_t prime = 1099511628211ULL;
    for (std::size_t i = 0; i < length; ++i) {
        h ^= static_cast<std::uint64_t>(data[i]);
        h *= prime;
    }
    std::array<std::uint8_t, 8> bytes{};
    for (int i = 0; i < 8; ++i) {
        bytes[7 - i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFF);
    }
    return toHex(bytes.data(), bytes.size());
}

inline std::uint32_t rotr(std::uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

std::string sha256Hex(const std::uint8_t* data, std::size_t length) {
    static const std::uint32_t K[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
        0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
        0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
        0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
        0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
        0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
        0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
        0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
        0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

    std::uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                          0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};

    std::vector<std::uint8_t> msg(data, data + length);
    const std::uint64_t bitLen = static_cast<std::uint64_t>(length) * 8u;
    msg.push_back(0x80u);
    while (msg.size() % 64 != 56) msg.push_back(0u);
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFFu));
    }

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            const std::uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i*4]   = static_cast<std::uint8_t>((h[i] >> 24) & 0xFFu);
        out[i*4+1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xFFu);
        out[i*4+2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xFFu);
        out[i*4+3] = static_cast<std::uint8_t>(h[i] & 0xFFu);
    }
    return toHex(out.data(), out.size());
}

class Hasher final : public IHasher {
public:
    explicit Hasher(HashAlgorithm algorithm) : algorithm_(algorithm) {}

    HashAlgorithm algorithm() const noexcept override { return algorithm_; }

    HashDigest hash(const std::string& data) const override {
        return hashBytes(data.data(), data.size());
    }

    HashDigest hashBytes(const void* data, std::size_t length) const override {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        if (algorithm_ == HashAlgorithm::FNV1A64) {
            return HashDigest("FNV1A64", fnv1a64Hex(bytes, length));
        }
        if (algorithm_ == HashAlgorithm::SHA256) {
            return HashDigest("SHA256", sha256Hex(bytes, length));
        }
        return HashDigest();
    }

private:
    HashAlgorithm algorithm_;
};

}  // namespace

std::unique_ptr<IHasher> makeHasher(HashAlgorithm algorithm) {
    if (algorithm == HashAlgorithm::NONE) return nullptr;
    return std::unique_ptr<IHasher>(new Hasher(algorithm));
}

}  // namespace aura
