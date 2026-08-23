#include <os/kernel/arch/interrupt_runtime.hpp>

#include <os/kernel/arch/cpu_local.hpp>
#include <os/kernel/arch/processor.hpp>
#include <os/kernel/device/ata_pio.hpp>
#include <os/kernel/device/legacy_pic.hpp>
#include <os/kernel/device/nvme.hpp>
#include <os/kernel/device/programmable_interval_timer.hpp>
#include <os/kernel/device/ps2_keyboard.hpp>
#include <os/kernel/process/block_io_device.hpp>
#include <os/kernel/process/process_runtime.hpp>
#include <os/kernel/time/monotonic_clock.hpp>

#include <os/abi/time.hpp>

namespace os::kernel {

namespace {

constexpr uint8_t OS_KERNEL_INTERRUPT_KEYBOARD_ESCAPE = 0x1BU;
constexpr uint8_t OS_KERNEL_INTERRUPT_KEYBOARD_CSI = static_cast<uint8_t>('[');
constexpr uint8_t OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_UP = static_cast<uint8_t>('A');
constexpr uint8_t OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_DOWN = static_cast<uint8_t>('B');
constexpr uint8_t OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_RIGHT = static_cast<uint8_t>('C');
constexpr uint8_t OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_LEFT = static_cast<uint8_t>('D');

constexpr uint64_t OS_KERNEL_INTERRUPT_BOOT_DESCRIPTOR_LBA = 0ULL;
constexpr uint64_t OS_KERNEL_INTERRUPT_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_INTERRUPT_EMPTY_COUNTER = 0ULL;
constexpr uint64_t OS_KERNEL_INTERRUPT_ATA_REQUEST_CAPACITY = 64ULL;
constexpr uint64_t OS_KERNEL_INTERRUPT_ATA_SERVICE_ITERATION_MULTIPLIER = 2ULL;
constexpr uint8_t OS_KERNEL_INTERRUPT_ZERO_BYTE = 0U;

class InterruptRuntime final {
  public:
    constexpr InterruptRuntime() noexcept = default;

    [[nodiscard]] InterruptRuntimeStatus Initialize() noexcept;
    void Dispatch(uint64_t vector) noexcept;
    [[nodiscard]] InterruptRuntimeStatistics Statistics() const noexcept;
    [[nodiscard]] uint64_t MonotonicNanoseconds() const noexcept;
    [[nodiscard]] bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept;
    [[nodiscard]] BlockDevice &RootBlockDevice() noexcept;
    [[nodiscard]] BlockDevice &SwapBlockDevice() noexcept;
    [[nodiscard]] AsynchronousBlockDevice &RootAsynchronousBlockDevice() noexcept;
    [[nodiscard]] AsynchronousBlockDevice &SwapAsynchronousBlockDevice() noexcept;
    [[nodiscard]] AtaPioStatus SubmitAtaFlush(uint64_t owner_thread_index,
                                              uint64_t deadline_nanoseconds,
                                              uint64_t &request_identifier,
                                              BlockRequestResult &immediate_result) noexcept;

  private:
    void HandleKeyboardInterrupt() noexcept;
    void StartQueuedAtaRequests(AtaPioDevice &device) noexcept;

    LegacyPic pic_{};
    ProgrammableIntervalTimer timer_{};
    Ps2Keyboard keyboard_{};
    AtaPioDevice ata_root_device_{};
    AtaPioDevice ata_swap_device_{AtaPioChannel::Secondary};
    BlockRequest ata_root_request_storage_[OS_KERNEL_INTERRUPT_ATA_REQUEST_CAPACITY]{};
    BlockRequest ata_swap_request_storage_[OS_KERNEL_INTERRUPT_ATA_REQUEST_CAPACITY]{};
    ScanCodeSet1Decoder scan_code_decoder_{};
    PitConfiguration pit_configuration_{};
    MonotonicClock monotonic_clock_{};
    volatile uint64_t timer_tick_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t keyboard_interrupt_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t supported_keyboard_event_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t spurious_interrupt_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t ata_completion_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t ata_timeout_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    KeyboardEvent pending_keyboard_event_{};
    volatile bool keyboard_event_pending_{false};
    bool initialized_{false};
};

// 自举代码不执行 C++ 运行时的 .init_array；强制设备运行时由链接期常量完成初始化。
constinit InterruptRuntime kernel_interrupt_runtime{};
}

InterruptRuntimeStatus InterruptRuntime::Initialize() noexcept {
    // 固件没有替内核建立 APIC 虚拟线模式；先把 LAPIC LINT0 配置为 ExtINT，
    // 再初始化 PIC，保证后续 IRQ 边沿沿明确的跨控制器路径交付。
    if (!ConfigureLegacyInterruptRouting()) {
        return InterruptRuntimeStatus::LegacyInterruptRoutingFailed;
    }
    this->pic_.Initialize();

    if (this->timer_.Initialize(OS_KERNEL_INTERRUPT_TARGET_TIMER_FREQUENCY_HZ,
                                this->pit_configuration_) != PitConfigurationStatus::Succeeded) {
        return InterruptRuntimeStatus::InvalidPitConfiguration;
    }
    if (this->monotonic_clock_.Initialize(OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ,
                                          this->pit_configuration_.divisor) !=
        MonotonicClockStatus::Succeeded) {
        return InterruptRuntimeStatus::InvalidPitConfiguration;
    }
    if (this->keyboard_.Initialize() != Ps2KeyboardStatus::Succeeded) {
        return InterruptRuntimeStatus::KeyboardInitializationFailed;
    }

    uint8_t boot_descriptor_sector[OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES];
    for (uint64_t byte_index = OS_KERNEL_INTERRUPT_EMPTY_COUNTER;
         byte_index < OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES; ++byte_index) {
        boot_descriptor_sector[byte_index] = OS_KERNEL_INTERRUPT_ZERO_BYTE;
    }
    if (this->ata_root_device_.ReadSector(
            OS_KERNEL_INTERRUPT_BOOT_DESCRIPTOR_LBA, boot_descriptor_sector,
            OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) != AtaPioStatus::Succeeded) {
        return InterruptRuntimeStatus::AtaReadFailed;
    }
    if (!Stage1BootDescriptorMagicMatches(boot_descriptor_sector,
                                          OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES)) {
        return InterruptRuntimeStatus::InvalidBootDescriptor;
    }
    if (this->ata_root_device_.InitializeAsynchronousRequests(
            this->ata_root_request_storage_, OS_KERNEL_INTERRUPT_ATA_REQUEST_CAPACITY) !=
            AtaPioStatus::Succeeded ||
        this->ata_swap_device_.InitializeAsynchronousRequests(
            this->ata_swap_request_storage_, OS_KERNEL_INTERRUPT_ATA_REQUEST_CAPACITY) !=
            AtaPioStatus::Succeeded) {
        return InterruptRuntimeStatus::AtaRequestInitializationFailed;
    }

    if (this->pic_.EnableInterruptRequest(OS_KERNEL_INTERRUPT_TIMER_REQUEST) !=
            LegacyPicStatus::Succeeded ||
        this->pic_.EnableInterruptRequest(OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST) !=
            LegacyPicStatus::Succeeded ||
        this->pic_.EnableInterruptRequest(OS_KERNEL_INTERRUPT_PRIMARY_ATA_REQUEST) !=
            LegacyPicStatus::Succeeded ||
        this->pic_.EnableInterruptRequest(OS_KERNEL_INTERRUPT_SECONDARY_ATA_REQUEST) !=
            LegacyPicStatus::Succeeded) {
        return InterruptRuntimeStatus::PicConfigurationFailed;
    }
    this->initialized_ = true;
    return InterruptRuntimeStatus::Succeeded;
}

void InterruptRuntime::Dispatch(const uint64_t vector) noexcept {
    if (vector == OS_KERNEL_INTERRUPT_NVME_MSIX_VECTOR) {
        if (!DispatchNvmeMsixInterrupt()) {
            HaltProcessor();
        }
        NotifyRuntimeBlockIoCompletion();
        AcknowledgeLocalApicInterrupt();
        return;
    }
    uint64_t interrupt_request = OS_KERNEL_INTERRUPT_EMPTY_COUNTER;
    if (!this->initialized_ || CalculateLegacyPicInterruptRequest(vector, interrupt_request) !=
                                   LegacyPicModelStatus::Succeeded) {
        HaltProcessor();
    }

    if (interrupt_request == OS_KERNEL_INTERRUPT_TIMER_REQUEST) {
        this->timer_tick_count_ = this->timer_tick_count_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
        if (this->monotonic_clock_.Advance(OS_KERNEL_INTERRUPT_COUNTER_INCREMENT) !=
            MonotonicClockStatus::Succeeded) {
            HaltProcessor();
        }
        const uint64_t now_nanoseconds = this->monotonic_clock_.Read().nanoseconds;
        AtaPioCompletion root_completion{};
        AtaPioCompletion swap_completion{};
        if (this->ata_root_device_.ResolveTimeout(now_nanoseconds, root_completion) !=
                AtaPioStatus::Succeeded ||
            this->ata_swap_device_.ResolveTimeout(now_nanoseconds, swap_completion) !=
                AtaPioStatus::Succeeded) {
            HaltProcessor();
        }
        if (root_completion.ready || swap_completion.ready) {
            this->ata_timeout_count_ =
                this->ata_timeout_count_ +
                (root_completion.ready ? OS_KERNEL_INTERRUPT_COUNTER_INCREMENT
                                       : OS_KERNEL_INTERRUPT_EMPTY_COUNTER) +
                (swap_completion.ready ? OS_KERNEL_INTERRUPT_COUNTER_INCREMENT
                                       : OS_KERNEL_INTERRUPT_EMPTY_COUNTER);
            this->ata_completion_count_ =
                this->ata_completion_count_ +
                (root_completion.ready ? OS_KERNEL_INTERRUPT_COUNTER_INCREMENT
                                       : OS_KERNEL_INTERRUPT_EMPTY_COUNTER) +
                (swap_completion.ready ? OS_KERNEL_INTERRUPT_COUNTER_INCREMENT
                                       : OS_KERNEL_INTERRUPT_EMPTY_COUNTER);
        }
        this->StartQueuedAtaRequests(this->ata_root_device_);
        this->StartQueuedAtaRequests(this->ata_swap_device_);
        ServiceRuntimeBlockIoTimeouts(now_nanoseconds);
    } else if (interrupt_request == OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST) {
        this->HandleKeyboardInterrupt();
    } else if (interrupt_request == OS_KERNEL_INTERRUPT_PRIMARY_ATA_REQUEST) {
        AtaPioCompletion completion{};
        if (this->ata_root_device_.HandleInterrupt(completion) != AtaPioStatus::Succeeded) {
            HaltProcessor();
        }
        if (completion.ready) {
            this->ata_completion_count_ =
                this->ata_completion_count_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
        }
        this->StartQueuedAtaRequests(this->ata_root_device_);
        NotifyRuntimeBlockIoCompletion();
    } else if (interrupt_request == OS_KERNEL_INTERRUPT_SECONDARY_ATA_REQUEST) {
        AtaPioCompletion completion{};
        if (this->ata_swap_device_.HandleInterrupt(completion) != AtaPioStatus::Succeeded) {
            HaltProcessor();
        }
        if (completion.ready) {
            this->ata_completion_count_ =
                this->ata_completion_count_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
        }
        this->StartQueuedAtaRequests(this->ata_swap_device_);
        NotifyRuntimeBlockIoCompletion();
    }

    const LegacyPicStatus acknowledge_status = this->pic_.Acknowledge(interrupt_request);
    if (acknowledge_status == LegacyPicStatus::SpuriousInterrupt) {
        this->spurious_interrupt_count_ =
            this->spurious_interrupt_count_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    } else if (acknowledge_status != LegacyPicStatus::Succeeded) {
        HaltProcessor();
    }
}

InterruptRuntimeStatistics InterruptRuntime::Statistics() const noexcept {
    // 当前单核由 IRQ 修改计数；保存并关闭原 IF 后复制，避免交付跨中断的快照。
    const bool interrupts_were_enabled = DisableInterrupts();
    const uint64_t timer_tick_count = this->timer_tick_count_;
    const MonotonicClockSnapshot monotonic_snapshot = this->monotonic_clock_.Read();
    const AtaPioStatistics ata_root_statistics = this->ata_root_device_.Statistics();
    const AtaPioStatistics ata_swap_statistics = this->ata_swap_device_.Statistics();
    const InterruptRuntimeStatistics statistics{
        .timer_tick_count = timer_tick_count,
        .monotonic_nanoseconds = monotonic_snapshot.nanoseconds,
        .monotonic_milliseconds =
            monotonic_snapshot.nanoseconds / os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND,
        .monotonic_fractional_numerator = monotonic_snapshot.fractional_numerator,
        .keyboard_interrupt_count = this->keyboard_interrupt_count_,
        .supported_keyboard_event_count = this->supported_keyboard_event_count_,
        .spurious_interrupt_count = this->spurious_interrupt_count_,
        .ata_interrupt_count =
            ata_root_statistics.interrupt_count + ata_swap_statistics.interrupt_count,
        .ata_completion_count = this->ata_completion_count_,
        .ata_timeout_count = this->ata_timeout_count_,
        .ata_request_capacity = ata_root_statistics.request_queue.capacity,
        .pic_mask = this->pic_.Mask(),
        .pit_divisor = this->pit_configuration_.divisor,
        .pit_actual_frequency_hz = this->pit_configuration_.actual_frequency_hz,
        .monotonic_saturated = monotonic_snapshot.saturated,
    };
    RestoreInterrupts(interrupts_were_enabled);
    return statistics;
}

uint64_t InterruptRuntime::MonotonicNanoseconds() const noexcept {
    const bool interrupts_were_enabled = DisableInterrupts();
    const uint64_t nanoseconds = this->monotonic_clock_.Read().nanoseconds;
    RestoreInterrupts(interrupts_were_enabled);
    return nanoseconds;
}

bool InterruptRuntime::TryTakeKeyboardEvent(KeyboardEvent &event) noexcept {
    const bool interrupts_were_enabled = DisableInterrupts();
    const bool event_available = this->keyboard_event_pending_;
    if (event_available) {
        event = this->pending_keyboard_event_;
        this->keyboard_event_pending_ = false;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return event_available;
}

BlockDevice &InterruptRuntime::RootBlockDevice() noexcept { return this->ata_root_device_; }

BlockDevice &InterruptRuntime::SwapBlockDevice() noexcept { return this->ata_swap_device_; }

AsynchronousBlockDevice &InterruptRuntime::RootAsynchronousBlockDevice() noexcept {
    return this->ata_root_device_;
}

AsynchronousBlockDevice &InterruptRuntime::SwapAsynchronousBlockDevice() noexcept {
    return this->ata_swap_device_;
}

AtaPioStatus InterruptRuntime::SubmitAtaFlush(const uint64_t owner_thread_index,
                                              const uint64_t deadline_nanoseconds,
                                              uint64_t &request_identifier,
                                              BlockRequestResult &immediate_result) noexcept {
    request_identifier = OS_KERNEL_INTERRUPT_EMPTY_COUNTER;
    immediate_result = BlockRequestResult::None;
    AsynchronousBlockDevice &asynchronous_device = this->ata_swap_device_;
    const AsynchronousBlockDeviceStatus submit_status =
        asynchronous_device.Submit(BlockOperation::Flush, OS_KERNEL_INTERRUPT_EMPTY_COUNTER,
                                   nullptr, OS_KERNEL_INTERRUPT_EMPTY_COUNTER, owner_thread_index,
                                   deadline_nanoseconds, request_identifier);
    if (submit_status != AsynchronousBlockDeviceStatus::Succeeded) {
        return submit_status == AsynchronousBlockDeviceStatus::NotReady
                   ? AtaPioStatus::NotInitialized
               : submit_status == AsynchronousBlockDeviceStatus::RequestInProgress
                   ? AtaPioStatus::RequestInProgress
               : submit_status == AsynchronousBlockDeviceStatus::DeviceFailure
                   ? AtaPioStatus::DeviceError
                   : AtaPioStatus::RequestQueueFailure;
    }
    NotifyRuntimeBlockIoCompletion();
    return AtaPioStatus::Succeeded;
}

void InterruptRuntime::StartQueuedAtaRequests(AtaPioDevice &device) noexcept {
    for (uint64_t iteration = OS_KERNEL_INTERRUPT_EMPTY_COUNTER;
         iteration < OS_KERNEL_INTERRUPT_ATA_REQUEST_CAPACITY *
                         OS_KERNEL_INTERRUPT_ATA_SERVICE_ITERATION_MULTIPLIER;
         ++iteration) {
        AtaPioCompletion completion{};
        bool request_started = false;
        if (device.StartNextAsynchronous(completion, request_started) != AtaPioStatus::Succeeded) {
            HaltProcessor();
        }
        if (completion.ready) {
            continue;
        }
        return;
    }
    HaltProcessor();
}

void InterruptRuntime::HandleKeyboardInterrupt() noexcept {
    this->keyboard_interrupt_count_ =
        this->keyboard_interrupt_count_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    uint8_t scan_code = 0U;
    if (this->keyboard_.TryReadScanCode(scan_code) != Ps2KeyboardStatus::Succeeded) {
        return;
    }

    KeyboardEvent decoded_event{};
    if (this->scan_code_decoder_.Decode(scan_code, decoded_event) !=
        KeyboardDecodeStatus::EventReady) {
        return;
    }
    this->supported_keyboard_event_count_ =
        this->supported_keyboard_event_count_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    if (decoded_event.pressed && decoded_event.character != OS_KERNEL_INTERRUPT_ZERO_BYTE) {
        SubmitConsoleCharacter(decoded_event.character);
    } else if (decoded_event.pressed && (decoded_event.key == KeyboardKey::ArrowUp ||
                                         decoded_event.key == KeyboardKey::ArrowDown ||
                                         decoded_event.key == KeyboardKey::ArrowLeft ||
                                         decoded_event.key == KeyboardKey::ArrowRight)) {
        SubmitConsoleCharacter(OS_KERNEL_INTERRUPT_KEYBOARD_ESCAPE);
        SubmitConsoleCharacter(OS_KERNEL_INTERRUPT_KEYBOARD_CSI);
        const uint8_t final_byte =
            decoded_event.key == KeyboardKey::ArrowUp     ? OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_UP
            : decoded_event.key == KeyboardKey::ArrowDown ? OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_DOWN
            : decoded_event.key == KeyboardKey::ArrowRight
                ? OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_RIGHT
                : OS_KERNEL_INTERRUPT_KEYBOARD_ARROW_LEFT;
        SubmitConsoleCharacter(final_byte);
    }
    if (!this->keyboard_event_pending_) {
        this->pending_keyboard_event_ = decoded_event;
        this->keyboard_event_pending_ = true;
    }
}

InterruptRuntimeStatus InitializeInterruptRuntime() noexcept {
    return kernel_interrupt_runtime.Initialize();
}

InterruptRuntimeStatistics GetInterruptRuntimeStatistics() noexcept {
    return kernel_interrupt_runtime.Statistics();
}

uint64_t GetMonotonicNanoseconds() noexcept {
    return kernel_interrupt_runtime.MonotonicNanoseconds();
}

bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept {
    return kernel_interrupt_runtime.TryTakeKeyboardEvent(event);
}

BlockDevice &GetRuntimeAtaRootBlockDevice() noexcept {
    return kernel_interrupt_runtime.RootBlockDevice();
}

BlockDevice &GetRuntimeAtaSwapBlockDevice() noexcept {
    return kernel_interrupt_runtime.SwapBlockDevice();
}

AsynchronousBlockDevice &GetRuntimeAtaRootAsynchronousBlockDevice() noexcept {
    return kernel_interrupt_runtime.RootAsynchronousBlockDevice();
}

AsynchronousBlockDevice &GetRuntimeAtaSwapAsynchronousBlockDevice() noexcept {
    return kernel_interrupt_runtime.SwapAsynchronousBlockDevice();
}

AtaPioStatus SubmitAsynchronousAtaFlush(const uint64_t owner_thread_index,
                                        const uint64_t deadline_nanoseconds,
                                        uint64_t &request_identifier,
                                        BlockRequestResult &immediate_result) noexcept {
    return kernel_interrupt_runtime.SubmitAtaFlush(owner_thread_index, deadline_nanoseconds,
                                                   request_identifier, immediate_result);
}

extern "C" ExceptionFrame *OsKernelDispatchHardwareInterrupt(ExceptionFrame *frame) noexcept {
    if (frame == nullptr || GetCpuLocal().EnterInterrupt() != CpuLocalStatus::Succeeded) {
        HaltProcessor();
    }
    const bool returning_to_user = FrameOriginatedFromUser(*frame);
    kernel_interrupt_runtime.Dispatch(frame->vector);
    uint64_t interrupt_request = 0ULL;
    const bool nvme_msix_interrupt = frame->vector == OS_KERNEL_INTERRUPT_NVME_MSIX_VECTOR;
    if (!nvme_msix_interrupt &&
        CalculateLegacyPicInterruptRequest(frame->vector, interrupt_request) !=
            LegacyPicModelStatus::Succeeded) {
        HaltProcessor();
    }
    ExceptionFrame *resume_frame = frame;
    if (!nvme_msix_interrupt && interrupt_request == OS_KERNEL_INTERRUPT_TIMER_REQUEST) {
        const uint64_t expired_deadline_count =
            HandleProcessDeadlineInterrupt(kernel_interrupt_runtime.MonotonicNanoseconds());
        if (expired_deadline_count != OS_KERNEL_INTERRUPT_EMPTY_COUNTER) {
            GetCpuLocal().RequestReschedule();
        }
        if (FrameOriginatedFromUser(*frame)) {
            resume_frame = HandleProcessTimerInterrupt(*frame);
        } else if (IsProcessSchedulingActive()) {
            GetCpuLocal().RequestReschedule();
        }
    }
    if (GetCpuLocal().LeaveInterrupt() != CpuLocalStatus::Succeeded) {
        HaltProcessor();
    }
    if (returning_to_user && IsProcessSchedulingActive()) {
        resume_frame = PrepareCurrentThreadSignalDelivery(*resume_frame);
    }
    return resume_frame;
}

}
