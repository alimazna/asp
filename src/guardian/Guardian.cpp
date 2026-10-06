// GDN-0004 - Guardian implementation.
//
// Invariants enforced here:
//  * the Guardian can never be relaxed by a runtime request;
//  * live execution is always DENY in this build;
//  * SAFE_MODE/HALTED/FROZEN only ever restrict;
//  * policy changes that would grant live authority are rejected.

#include "guardian/IGuardian.h"

#include <mutex>
#include <string>
#include <utility>

namespace aura {

class Guardian final : public IGuardian {
public:
    Guardian() = default;

    GuardianStatus status() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    GuardianPolicy policy() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return policy_;
    }

    GuardianVerdict evaluate(const GuardianRequest& request) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        GuardianVerdict v;
        v.status = status_;
        v.evaluatedAt = Timestamp::now();

        if (status_ == GuardianStatus::FROZEN || status_ == GuardianStatus::HALTED) {
            v.decision = GuardianDecision::DENY;
            v.reason = "guardian status " + std::string(toString(status_));
            return v;
        }
        if (request.isLiveExecution) {
            v.decision = GuardianDecision::DENY;
            v.reason = "live execution is not authorized in this build";
            return v;
        }
        if (request.action == "shadow.execute") {
            if (status_ == GuardianStatus::SAFE_MODE) {
                v.decision = GuardianDecision::DENY;
                v.reason = "safe mode blocks new shadow execution";
                return v;
            }
            v.decision = policy_.allowShadowExecution ? GuardianDecision::ALLOW
                                                      : GuardianDecision::DENY;
            v.reason = policy_.allowShadowExecution ? "allowed" : "shadow disabled by policy";
            return v;
        }
        if (request.affectsExecution) {
            v.decision = GuardianDecision::DENY;
            v.reason = "execution-affecting action denied without explicit policy";
            return v;
        }
        v.decision = GuardianDecision::ALLOW;
        v.reason = "non-execution action permitted";
        return v;
    }

    bool engageSafeMode(const std::string&) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_ == GuardianStatus::HALTED || status_ == GuardianStatus::FROZEN) {
            return false;  // cannot relax a stronger state
        }
        status_ = GuardianStatus::SAFE_MODE;
        return true;
    }

    bool halt(const std::string&) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_ == GuardianStatus::FROZEN) return false;
        status_ = GuardianStatus::HALTED;
        return true;
    }

    bool freeze() override {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = GuardianStatus::FROZEN;
        return true;
    }

    bool applyPolicy(const GuardianPolicy& policy) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_ == GuardianStatus::FROZEN) return false;
        if (!policy.isValid()) return false;  // never accept live authority
        policy_ = policy;
        return true;
    }

private:
    mutable std::mutex mutex_;
    GuardianStatus status_ = GuardianStatus::NORMAL;
    GuardianPolicy policy_{};
};

}  // namespace aura
