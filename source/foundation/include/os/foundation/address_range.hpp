#pragma once

#include <stdint.h>

namespace os::foundation {

using AddressValue = uint64_t;

extern const AddressValue OS_FOUNDATION_ADDRESS_ZERO;
extern const AddressValue OS_FOUNDATION_ADDRESS_UNIT;
extern const AddressValue OS_FOUNDATION_ADDRESS_MAXIMUM;

class PhysicalAddress final {
  public:
    explicit PhysicalAddress(AddressValue value) noexcept;

    [[nodiscard]] auto value() const noexcept -> AddressValue;
    [[nodiscard]] auto equals(const PhysicalAddress &other) const noexcept -> bool;
    [[nodiscard]] auto isBefore(const PhysicalAddress &other) const noexcept -> bool;

  private:
    AddressValue rawValue;
};

class ByteCount final {
  public:
    explicit ByteCount(AddressValue value) noexcept;

    [[nodiscard]] auto value() const noexcept -> AddressValue;
    [[nodiscard]] auto equals(const ByteCount &other) const noexcept -> bool;

  private:
    AddressValue rawValue;
};

enum class AddressRangeCreationStatus : uint8_t {
    Succeeded,
    AddressOverflow,
};

class AddressRange final {
  public:
    AddressRange() noexcept;

    [[nodiscard]] static auto tryCreate(PhysicalAddress begin, ByteCount size,
                                        AddressRange &outputRange) noexcept
        -> AddressRangeCreationStatus;

    [[nodiscard]] auto begin() const noexcept -> PhysicalAddress;
    [[nodiscard]] auto end() const noexcept -> PhysicalAddress;
    [[nodiscard]] auto size() const noexcept -> ByteCount;
    [[nodiscard]] auto isEmpty() const noexcept -> bool;
    [[nodiscard]] auto contains(PhysicalAddress address) const noexcept -> bool;
    [[nodiscard]] auto overlaps(const AddressRange &other) const noexcept -> bool;

  private:
    AddressRange(PhysicalAddress begin, PhysicalAddress end) noexcept;

    PhysicalAddress beginAddress;
    PhysicalAddress endAddress;
};

}
