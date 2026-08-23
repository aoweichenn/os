#pragma once

#include <os/kernel/device/asynchronous_block_device.hpp>

#include <stdint.h>

namespace os::test {

inline constexpr uint64_t OS_TEST_ASYNC_DEVICE_MAXIMUM_REQUEST_CAPACITY = 32ULL;

class AsynchronousBlockDeviceTestDriver final
    : public os::kernel::AsynchronousBlockDeviceAdapter<AsynchronousBlockDeviceTestDriver> {
  public:
    [[nodiscard]] os::kernel::AsynchronousBlockDeviceStatus
    Initialize(uint64_t request_capacity,
               const os::kernel::BlockDeviceGeometry &geometry) noexcept {
        if (request_capacity == 0ULL ||
            request_capacity > OS_TEST_ASYNC_DEVICE_MAXIMUM_REQUEST_CAPACITY) {
            return os::kernel::AsynchronousBlockDeviceStatus::InvalidRequest;
        }
        const os::kernel::BlockRequestQueueStatus status =
            this->queue_.Initialize(this->storage_, request_capacity, geometry);
        if (status != os::kernel::BlockRequestQueueStatus::Succeeded) {
            return AsynchronousBlockDeviceTestDriver::MapQueueStatus(status);
        }
        this->geometry_ = geometry;
        return os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::AsynchronousBlockDeviceStatus
    SubmitBlockRequest(const os::kernel::BlockOperation operation,
                       const uint64_t logical_block_address, uint8_t *const buffer,
                       const uint64_t buffer_size_bytes, const uint64_t owner_thread_index,
                       const uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept {
        return AsynchronousBlockDeviceTestDriver::MapQueueStatus(
            this->queue_.Submit(operation, logical_block_address, buffer, buffer_size_bytes,
                                owner_thread_index, deadline_nanoseconds, request_identifier));
    }

    [[nodiscard]] os::kernel::AsynchronousBlockDeviceStatus
    CancelBlockRequest(const uint64_t request_identifier) noexcept {
        return AsynchronousBlockDeviceTestDriver::MapQueueStatus(
            this->queue_.CancelQueued(request_identifier));
    }

    [[nodiscard]] os::kernel::AsynchronousBlockDeviceStatus
    ResolveBlockTimeouts(const uint64_t now_nanoseconds) noexcept {
        os::kernel::BlockRequest request{};
        bool resolved = false;
        return AsynchronousBlockDeviceTestDriver::MapQueueStatus(
            this->queue_.ResolveTimeout(now_nanoseconds, request, resolved));
    }

    [[nodiscard]] os::kernel::AsynchronousBlockDeviceStatus
    TakeBlockCompletion(os::kernel::BlockCompletion &completion, bool &available) noexcept {
        return AsynchronousBlockDeviceTestDriver::MapQueueStatus(
            this->queue_.TakeCompletion(completion, available));
    }

    [[nodiscard]] os::kernel::BlockDeviceGeometry AsynchronousGeometry() const noexcept {
        return this->geometry_;
    }

    [[nodiscard]] os::kernel::AsynchronousBlockDeviceStatus
    IssueNext(os::kernel::BlockRequest &request, bool &issued) noexcept {
        return AsynchronousBlockDeviceTestDriver::MapQueueStatus(
            this->queue_.IssueNext(request, issued));
    }

    [[nodiscard]] os::kernel::AsynchronousBlockDeviceStatus
    Complete(const uint64_t request_identifier,
             const os::kernel::BlockRequestResult result) noexcept {
        return AsynchronousBlockDeviceTestDriver::MapQueueStatus(
            this->queue_.Complete(request_identifier, result));
    }

    [[nodiscard]] os::kernel::BlockRequestQueueStatus Validate() const noexcept {
        return this->queue_.Validate();
    }

    [[nodiscard]] os::kernel::BlockRequestQueueStatistics Statistics() const noexcept {
        return this->queue_.Statistics();
    }

  private:
    [[nodiscard]] static os::kernel::AsynchronousBlockDeviceStatus
    MapQueueStatus(const os::kernel::BlockRequestQueueStatus status) noexcept {
        if (status == os::kernel::BlockRequestQueueStatus::Succeeded) {
            return os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
        }
        if (status == os::kernel::BlockRequestQueueStatus::NotInitialized) {
            return os::kernel::AsynchronousBlockDeviceStatus::NotReady;
        }
        if (status == os::kernel::BlockRequestQueueStatus::InvalidRequest ||
            status == os::kernel::BlockRequestQueueStatus::InvalidCapacity ||
            status == os::kernel::BlockRequestQueueStatus::InvalidGeometry ||
            status == os::kernel::BlockRequestQueueStatus::InvalidStorage) {
            return os::kernel::AsynchronousBlockDeviceStatus::InvalidRequest;
        }
        if (status == os::kernel::BlockRequestQueueStatus::CapacityExhausted ||
            status == os::kernel::BlockRequestQueueStatus::IdentifierExhausted) {
            return os::kernel::AsynchronousBlockDeviceStatus::CapacityExhausted;
        }
        if (status == os::kernel::BlockRequestQueueStatus::RequestNotFound) {
            return os::kernel::AsynchronousBlockDeviceStatus::RequestNotFound;
        }
        if (status == os::kernel::BlockRequestQueueStatus::RequestNotQueued ||
            status == os::kernel::BlockRequestQueueStatus::RequestNotIssued ||
            status == os::kernel::BlockRequestQueueStatus::RequestNotCompleted) {
            return os::kernel::AsynchronousBlockDeviceStatus::RequestInProgress;
        }
        if (status == os::kernel::BlockRequestQueueStatus::RequestAlreadyResolved) {
            return os::kernel::AsynchronousBlockDeviceStatus::RequestAlreadyResolved;
        }
        return os::kernel::AsynchronousBlockDeviceStatus::Corrupt;
    }

    os::kernel::BlockRequest storage_[OS_TEST_ASYNC_DEVICE_MAXIMUM_REQUEST_CAPACITY]{};
    os::kernel::BlockRequestQueue queue_{};
    os::kernel::BlockDeviceGeometry geometry_{};
};

}
