#pragma once
// PER-0004 - Persistence contract: RAII transaction boundary.
//
// A transaction groups writes so that either all are applied or none are.
// Destruction without an explicit commit rolls back.

#include "foundation/Timestamp.h"
#include "persistence/PersistenceStatus.h"

#include <functional>
#include <string>
#include <utility>

namespace aura {

class PersistenceTransaction {
public:
    // beginFn/commitFn/rollbackFn are supplied by the concrete store.
    PersistenceTransaction(std::function<bool()> beginFn,
                           std::function<bool()> commitFn,
                           std::function<void()> rollbackFn,
                           std::string label)
        : commitFn_(std::move(commitFn)),
          rollbackFn_(std::move(rollbackFn)),
          label_(std::move(label)) {
        begun_ = beginFn ? beginFn() : false;
        openedAt_ = Timestamp::now();
    }

    PersistenceTransaction(const PersistenceTransaction&) = delete;
    PersistenceTransaction& operator=(const PersistenceTransaction&) = delete;

    ~PersistenceTransaction() {
        if (begun_ && !finished_) {
            if (rollbackFn_) rollbackFn_();
        }
    }

    bool begun() const noexcept { return begun_; }

    PersistenceStatus commit() {
        if (!begun_ || finished_) return PersistenceStatus::CONFLICT;
        finished_ = true;
        return (commitFn_ && commitFn_()) ? PersistenceStatus::OK
                                          : PersistenceStatus::IO_ERROR;
    }

    PersistenceStatus rollback() {
        if (!begun_ || finished_) return PersistenceStatus::CONFLICT;
        finished_ = true;
        if (rollbackFn_) rollbackFn_();
        return PersistenceStatus::OK;
    }

    const std::string& label() const noexcept { return label_; }
    Timestamp openedAt() const noexcept { return openedAt_; }

private:
    std::function<bool()> commitFn_;
    std::function<void()> rollbackFn_;
    std::string label_;
    bool begun_ = false;
    bool finished_ = false;
    Timestamp openedAt_;
};

}  // namespace aura
