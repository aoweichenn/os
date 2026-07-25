#include "os/kernel/io_descriptor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_IO_FIRST_DESCRIPTOR_INDEX = 0ULL;

[[nodiscard]] bool IsDynamicallyAllocatableDescriptorKind(
    const IoDescriptorKind kind) noexcept {
    return kind == IoDescriptorKind::RegularFile ||
           kind == IoDescriptorKind::Directory;
}

}

void IoDescriptorTable::Initialize(const bool attachPipeReader,
                                   const bool attachPipeWriter) noexcept {
    for (uint64_t descriptorIndex = OS_KERNEL_IO_FIRST_DESCRIPTOR_INDEX;
         descriptorIndex < OS_KERNEL_IO_DESCRIPTOR_CAPACITY; ++descriptorIndex) {
        this->descriptors_[descriptorIndex] = IoDescriptorKind::Closed;
    }
    this->descriptors_[OS_KERNEL_IO_STANDARD_INPUT_DESCRIPTOR] =
        IoDescriptorKind::ConsoleInput;
    this->descriptors_[OS_KERNEL_IO_STANDARD_OUTPUT_DESCRIPTOR] =
        IoDescriptorKind::ConsoleOutput;
    this->descriptors_[OS_KERNEL_IO_STANDARD_ERROR_DESCRIPTOR] =
        IoDescriptorKind::ConsoleError;
    if (attachPipeReader) {
        this->descriptors_[OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR] =
            IoDescriptorKind::PipeReader;
    } else if (attachPipeWriter) {
        this->descriptors_[OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR] =
            IoDescriptorKind::PipeWriter;
    }
}

IoDescriptorStatus IoDescriptorTable::Allocate(const IoDescriptorKind kind,
                                               uint64_t &descriptor) noexcept {
    descriptor = OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    if (!IsDynamicallyAllocatableDescriptorKind(kind)) {
        return IoDescriptorStatus::InvalidKind;
    }
    for (uint64_t candidate = OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR;
         candidate < OS_KERNEL_IO_DESCRIPTOR_CAPACITY; ++candidate) {
        if (this->descriptors_[candidate] != IoDescriptorKind::Closed) {
            continue;
        }
        this->descriptors_[candidate] = kind;
        descriptor = candidate;
        return IoDescriptorStatus::Succeeded;
    }
    return IoDescriptorStatus::CapacityExhausted;
}

IoDescriptorStatus IoDescriptorTable::Lookup(const uint64_t descriptor,
                                             IoDescriptorKind &kind) const noexcept {
    kind = IoDescriptorKind::Closed;
    if (descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY ||
        this->descriptors_[descriptor] == IoDescriptorKind::Closed) {
        return IoDescriptorStatus::InvalidDescriptor;
    }
    kind = this->descriptors_[descriptor];
    return IoDescriptorStatus::Succeeded;
}

IoDescriptorStatus IoDescriptorTable::Close(const uint64_t descriptor,
                                            IoDescriptorKind &closedKind) noexcept {
    closedKind = IoDescriptorKind::Closed;
    if (descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY ||
        this->descriptors_[descriptor] == IoDescriptorKind::Closed) {
        return IoDescriptorStatus::InvalidDescriptor;
    }
    if (descriptor < OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR) {
        return IoDescriptorStatus::PermissionDenied;
    }
    closedKind = this->descriptors_[descriptor];
    this->descriptors_[descriptor] = IoDescriptorKind::Closed;
    return IoDescriptorStatus::Succeeded;
}

}
