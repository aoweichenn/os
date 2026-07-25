#include "os/kernel/interrupt_runtime.hpp"

#include "os/kernel/ata_pio.hpp"
#include "os/kernel/legacy_pic.hpp"
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

    [[nodiscard]] InterruptRuntimeStatus initialize() noexcept;
    void dispatch(uint64_t vector) noexcept;
    [[nodiscard]] InterruptRuntimeStatistics statistics() const noexcept;
    [[nodiscard]] bool tryTakeKeyboardEvent(KeyboardEvent &event) noexcept;

  private:
    void handleKeyboardInterrupt() noexcept;

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

InterruptRuntimeStatus InterruptRuntime::initialize() noexcept {
    // 固件没有替内核建立 APIC 虚拟线模式；关闭本地 APIC 后，8259A 的 INTR
    // 才会沿处理器的传统中断输入交付。后续 APIC 阶段会显式替换这条路径。
    if (!configureLegacyInterruptRouting()) {
        return InterruptRuntimeStatus::LegacyInterruptRoutingFailed;
    }
    this->pic_.initialize();

    if (this->timer_.initialize(OS_KERNEL_INTERRUPT_TARGET_TIMER_FREQUENCY_HZ,
                                this->pitConfiguration_) != PitConfigurationStatus::Succeeded) {
        return InterruptRuntimeStatus::InvalidPitConfiguration;
    }
    if (this->keyboard_.initialize() != Ps2KeyboardStatus::Succeeded) {
        return InterruptRuntimeStatus::KeyboardInitializationFailed;
    }

    uint8_t bootDescriptorSector[OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES];
    for (uint64_t byteIndex = 0ULL; byteIndex < OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES;
         ++byteIndex) {
        bootDescriptorSector[byteIndex] = OS_KERNEL_INTERRUPT_ZERO_BYTE;
    }
    if (this->ataDevice_.readSector(OS_KERNEL_INTERRUPT_BOOT_DESCRIPTOR_LBA, bootDescriptorSector,
                                    OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) !=
        AtaPioStatus::Succeeded) {
        return InterruptRuntimeStatus::AtaReadFailed;
    }
    if (!stage1BootDescriptorMagicMatches(bootDescriptorSector,
                                          OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES)) {
        return InterruptRuntimeStatus::InvalidBootDescriptor;
    }

    if (this->pic_.enableInterruptRequest(OS_KERNEL_INTERRUPT_TIMER_REQUEST) !=
            LegacyPicStatus::Succeeded ||
        this->pic_.enableInterruptRequest(OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST) !=
            LegacyPicStatus::Succeeded) {
        return InterruptRuntimeStatus::PicConfigurationFailed;
    }
    this->initialized_ = true;
    return InterruptRuntimeStatus::Succeeded;
}

void InterruptRuntime::dispatch(const uint64_t vector) noexcept {
    uint64_t interruptRequest = 0ULL;
    if (!this->initialized_ || calculateLegacyPicInterruptRequest(vector, interruptRequest) !=
                                   LegacyPicModelStatus::Succeeded) {
        haltProcessor();
    }

    if (interruptRequest == OS_KERNEL_INTERRUPT_TIMER_REQUEST) {
        this->timerTickCount_ = this->timerTickCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    } else if (interruptRequest == OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST) {
        this->handleKeyboardInterrupt();
    }

    const LegacyPicStatus acknowledgeStatus = this->pic_.acknowledge(interruptRequest);
    if (acknowledgeStatus == LegacyPicStatus::SpuriousInterrupt) {
        this->spuriousInterruptCount_ =
            this->spuriousInterruptCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    } else if (acknowledgeStatus != LegacyPicStatus::Succeeded) {
        haltProcessor();
    }
}

InterruptRuntimeStatistics InterruptRuntime::statistics() const noexcept {
    // 当前单核由 IRQ 修改计数；保存并关闭原 IF 后复制，避免交付跨中断的快照。
    const bool interruptsWereEnabled = disableInterrupts();
    const uint64_t timerTickCount = this->timerTickCount_;
    const InterruptRuntimeStatistics statistics{
        .timerTickCount = timerTickCount,
        .monotonicMilliseconds =
            calculatePitElapsedMilliseconds(timerTickCount, this->pitConfiguration_.divisor),
        .keyboardInterruptCount = this->keyboardInterruptCount_,
        .supportedKeyboardEventCount = this->supportedKeyboardEventCount_,
        .spuriousInterruptCount = this->spuriousInterruptCount_,
        .picMask = this->pic_.mask(),
        .pitDivisor = this->pitConfiguration_.divisor,
        .pitActualFrequencyHz = this->pitConfiguration_.actualFrequencyHz,
    };
    restoreInterrupts(interruptsWereEnabled);
    return statistics;
}

bool InterruptRuntime::tryTakeKeyboardEvent(KeyboardEvent &event) noexcept {
    const bool interruptsWereEnabled = disableInterrupts();
    const bool eventAvailable = this->keyboardEventPending_;
    if (eventAvailable) {
        event = this->pendingKeyboardEvent_;
        this->keyboardEventPending_ = false;
    }
    restoreInterrupts(interruptsWereEnabled);
    return eventAvailable;
}

void InterruptRuntime::handleKeyboardInterrupt() noexcept {
    this->keyboardInterruptCount_ =
        this->keyboardInterruptCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    uint8_t scanCode = 0U;
    if (this->keyboard_.tryReadScanCode(scanCode) != Ps2KeyboardStatus::Succeeded) {
        return;
    }

    KeyboardEvent decodedEvent{};
    if (this->scanCodeDecoder_.decode(scanCode, decodedEvent) != KeyboardDecodeStatus::EventReady) {
        return;
    }
    this->supportedKeyboardEventCount_ =
        this->supportedKeyboardEventCount_ + OS_KERNEL_INTERRUPT_COUNTER_INCREMENT;
    if (!this->keyboardEventPending_) {
        this->pendingKeyboardEvent_ = decodedEvent;
        this->keyboardEventPending_ = true;
    }
}

InterruptRuntimeStatus initializeInterruptRuntime() noexcept {
    return kernelInterruptRuntime.initialize();
}

InterruptRuntimeStatistics interruptRuntimeStatistics() noexcept {
    return kernelInterruptRuntime.statistics();
}

bool tryTakeKeyboardEvent(KeyboardEvent &event) noexcept {
    return kernelInterruptRuntime.tryTakeKeyboardEvent(event);
}

extern "C" void osKernelDispatchHardwareInterrupt(ExceptionFrame *frame) noexcept {
    if (frame == nullptr) {
        haltProcessor();
    }
    kernelInterruptRuntime.dispatch(frame->vector);
}

}
