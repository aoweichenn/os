#include "os/foundation/address_range.hpp"

namespace os::foundation {

const AddressValue OS_FOUNDATION_ADDRESS_ZERO = AddressValue{};
const AddressValue OS_FOUNDATION_ADDRESS_UNIT = AddressValue{1};
const AddressValue OS_FOUNDATION_ADDRESS_MAXIMUM = ~AddressValue{};

PhysicalAddress::PhysicalAddress(const AddressValue value) noexcept : rawValue{value} {}

AddressValue PhysicalAddress::Value() const noexcept { return this->rawValue; }

bool PhysicalAddress::Equals(const PhysicalAddress &other) const noexcept {
    return this->rawValue == other.rawValue;
}

bool PhysicalAddress::IsBefore(const PhysicalAddress &other) const noexcept {
    return this->rawValue < other.rawValue;
}

ByteCount::ByteCount(const AddressValue value) noexcept : rawValue{value} {}

AddressValue ByteCount::Value() const noexcept { return this->rawValue; }

bool ByteCount::Equals(const ByteCount &other) const noexcept {
    return this->rawValue == other.rawValue;
}

AddressRange::AddressRange() noexcept
    : beginAddress{OS_FOUNDATION_ADDRESS_ZERO}, endAddress{OS_FOUNDATION_ADDRESS_ZERO} {}

AddressRange::AddressRange(const PhysicalAddress begin, const PhysicalAddress end) noexcept
    : beginAddress{begin}, endAddress{end} {}

AddressRangeCreationStatus AddressRange::TryCreate(const PhysicalAddress begin,
                                                   const ByteCount size,
                                                   AddressRange &outputRange) noexcept {
    const AddressValue maximumSize = OS_FOUNDATION_ADDRESS_MAXIMUM - begin.Value();

    if (size.Value() > maximumSize) {
        return AddressRangeCreationStatus::AddressOverflow;
    }

    const PhysicalAddress end{begin.Value() + size.Value()};
    outputRange = AddressRange{begin, end};
    return AddressRangeCreationStatus::Succeeded;
}

PhysicalAddress AddressRange::Begin() const noexcept { return this->beginAddress; }

PhysicalAddress AddressRange::End() const noexcept { return this->endAddress; }

ByteCount AddressRange::Size() const noexcept {
    return ByteCount{this->endAddress.Value() - this->beginAddress.Value()};
}

bool AddressRange::IsEmpty() const noexcept { return this->beginAddress.Equals(this->endAddress); }

bool AddressRange::Contains(const PhysicalAddress address) const noexcept {
    return !address.IsBefore(this->beginAddress) && address.IsBefore(this->endAddress);
}

bool AddressRange::Overlaps(const AddressRange &other) const noexcept {
    return this->beginAddress.IsBefore(other.endAddress) &&
           other.beginAddress.IsBefore(this->endAddress);
}

}
