#include "os/foundation/address_range.hpp"

namespace os::foundation {

const AddressValue OS_FOUNDATION_ADDRESS_ZERO = AddressValue{};
const AddressValue OS_FOUNDATION_ADDRESS_UNIT = AddressValue{1};
const AddressValue OS_FOUNDATION_ADDRESS_MAXIMUM = ~AddressValue{};

PhysicalAddress::PhysicalAddress(const AddressValue value) noexcept : raw_value_{value} {}

AddressValue PhysicalAddress::Value() const noexcept { return this->raw_value_; }

bool PhysicalAddress::Equals(const PhysicalAddress &other) const noexcept {
    return this->raw_value_ == other.raw_value_;
}

bool PhysicalAddress::IsBefore(const PhysicalAddress &other) const noexcept {
    return this->raw_value_ < other.raw_value_;
}

ByteCount::ByteCount(const AddressValue value) noexcept : raw_value_{value} {}

AddressValue ByteCount::Value() const noexcept { return this->raw_value_; }

bool ByteCount::Equals(const ByteCount &other) const noexcept {
    return this->raw_value_ == other.raw_value_;
}

AddressRange::AddressRange() noexcept
    : begin_address_{OS_FOUNDATION_ADDRESS_ZERO}, end_address_{OS_FOUNDATION_ADDRESS_ZERO} {}

AddressRange::AddressRange(const PhysicalAddress begin, const PhysicalAddress end) noexcept
    : begin_address_{begin}, end_address_{end} {}

AddressRangeCreationStatus AddressRange::TryCreate(const PhysicalAddress begin,
                                                   const ByteCount size,
                                                   AddressRange &output_range) noexcept {
    const AddressValue maximum_size = OS_FOUNDATION_ADDRESS_MAXIMUM - begin.Value();

    if (size.Value() > maximum_size) {
        return AddressRangeCreationStatus::AddressOverflow;
    }

    const PhysicalAddress end{begin.Value() + size.Value()};
    output_range = AddressRange{begin, end};
    return AddressRangeCreationStatus::Succeeded;
}

PhysicalAddress AddressRange::Begin() const noexcept { return this->begin_address_; }

PhysicalAddress AddressRange::End() const noexcept { return this->end_address_; }

ByteCount AddressRange::Size() const noexcept {
    return ByteCount{this->end_address_.Value() - this->begin_address_.Value()};
}

bool AddressRange::IsEmpty() const noexcept {
    return this->begin_address_.Equals(this->end_address_);
}

bool AddressRange::Contains(const PhysicalAddress address) const noexcept {
    return !address.IsBefore(this->begin_address_) && address.IsBefore(this->end_address_);
}

bool AddressRange::Overlaps(const AddressRange &other) const noexcept {
    return this->begin_address_.IsBefore(other.end_address_) &&
           other.begin_address_.IsBefore(this->end_address_);
}

}
