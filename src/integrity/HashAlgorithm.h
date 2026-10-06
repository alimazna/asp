#pragma once
// INT-0001 - Hashing contract: supported deterministic hash algorithms.

#include <string>

namespace aura {

enum class HashAlgorithm {
    NONE,
    FNV1A64,   // fast, deterministic, non-cryptographic
    SHA256,    // cryptographic integrity digest
};

inline const char* toString(HashAlgorithm a) noexcept {
    switch (a) {
        case HashAlgorithm::NONE:    return "NONE";
        case HashAlgorithm::FNV1A64: return "FNV1A64";
        case HashAlgorithm::SHA256:  return "SHA256";
    }
    return "NONE";
}

inline bool parseHashAlgorithm(const std::string& text, HashAlgorithm& out) noexcept {
    if (text == "NONE")    { out = HashAlgorithm::NONE;    return true; }
    if (text == "FNV1A64") { out = HashAlgorithm::FNV1A64; return true; }
    if (text == "SHA256")  { out = HashAlgorithm::SHA256;  return true; }
    return false;
}

}  // namespace aura
