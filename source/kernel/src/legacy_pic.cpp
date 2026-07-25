#include "os/kernel/legacy_pic.hpp"

#include "os/kernel/device_model.hpp"
#include "os/kernel/port_io.hpp"

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_PIC_MASTER_COMMAND_PORT = 0x0020U;
constexpr uint16_t OS_KERNEL_PIC_MASTER_DATA_PORT = 0x0021U;
constexpr uint16_t OS_KERNEL_PIC_SLAVE_COMMAND_PORT = 0x00A0U;
constexpr uint16_t OS_KERNEL_PIC_SLAVE_DATA_PORT = 0x00A1U;
constexpr uint8_t OS_KERNEL_PIC_INITIAL_MASK = 0xFFU;
constexpr uint8_t OS_KERNEL_PIC_ICW1_INITIALIZE_WITH_ICW4 = 0x11U;
constexpr uint8_t OS_KERNEL_PIC_MASTER_ICW3_SLAVE_ON_IRQ2 = 0x04U;
constexpr uint8_t OS_KERNEL_PIC_SLAVE_ICW3_CASCADE_ID = 0x02U;
constexpr uint8_t OS_KERNEL_PIC_ICW4_8086_MODE = 0x01U;
constexpr uint8_t OS_KERNEL_PIC_OCW2_NON_SPECIFIC_EOI = 0x20U;
constexpr uint8_t OS_KERNEL_PIC_OCW3_READ_ISR = 0x0BU;
constexpr uint64_t OS_KERNEL_PIC_MASTER_INTERRUPT_REQUEST_COUNT = 8ULL;
constexpr uint64_t OS_KERNEL_PIC_CASCADE_INTERRUPT_REQUEST = 2ULL;
constexpr uint64_t OS_KERNEL_PIC_MASTER_SPURIOUS_INTERRUPT_REQUEST = 7ULL;
constexpr uint64_t OS_KERNEL_PIC_SLAVE_SPURIOUS_INTERRUPT_REQUEST = 15ULL;
constexpr uint8_t OS_KERNEL_PIC_HIGHEST_IN_SERVICE_BIT = 0x80U;
constexpr uint8_t OS_KERNEL_PIC_SINGLE_MASK_BIT = 0x01U;
constexpr uint64_t OS_KERNEL_PIC_SLAVE_MASK_SHIFT_BITS = 8ULL;

}

LegacyPic::LegacyPic() noexcept
    : masterMask_{OS_KERNEL_PIC_INITIAL_MASK}, slaveMask_{OS_KERNEL_PIC_INITIAL_MASK} {}

void LegacyPic::initialize() noexcept {
    writePort8(OS_KERNEL_PIC_MASTER_COMMAND_PORT, OS_KERNEL_PIC_ICW1_INITIALIZE_WITH_ICW4);
    waitForPortIo();
    writePort8(OS_KERNEL_PIC_SLAVE_COMMAND_PORT, OS_KERNEL_PIC_ICW1_INITIALIZE_WITH_ICW4);
    waitForPortIo();
    writePort8(OS_KERNEL_PIC_MASTER_DATA_PORT,
               static_cast<uint8_t>(OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE));
    waitForPortIo();
    writePort8(OS_KERNEL_PIC_SLAVE_DATA_PORT,
               static_cast<uint8_t>(OS_KERNEL_DEVICE_PIC_SLAVE_VECTOR_BASE));
    waitForPortIo();
    writePort8(OS_KERNEL_PIC_MASTER_DATA_PORT, OS_KERNEL_PIC_MASTER_ICW3_SLAVE_ON_IRQ2);
    waitForPortIo();
    writePort8(OS_KERNEL_PIC_SLAVE_DATA_PORT, OS_KERNEL_PIC_SLAVE_ICW3_CASCADE_ID);
    waitForPortIo();
    writePort8(OS_KERNEL_PIC_MASTER_DATA_PORT, OS_KERNEL_PIC_ICW4_8086_MODE);
    waitForPortIo();
    writePort8(OS_KERNEL_PIC_SLAVE_DATA_PORT, OS_KERNEL_PIC_ICW4_8086_MODE);
    waitForPortIo();

    this->masterMask_ = OS_KERNEL_PIC_INITIAL_MASK;
    this->slaveMask_ = OS_KERNEL_PIC_INITIAL_MASK;
    this->writeMasks();
}

LegacyPicStatus LegacyPic::enableInterruptRequest(const uint64_t interruptRequest) noexcept {
    if (interruptRequest >= OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT) {
        return LegacyPicStatus::InvalidInterruptRequest;
    }

    if (interruptRequest < OS_KERNEL_PIC_MASTER_INTERRUPT_REQUEST_COUNT) {
        this->masterMask_ = static_cast<uint8_t>(
            this->masterMask_ & static_cast<uint8_t>(~(static_cast<uint8_t>(
                                    OS_KERNEL_PIC_SINGLE_MASK_BIT << interruptRequest))));
    } else {
        const uint64_t slaveInterruptRequest =
            interruptRequest - OS_KERNEL_PIC_MASTER_INTERRUPT_REQUEST_COUNT;
        this->slaveMask_ = static_cast<uint8_t>(
            this->slaveMask_ & static_cast<uint8_t>(~(static_cast<uint8_t>(
                                   OS_KERNEL_PIC_SINGLE_MASK_BIT << slaveInterruptRequest))));
        this->masterMask_ = static_cast<uint8_t>(
            this->masterMask_ &
            static_cast<uint8_t>(~(static_cast<uint8_t>(
                OS_KERNEL_PIC_SINGLE_MASK_BIT << OS_KERNEL_PIC_CASCADE_INTERRUPT_REQUEST))));
    }
    this->writeMasks();
    return LegacyPicStatus::Succeeded;
}

LegacyPicStatus LegacyPic::acknowledge(const uint64_t interruptRequest) noexcept {
    if (interruptRequest >= OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT) {
        return LegacyPicStatus::InvalidInterruptRequest;
    }

    if (interruptRequest == OS_KERNEL_PIC_MASTER_SPURIOUS_INTERRUPT_REQUEST &&
        (this->readInServiceRegister(false) & OS_KERNEL_PIC_HIGHEST_IN_SERVICE_BIT) == 0U) {
        // 虚假 IRQ7 未进入主片 ISR，发送 EOI 会错误结束另一条在服务请求。
        return LegacyPicStatus::SpuriousInterrupt;
    }
    if (interruptRequest == OS_KERNEL_PIC_SLAVE_SPURIOUS_INTERRUPT_REQUEST &&
        (this->readInServiceRegister(true) & OS_KERNEL_PIC_HIGHEST_IN_SERVICE_BIT) == 0U) {
        // 虚假 IRQ15 不确认从片，但主片已经接受级联 IRQ2，仍需向主片 EOI。
        writePort8(OS_KERNEL_PIC_MASTER_COMMAND_PORT, OS_KERNEL_PIC_OCW2_NON_SPECIFIC_EOI);
        return LegacyPicStatus::SpuriousInterrupt;
    }

    if (interruptRequest >= OS_KERNEL_PIC_MASTER_INTERRUPT_REQUEST_COUNT) {
        // 真实从片 IRQ 必须先释放从片优先级，再释放主片级联优先级。
        writePort8(OS_KERNEL_PIC_SLAVE_COMMAND_PORT, OS_KERNEL_PIC_OCW2_NON_SPECIFIC_EOI);
    }
    writePort8(OS_KERNEL_PIC_MASTER_COMMAND_PORT, OS_KERNEL_PIC_OCW2_NON_SPECIFIC_EOI);
    return LegacyPicStatus::Succeeded;
}

uint16_t LegacyPic::mask() const noexcept {
    return static_cast<uint16_t>(this->masterMask_) |
           static_cast<uint16_t>(static_cast<uint16_t>(this->slaveMask_)
                                 << OS_KERNEL_PIC_SLAVE_MASK_SHIFT_BITS);
}

uint8_t LegacyPic::readInServiceRegister(const bool slave) const noexcept {
    const uint16_t commandPort =
        slave ? OS_KERNEL_PIC_SLAVE_COMMAND_PORT : OS_KERNEL_PIC_MASTER_COMMAND_PORT;
    writePort8(commandPort, OS_KERNEL_PIC_OCW3_READ_ISR);
    return readPort8(commandPort);
}

void LegacyPic::writeMasks() const noexcept {
    writePort8(OS_KERNEL_PIC_MASTER_DATA_PORT, this->masterMask_);
    writePort8(OS_KERNEL_PIC_SLAVE_DATA_PORT, this->slaveMask_);
}

}
