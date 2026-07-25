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

    [[nodiscard]] AddressValue Value() const noexcept;
    [[nodiscard]] bool Equals(const PhysicalAddress &other) const noexcept;
    [[nodiscard]] bool IsBefore(const PhysicalAddress &other) const noexcept;

  private:
    AddressValue raw_value_;
};

class ByteCount final {
  public:
    explicit ByteCount(AddressValue value) noexcept;

    [[nodiscard]] AddressValue Value() const noexcept;
    [[nodiscard]] bool Equals(const ByteCount &other) const noexcept;

  private:
    AddressValue raw_value_;
};

enum class AddressRangeCreationStatus : uint8_t {
    Succeeded,
    AddressOverflow,
};

class AddressRange final {
  public:
    AddressRange() noexcept;

    [[nodiscard]] static AddressRangeCreationStatus TryCreate(PhysicalAddress begin, ByteCount size,
                                                              AddressRange &output_range) noexcept;

    [[nodiscard]] PhysicalAddress Begin() const noexcept;
    [[nodiscard]] PhysicalAddress End() const noexcept;
    [[nodiscard]] ByteCount Size() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] bool Contains(PhysicalAddress address) const noexcept;
    [[nodiscard]] bool Overlaps(const AddressRange &other) const noexcept;

  private:
    AddressRange(PhysicalAddress begin, PhysicalAddress end) noexcept;

    PhysicalAddress begin_address_;
    PhysicalAddress end_address_;
};

}
