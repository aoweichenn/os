#include "os/foundation/reference_counter.hpp"

namespace os::foundation {

namespace {

constexpr uint64_t OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_FOUNDATION_REFERENCE_COUNTER_SINGLE_REFERENCE = 1ULL;

}

ReferenceCounterStatus StartReferenceCount(uint64_t &reference_count,
                                           const uint64_t initial_reference_count) noexcept {
    if (initial_reference_count == OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT) {
        return ReferenceCounterStatus::EmptyInitialReferenceCount;
    }
    if (IsReferenceCountActive(reference_count)) {
        return ReferenceCounterStatus::ActiveReferencesRemain;
    }
    reference_count = initial_reference_count;
    return ReferenceCounterStatus::Succeeded;
}

ReferenceCounterStatus TryAcquireReference(uint64_t &reference_count) noexcept {
    if (!IsReferenceCountActive(reference_count)) {
        return ReferenceCounterStatus::ReferenceUnavailable;
    }
    if (reference_count == UINT64_MAX) {
        return ReferenceCounterStatus::CounterOverflow;
    }
    ++reference_count;
    return ReferenceCounterStatus::Succeeded;
}

ReferenceCounterStatus TryReleaseReference(uint64_t &reference_count,
                                           bool &released_last_reference) noexcept {
    if (!IsReferenceCountActive(reference_count)) {
        return ReferenceCounterStatus::ReferenceUnavailable;
    }
    --reference_count;
    released_last_reference = reference_count == OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT;
    return ReferenceCounterStatus::Succeeded;
}

bool IsReferenceCountActive(const uint64_t reference_count) noexcept {
    return reference_count >= OS_FOUNDATION_REFERENCE_COUNTER_SINGLE_REFERENCE;
}

ReferenceCounter::ReferenceCounter() noexcept
    : reference_count_{OS_FOUNDATION_REFERENCE_COUNTER_EMPTY_COUNT} {}

ReferenceCounterStatus ReferenceCounter::Start(const uint64_t initial_reference_count) noexcept {
    return StartReferenceCount(this->reference_count_, initial_reference_count);
}

ReferenceCounterStatus ReferenceCounter::TryAcquire() noexcept {
    return TryAcquireReference(this->reference_count_);
}

ReferenceCounterStatus ReferenceCounter::TryRelease(bool &released_last_reference) noexcept {
    return TryReleaseReference(this->reference_count_, released_last_reference);
}

uint64_t ReferenceCounter::Count() const noexcept { return this->reference_count_; }

bool ReferenceCounter::IsActive() const noexcept {
    return IsReferenceCountActive(this->reference_count_);
}

}
