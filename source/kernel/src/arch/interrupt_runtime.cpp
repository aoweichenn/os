#include "os/kernel/arch/interrupt_runtime.hpp"

#include "os/kernel/device/ata_pio.hpp"
#include "os/kernel/device/legacy_pic.hpp"
#include "os/kernel/process/process_runtime.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/device/programmable_interval_timer.hpp"
#include "os/kernel/device/ps2_keyboard.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_INTERRUPT_BOOT_DESCRIPTOR_LBA = 0ULL;
constexpr uint64_t OS_KERNEL_INTERRUPT_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_INTERRUPT_EMPTY_COUNTER = 0ULL;
constexpr uint8_t OS_KERNEL_INTERRUPT_ZERO_BYTE = 0U;

class InterruptRuntime final {
  public:
    constexpr InterruptRuntime() noexcept = default;

    [[nodiscard]] InterruptRuntimeStatus Initialize() noexcept;
    void Dispatch(uint64_t vector) noexcept;
    [[nodiscard]] InterruptRuntimeStatistics Statistics() const noexcept;
    [[nodiscard]] bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept;

  private:
    void HandleKeyboardInterrupt() noexcept;

    LegacyPic pic_{};
    ProgrammableIntervalTimer timer_{};
    Ps2Keyboard keyboard_{};
    AtaPioDevice ata_device_{};
    ScanCodeSet1Decoder scan_code_decoder_{};
    PitConfiguration pit_configuration_{};
    volatile uint64_t timer_tick_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t keyboard_interrupt_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t supported_keyboard_event_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
    volatile uint64_t spurious_interrupt_count_{OS_KERNEL_INTERRUPT_EMPTY_COUNTER};
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
    if (this->keyboard_.Initialize() != Ps2KeyboardStatus::Succeeded) {
        return InterruptRuntimeStatus::KeyboardInitializationFailed;
    }

    uint8_t boot_descriptor_sector[OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES];
    for (uint64_t byte_index = OS_KERNEL_INTERRUPT_EMPTY_COUNTER;
         byte_index < OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES; ++byte_index) {
        boot_descriptor_sector[byte_index] = OS_KERNEL_INTERRUPT_ZERO_BYTE;
    }
    if (this->ata_device_.ReadSector(
            OS_KERNEL_INTERRUPT_BOOT_DESCRIPTOR_LBA, boot_descriptor_sector,
            OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) != AtaPioStatus::Succeeded) {
        return InterruptRuntimeStatus::AtaReadFailed;
    }
    if (!Stage1BootDescriptorMagicMatches(boot_descriptor_sector,
                                          OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES)) {
        return InterruptRuntimeStatus::InvalidBootDescriptor;
    }

    if (this->pic_.EnableInterruptRequest(OS_KERNEL_INTERRUPT_TIMER_REQUEST) !=
            LegacyPicStatus::Succeeded ||
        this->pic_.EnableInterruptRequest(OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST) !=
            LegacyPicStatus::Succeeded) {
        return InterruptRuntimeStatus::PicConfigurationFailed;
    }
    this->initialized_ = true;
    return InterruptRuntimeStatus::Succeeded;
}

void InterruptRuntime::Dispatch(const uint64_t vector) noexcept {
    uint64_t interrupt_request = OS_KERNEL_INTERRUPT_EMPTY_COUNTER;
    if (!this->initialized_ || CalculateLegacyPicInterruptRequest(vector, interrupt_request) !=
                                   LegacyPicModelStatus::Succeeded) {
        HaltProcessor();
    }

    if (interrupt_request == OS_KERNEL_INTERRUPT_TIMER_REQUEST) {
        this->timer_tick_count_ = this->timer_tick_count_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    } else if (interrupt_request == OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST) {
        this->HandleKeyboardInterrupt();
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
    const InterruptRuntimeStatistics statistics{
        .timer_tick_count = timer_tick_count,
        .monotonic_milliseconds =
            CalculatePitElapsedMilliseconds(timer_tick_count, this->pit_configuration_.divisor),
        .keyboard_interrupt_count = this->keyboard_interrupt_count_,
        .supported_keyboard_event_count = this->supported_keyboard_event_count_,
        .spurious_interrupt_count = this->spurious_interrupt_count_,
        .pic_mask = this->pic_.Mask(),
        .pit_divisor = this->pit_configuration_.divisor,
        .pit_actual_frequency_hz = this->pit_configuration_.actual_frequency_hz,
    };
    RestoreInterrupts(interrupts_were_enabled);
    return statistics;
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

bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept {
    return kernel_interrupt_runtime.TryTakeKeyboardEvent(event);
}

extern "C" ExceptionFrame *OsKernelDispatchHardwareInterrupt(ExceptionFrame *frame) noexcept {
    if (frame == nullptr) {
        HaltProcessor();
    }
    kernel_interrupt_runtime.Dispatch(frame->vector);
    uint64_t interrupt_request = 0ULL;
    if (CalculateLegacyPicInterruptRequest(frame->vector, interrupt_request) !=
        LegacyPicModelStatus::Succeeded) {
        HaltProcessor();
    }
    if (interrupt_request == OS_KERNEL_INTERRUPT_TIMER_REQUEST) {
        return HandleProcessTimerInterrupt(*frame);
    }
    return frame;
}

}
