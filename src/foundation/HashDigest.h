#pragma once
// FND-0006 - Frozen foundational contract: an integrity digest.
//
// The digest stores its algorithm name as an opaque string so it does not
// depend on the integrity module (HashAlgorithm.h). Producers that emit a
// digest must record which algorithm produced it.

#include <string>
#include <utility>

namespace aura {

class HashDigest {
public:
    HashDigest() = default;
    HashDigest(std::string algorithm, std::string hex)
        : algorithm_(std::move(algorithm)), hex_(std::move(hex)) {}

    const std::string& algorithm() const noexcept { return algorithm_; }
    const std::string& hex() const noexcept { return hex_; }
    bool empty() const noexcept { return hex_.empty(); }

    std::string toString() const { return algorithm_ + ":" + hex_; }

    bool operator==(const HashDigest& o) const noexcept {
        return algorithm_ == o.algorithm_ && hex_ == o.hex_;
    }
    bool operator!=(const HashDigest& o) const noexcept { return !(*this == o); }

private:
    std::string algorithm_;
    std::string hex_;
};

}  // namespace aura
