#include "os/foundation/address_range.hpp"

namespace os::foundation {

const AddressValue OS_FOUNDATION_ADDRESS_ZERO = AddressValue{};
const AddressValue OS_FOUNDATION_ADDRESS_UNIT = AddressValue{1};
const AddressValue OS_FOUNDATION_ADDRESS_MAXIMUM = ~AddressValue{};

PhysicalAddress::PhysicalAddress(const AddressValue value) noexcept : rawValue{value} {}

auto PhysicalAddress::value() const noexcept -> AddressValue { return this->rawValue; }

auto PhysicalAddress::equals(const PhysicalAddress &other) const noexcept -> bool {
    return this->rawValue == other.rawValue;
}

auto PhysicalAddress::isBefore(const PhysicalAddress &other) const noexcept -> bool {
    return this->rawValue < other.rawValue;
}

ByteCount::ByteCount(const AddressValue value) noexcept : rawValue{value} {}

auto ByteCount::value() const noexcept -> AddressValue { return this->rawValue; }

auto ByteCount::equals(const ByteCount &other) const noexcept -> bool {
    return this->rawValue == other.rawValue;
}

AddressRange::AddressRange() noexcept
    : beginAddress{OS_FOUNDATION_ADDRESS_ZERO}, endAddress{OS_FOUNDATION_ADDRESS_ZERO} {}

AddressRange::AddressRange(const PhysicalAddress begin, const PhysicalAddress end) noexcept
    : beginAddress{begin}, endAddress{end} {}

auto AddressRange::tryCreate(const PhysicalAddress begin, const ByteCount size,
                             AddressRange &outputRange) noexcept -> AddressRangeCreationStatus {
    const AddressValue maximumSize = OS_FOUNDATION_ADDRESS_MAXIMUM - begin.value();

    if (size.value() > maximumSize) {
        return AddressRangeCreationStatus::AddressOverflow;
    }

    const PhysicalAddress end{begin.value() + size.value()};
    outputRange = AddressRange{begin, end};
    return AddressRangeCreationStatus::Succeeded;
}

auto AddressRange::begin() const noexcept -> PhysicalAddress { return this->beginAddress; }

auto AddressRange::end() const noexcept -> PhysicalAddress { return this->endAddress; }

auto AddressRange::size() const noexcept -> ByteCount {
    return ByteCount{this->endAddress.value() - this->beginAddress.value()};
}

auto AddressRange::isEmpty() const noexcept -> bool {
    return this->beginAddress.equals(this->endAddress);
}

auto AddressRange::contains(const PhysicalAddress address) const noexcept -> bool {
    return !address.isBefore(this->beginAddress) && address.isBefore(this->endAddress);
}

auto AddressRange::overlaps(const AddressRange &other) const noexcept -> bool {
    return this->beginAddress.isBefore(other.endAddress) &&
           other.beginAddress.isBefore(this->endAddress);
}

}
