#include "os/kernel/interrupt_runtime.hpp"

#include "os/kernel/ata_pio.hpp"
#include "os/kernel/legacy_pic.hpp"
#include "os/kernel/process_runtime.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/programmable_interval_timer.hpp"
#include "os/kernel/ps2_keyboard.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_INTERRUPT_BOOT_DESCRIPTOR_LBA = 0ULL;
constexpr uint64_t OS_KERNEL_INTERRUPT_COUNTER_INCREMENT = 1ULL;
constexpr uint8_t OS_KERNEL_INTERRUPT_ZERO_BYTE = 0U;

class InterruptRuntime final {
  public:
    InterruptRuntime() noexcept;

    [[nodiscard]] InterruptRuntimeStatus Initialize() noexcept;
    void Dispatch(uint64_t vector) noexcept;
    [[nodiscard]] InterruptRuntimeStatistics Statistics() const noexcept;
    [[nodiscard]] bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept;

  private:
    void HandleKeyboardInterrupt() noexcept;

    LegacyPic pic_;
    ProgrammableIntervalTimer timer_;
    Ps2Keyboard keyboard_;
    AtaPioDevice ataDevice_;
    ScanCodeSet1Decoder scanCodeDecoder_;
    PitConfiguration pitConfiguration_;
    volatile uint64_t timerTickCount_;
    volatile uint64_t keyboardInterruptCount_;
    volatile uint64_t supportedKeyboardEventCount_;
    volatile uint64_t spuriousInterruptCount_;
    KeyboardEvent pendingKeyboardEvent_;
    volatile bool keyboardEventPending_;
    bool initialized_;
};

InterruptRuntime kernelInterruptRuntime;

}

InterruptRuntime::InterruptRuntime() noexcept
    : pic_{}, timer_{}, keyboard_{}, ataDevice_{}, scanCodeDecoder_{}, pitConfiguration_{},
      timerTickCount_{0ULL}, keyboardInterruptCount_{0ULL}, supportedKeyboardEventCount_{0ULL},
      spuriousInterruptCount_{0ULL}, pendingKeyboardEvent_{}, keyboardEventPending_{false},
      initialized_{false} {}

InterruptRuntimeStatus InterruptRuntime::Initialize() noexcept {
    // 固件没有替内核建立 APIC 虚拟线模式；先把 LAPIC LINT0 配置为 ExtINT，
    // 再初始化 PIC，保证后续 IRQ 边沿沿明确的跨控制器路径交付。
    if (!ConfigureLegacyInterruptRouting()) {
        return InterruptRuntimeStatus::LegacyInterruptRoutingFailed;
    }
    this->pic_.Initialize();

    if (this->timer_.Initialize(OS_KERNEL_INTERRUPT_TARGET_TIMER_FREQUENCY_HZ,
                                this->pitConfiguration_) != PitConfigurationStatus::Succeeded) {
        return InterruptRuntimeStatus::InvalidPitConfiguration;
    }
    if (this->keyboard_.Initialize() != Ps2KeyboardStatus::Succeeded) {
        return InterruptRuntimeStatus::KeyboardInitializationFailed;
    }

    uint8_t bootDescriptorSector[OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES];
    for (uint64_t byteIndex = 0ULL; byteIndex < OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES;
         ++byteIndex) {
        bootDescriptorSector[byteIndex] = OS_KERNEL_INTERRUPT_ZERO_BYTE;
    }
    if (this->ataDevice_.ReadSector(OS_KERNEL_INTERRUPT_BOOT_DESCRIPTOR_LBA, bootDescriptorSector,
                                    OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) !=
        AtaPioStatus::Succeeded) {
        return InterruptRuntimeStatus::AtaReadFailed;
    }
    if (!Stage1BootDescriptorMagicMatches(bootDescriptorSector,
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
    uint64_t interruptRequest = 0ULL;
    if (!this->initialized_ || CalculateLegacyPicInterruptRequest(vector, interruptRequest) !=
                                   LegacyPicModelStatus::Succeeded) {
        HaltProcessor();
    }

    if (interruptRequest == OS_KERNEL_INTERRUPT_TIMER_REQUEST) {
        this->timerTickCount_ = this->timerTickCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    } else if (interruptRequest == OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST) {
        this->HandleKeyboardInterrupt();
    }

    const LegacyPicStatus acknowledgeStatus = this->pic_.Acknowledge(interruptRequest);
    if (acknowledgeStatus == LegacyPicStatus::SpuriousInterrupt) {
        this->spuriousInterruptCount_ =
            this->spuriousInterruptCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    } else if (acknowledgeStatus != LegacyPicStatus::Succeeded) {
        HaltProcessor();
    }
}

InterruptRuntimeStatistics InterruptRuntime::Statistics() const noexcept {
    // 当前单核由 IRQ 修改计数；保存并关闭原 IF 后复制，避免交付跨中断的快照。
    const bool interruptsWereEnabled = DisableInterrupts();
    const uint64_t timerTickCount = this->timerTickCount_;
    const InterruptRuntimeStatistics statistics{
        .timerTickCount = timerTickCount,
        .monotonicMilliseconds =
            CalculatePitElapsedMilliseconds(timerTickCount, this->pitConfiguration_.divisor),
        .keyboardInterruptCount = this->keyboardInterruptCount_,
        .supportedKeyboardEventCount = this->supportedKeyboardEventCount_,
        .spuriousInterruptCount = this->spuriousInterruptCount_,
        .picMask = this->pic_.Mask(),
        .pitDivisor = this->pitConfiguration_.divisor,
        .pitActualFrequencyHz = this->pitConfiguration_.actualFrequencyHz,
    };
    RestoreInterrupts(interruptsWereEnabled);
    return statistics;
}

bool InterruptRuntime::TryTakeKeyboardEvent(KeyboardEvent &event) noexcept {
    const bool interruptsWereEnabled = DisableInterrupts();
    const bool eventAvailable = this->keyboardEventPending_;
    if (eventAvailable) {
        event = this->pendingKeyboardEvent_;
        this->keyboardEventPending_ = false;
    }
    RestoreInterrupts(interruptsWereEnabled);
    return eventAvailable;
}

void InterruptRuntime::HandleKeyboardInterrupt() noexcept {
    this->keyboardInterruptCount_ =
        this->keyboardInterruptCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    uint8_t scanCode = 0U;
    if (this->keyboard_.TryReadScanCode(scanCode) != Ps2KeyboardStatus::Succeeded) {
        return;
    }

    KeyboardEvent decodedEvent{};
    if (this->scanCodeDecoder_.Decode(scanCode, decodedEvent) != KeyboardDecodeStatus::EventReady) {
        return;
    }
    this->supportedKeyboardEventCount_ =
        this->supportedKeyboardEventCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    if (!this->keyboardEventPending_) {
        this->pendingKeyboardEvent_ = decodedEvent;
        this->keyboardEventPending_ = true;
    }
}

InterruptRuntimeStatus InitializeInterruptRuntime() noexcept {
    return kernelInterruptRuntime.Initialize();
}

InterruptRuntimeStatistics GetInterruptRuntimeStatistics() noexcept {
    return kernelInterruptRuntime.Statistics();
}

bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept {
    return kernelInterruptRuntime.TryTakeKeyboardEvent(event);
}

extern "C" ExceptionFrame *osKernelDispatchHardwareInterrupt(ExceptionFrame *frame) noexcept {
    if (frame == nullptr) {
        HaltProcessor();
    }
    kernelInterruptRuntime.Dispatch(frame->vector);
    uint64_t interruptRequest = 0ULL;
    if (CalculateLegacyPicInterruptRequest(frame->vector, interruptRequest) !=
        LegacyPicModelStatus::Succeeded) {
        HaltProcessor();
    }
    if (interruptRequest == OS_KERNEL_INTERRUPT_TIMER_REQUEST) {
        return HandleProcessTimerInterrupt(*frame);
    }
    return frame;
}

}
