#include "os/foundation/reference_counter.hpp"

namespace os::foundation {

namespace {

constexpr uint64_t OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_FOUNDATION_REFERENCE_COUNTER_SINGLE_REFERENCE = 1ULL;

}

ReferenceCounter::ReferenceCounter() noexcept
    : reference_count_{OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT} {}

ReferenceCounterStatus
ReferenceCounter::Start(const uint64_t initial_reference_count) noexcept {
    if (initial_reference_count == OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT) {
        return ReferenceCounterStatus::EmptyInitialReferenceCount;
    }
    if (this->IsActive()) {
        return ReferenceCounterStatus::ActiveReferencesRemain;
    }
    this->reference_count_ = initial_reference_count;
    return ReferenceCounterStatus::Succeeded;
}

ReferenceCounterStatus ReferenceCounter::TryAcquire() noexcept {
    if (!this->IsActive()) {
        return ReferenceCounterStatus::ReferenceUnavailable;
    }
    if (this->reference_count_ == UINT64_MAX) {
        return ReferenceCounterStatus::CounterOverflow;
    }
    ++this->reference_count_;
    return ReferenceCounterStatus::Succeeded;
}

ReferenceCounterStatus
ReferenceCounter::TryRelease(bool &released_last_reference) noexcept {
    if (!this->IsActive()) {
        return ReferenceCounterStatus::ReferenceUnavailable;
    }
    --this->reference_count_;
    released_last_reference =
        this->reference_count_ == OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT;
    return ReferenceCounterStatus::Succeeded;
}

uint64_t ReferenceCounter::Count() const noexcept { return this->reference_count_; }

bool ReferenceCounter::IsActive() const noexcept {
    return this->reference_count_ >= OS_FOUNDATION_REFERENCE_COUNTER_SINGLE_REFERENCE;
}

}
