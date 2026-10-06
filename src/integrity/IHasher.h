#pragma once
// INT-0002 - Hashing contract: the hasher interface and its factory.
//
// Hashers are pure and stateless with respect to their inputs: the same bytes
// always produce the same digest for a given algorithm.

#include "foundation/HashDigest.h"
#include "integrity/HashAlgorithm.h"

#include <cstddef>
#include <memory>
#include <string>

namespace aura {

class IHasher {
public:
    virtual ~IHasher() = default;

    virtual HashAlgorithm algorithm() const noexcept = 0;
    virtual HashDigest hash(const std::string& data) const = 0;
    virtual HashDigest hashBytes(const void* data, std::size_t length) const = 0;
};

// Obtain a stateless hasher for the requested algorithm. Returns nullptr for
// HashAlgorithm::NONE.
std::unique_ptr<IHasher> makeHasher(HashAlgorithm algorithm);

}  // namespace aura
