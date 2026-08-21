#include <os/kernel/device/nvme_model.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_NVME_MODEL_SUITE_NAME = "kernel/nvme_model/unit";
constexpr std::string_view OS_TEST_NVME_MODEL_CAPABILITIES =
    "NVMe CAP 必须解析队列、超时、doorbell、页大小和 NVM command set";
constexpr std::string_view OS_TEST_NVME_MODEL_CONFIGURATION =
    "NVMe CC 与 AQA 必须使用 4 KiB、64B SQE、16B CQE 和零基队列深度";
constexpr std::string_view OS_TEST_NVME_MODEL_IDENTIFY_COMMAND =
    "Identify Controller/Namespace 命令必须编码 CID、NSID、PRP1 与 CNS";
constexpr std::string_view OS_TEST_NVME_MODEL_COMPLETION =
    "NVMe CQE 必须按 phase、CID、SQID 和 status 解析唯一完成";
constexpr std::string_view OS_TEST_NVME_MODEL_IDENTIFY_DATA =
    "Identify 数据必须解析 controller 限制与 namespace LBA 格式";
constexpr std::string_view OS_TEST_NVME_MODEL_QUEUE_WRAP =
    "NVMe admin SQ/CQ 游标必须在深度边界回绕并翻转 completion phase";
constexpr std::string_view OS_TEST_NVME_MODEL_IO_QUEUE_COMMANDS =
    "NVMe Set Features 与 Create I/O CQ/SQ 必须编码零基深度、QID、CQID 和 PC";
constexpr std::string_view OS_TEST_NVME_MODEL_IO_COMMANDS =
    "NVMe Read/Write/Flush 必须编码 NSID、SLBA、零基 NLB 与单页 PRP1";
constexpr std::string_view OS_TEST_NVME_MODEL_BLOCK_GEOMETRY =
    "NVMe BlockDevice 几何必须受 MDTS、16 页槽位和 namespace 容量共同限制";
constexpr std::string_view OS_TEST_NVME_MODEL_PRP_LIST =
    "NVMe PRP 必须区分单页、直接 PRP2 与三页以上的 packed PRP list";

constexpr uint64_t OS_TEST_NVME_MODEL_CAP_MQES = 1023ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CAP_TIMEOUT_UNITS = 15ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CAP_TIMEOUT_SHIFT_BITS = 24ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CAP_NVM_COMMAND_SET_BIT = 1ULL << 37ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CAPABILITIES_VALUE =
    OS_TEST_NVME_MODEL_CAP_MQES |
    (OS_TEST_NVME_MODEL_CAP_TIMEOUT_UNITS << OS_TEST_NVME_MODEL_CAP_TIMEOUT_SHIFT_BITS) |
    OS_TEST_NVME_MODEL_CAP_NVM_COMMAND_SET_BIT;
constexpr uint64_t OS_TEST_NVME_MODEL_EXPECTED_TIMEOUT_MILLISECONDS = 7500ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_EXPECTED_DOORBELL_STRIDE_BYTES = 4ULL;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_CONFIGURATION = 0x00460001U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_AQA = 0x00010001U;
constexpr uint16_t OS_TEST_NVME_MODEL_CONTROLLER_COMMAND_IDENTIFIER = 1U;
constexpr uint16_t OS_TEST_NVME_MODEL_NAMESPACE_COMMAND_IDENTIFIER = 2U;
constexpr uint32_t OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER = 1U;
constexpr uint64_t OS_TEST_NVME_MODEL_DATA_PHYSICAL_ADDRESS = 0x0000000000200000ULL;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_CONTROLLER_DWORD_ZERO = 0x00010006U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_NAMESPACE_DWORD_ZERO = 0x00020006U;
constexpr uint16_t OS_TEST_NVME_MODEL_VENDOR_IDENTIFIER = 0x1B36U;
constexpr uint8_t OS_TEST_NVME_MODEL_MDTS_EXPONENT = 7U;
constexpr uint64_t OS_TEST_NVME_MODEL_NAMESPACE_BLOCK_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_NAMESPACE_CAPACITY_OFFSET_BYTES = 8ULL;
constexpr uint8_t OS_TEST_NVME_MODEL_SUBMISSION_ENTRY_SIZES = 0x66U;
constexpr uint8_t OS_TEST_NVME_MODEL_COMPLETION_ENTRY_SIZES = 0x44U;
constexpr uint64_t OS_TEST_NVME_MODEL_EXPECTED_MAXIMUM_TRANSFER_SIZE_BYTES = 524288ULL;
constexpr uint8_t OS_TEST_NVME_MODEL_LBA_DATA_SIZE_EXPONENT = 9U;
constexpr uint64_t OS_TEST_NVME_MODEL_EXPECTED_LBA_SIZE_BYTES = 512ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CONTROLLER_MDTS_OFFSET_BYTES = 77ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CONTROLLER_SQES_OFFSET_BYTES = 512ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CONTROLLER_CQES_OFFSET_BYTES = 513ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_CONTROLLER_NN_OFFSET_BYTES = 516ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_NAMESPACE_NLBAF_OFFSET_BYTES = 25ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_NAMESPACE_FLBAS_OFFSET_BYTES = 26ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_NAMESPACE_LBAF0_OFFSET_BYTES = 128ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_LBA_DATA_SIZE_SHIFT_BITS = 16ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_DWORD_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_QWORD_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_BYTE_BIT_COUNT = 8ULL;
constexpr uint16_t OS_TEST_NVME_MODEL_SET_QUEUE_COUNT_COMMAND_IDENTIFIER = 3U;
constexpr uint16_t OS_TEST_NVME_MODEL_CREATE_COMPLETION_COMMAND_IDENTIFIER = 4U;
constexpr uint16_t OS_TEST_NVME_MODEL_CREATE_SUBMISSION_COMMAND_IDENTIFIER = 5U;
constexpr uint16_t OS_TEST_NVME_MODEL_IO_QUEUE_IDENTIFIER = 1U;
constexpr uint64_t OS_TEST_NVME_MODEL_IO_QUEUE_DEPTH = 64ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_IO_QUEUE_PHYSICAL_ADDRESS = 0x0000000000300000ULL;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_SET_QUEUE_DWORD_ZERO = 0x00030009U;
constexpr uint32_t OS_TEST_NVME_MODEL_NUMBER_OF_QUEUES_FEATURE_IDENTIFIER = 0x07U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_CREATE_CQ_DWORD_ZERO = 0x00040005U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_CREATE_SQ_DWORD_ZERO = 0x00050001U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_CREATE_QUEUE_DWORD_TEN = 0x003F0001U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_CREATE_CQ_DWORD_ELEVEN = 0x00000003U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_CREATE_SQ_DWORD_ELEVEN = 0x00010001U;
constexpr uint64_t OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_ADDRESS = 0x0000000100000002ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_COUNT = 8ULL;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_WRITE_DWORD_ZERO = 0x00010001U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_READ_DWORD_ZERO = 0x00020002U;
constexpr uint32_t OS_TEST_NVME_MODEL_EXPECTED_FLUSH_DWORD_ZERO = 0x00030000U;
constexpr uint32_t OS_TEST_NVME_MODEL_ALLOCATED_QUEUE_COUNTS = 0x00030005U;
constexpr uint64_t OS_TEST_NVME_MODEL_EXPECTED_SUBMISSION_QUEUE_COUNT = 6ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_EXPECTED_COMPLETION_QUEUE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_EXPECTED_MAXIMUM_TRANSFER_BLOCK_COUNT = 128ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_INVALID_IO_SUBMISSION_QUEUE_DEPTH = 65ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_PRP_DATA_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_PRP_LIST_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_PRP_LIST_PHYSICAL_ADDRESS = 0x0000000000500000ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_PRP_PAGE_ONE = 0x0000000000600000ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_PRP_PAGE_TWO = 0x0000000000601000ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_PRP_PAGE_THREE = 0x0000000000602000ULL;
constexpr uint64_t OS_TEST_NVME_MODEL_PRP_PAGE_FOUR = 0x0000000000603000ULL;

void StoreLittleEndian16(uint8_t *const destination, const uint16_t value) noexcept {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8U);
}

void StoreLittleEndian32(uint8_t *const destination, const uint32_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_NVME_MODEL_DWORD_SIZE_BYTES;
         ++byte_index) {
        destination[byte_index] = static_cast<uint8_t>(
            value >> (byte_index * OS_TEST_NVME_MODEL_BYTE_BIT_COUNT));
    }
}

void StoreLittleEndian64(uint8_t *const destination, const uint64_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_NVME_MODEL_QWORD_SIZE_BYTES;
         ++byte_index) {
        destination[byte_index] = static_cast<uint8_t>(
            value >> (byte_index * OS_TEST_NVME_MODEL_BYTE_BIT_COUNT));
    }
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_NVME_MODEL_SUITE_NAME};
    os::kernel::NvmeControllerCapabilities capabilities{};
    test_context.Expect(
        os::kernel::DecodeNvmeControllerCapabilities(OS_TEST_NVME_MODEL_CAPABILITIES_VALUE,
                                                     capabilities) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            capabilities.maximum_queue_entry_count == OS_TEST_NVME_MODEL_CAP_MQES + 1ULL &&
            capabilities.ready_timeout_milliseconds ==
                OS_TEST_NVME_MODEL_EXPECTED_TIMEOUT_MILLISECONDS &&
            capabilities.doorbell_stride_bytes ==
                OS_TEST_NVME_MODEL_EXPECTED_DOORBELL_STRIDE_BYTES &&
            capabilities.minimum_memory_page_size_bytes ==
                os::kernel::OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES &&
            capabilities.maximum_memory_page_size_bytes ==
                os::kernel::OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES &&
            capabilities.nvm_command_set_supported,
        OS_TEST_NVME_MODEL_CAPABILITIES);

    uint32_t controller_configuration = 0U;
    uint32_t admin_queue_attributes = 0U;
    test_context.Expect(
        os::kernel::BuildNvmeControllerConfiguration(capabilities,
                                                     controller_configuration) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            controller_configuration == OS_TEST_NVME_MODEL_EXPECTED_CONFIGURATION &&
            os::kernel::BuildNvmeAdminQueueAttributes(
                os::kernel::OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH, admin_queue_attributes) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            admin_queue_attributes == OS_TEST_NVME_MODEL_EXPECTED_AQA &&
            os::kernel::BuildNvmeAdminQueueAttributes(1ULL, admin_queue_attributes) ==
                os::kernel::NvmeModelStatus::InvalidQueueDepth,
        OS_TEST_NVME_MODEL_CONFIGURATION);

    os::kernel::NvmeSubmissionEntry command{};
    const bool controller_command_valid =
        os::kernel::BuildNvmeIdentifyCommand(
            OS_TEST_NVME_MODEL_CONTROLLER_COMMAND_IDENTIFIER, 0U,
            os::kernel::OS_KERNEL_NVME_IDENTIFY_CONTROLLER_CNS,
            OS_TEST_NVME_MODEL_DATA_PHYSICAL_ADDRESS, command) ==
            os::kernel::NvmeModelStatus::Succeeded &&
        command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_CONTROLLER_DWORD_ZERO &&
        command.dwords[1] == 0U &&
        command.dwords[6] == static_cast<uint32_t>(OS_TEST_NVME_MODEL_DATA_PHYSICAL_ADDRESS) &&
        command.dwords[10] == os::kernel::OS_KERNEL_NVME_IDENTIFY_CONTROLLER_CNS;
    const bool namespace_command_valid =
        os::kernel::BuildNvmeIdentifyCommand(
            OS_TEST_NVME_MODEL_NAMESPACE_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER,
            os::kernel::OS_KERNEL_NVME_IDENTIFY_NAMESPACE_CNS,
            OS_TEST_NVME_MODEL_DATA_PHYSICAL_ADDRESS, command) ==
            os::kernel::NvmeModelStatus::Succeeded &&
        command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_NAMESPACE_DWORD_ZERO &&
        command.dwords[1] == OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER && command.dwords[10] == 0U;
    test_context.Expect(controller_command_valid && namespace_command_valid,
                        OS_TEST_NVME_MODEL_IDENTIFY_COMMAND);

    os::kernel::NvmeSubmissionEntry queue_command{};
    const bool queue_count_command_valid =
        os::kernel::BuildNvmeSetQueueCountCommand(
            OS_TEST_NVME_MODEL_SET_QUEUE_COUNT_COMMAND_IDENTIFIER, 1ULL, 1ULL,
            queue_command) == os::kernel::NvmeModelStatus::Succeeded &&
        queue_command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_SET_QUEUE_DWORD_ZERO &&
        queue_command.dwords[10] == OS_TEST_NVME_MODEL_NUMBER_OF_QUEUES_FEATURE_IDENTIFIER &&
        queue_command.dwords[11] == 0U;
    const bool completion_queue_command_valid =
        os::kernel::BuildNvmeCreateIoCompletionQueueCommand(
            OS_TEST_NVME_MODEL_CREATE_COMPLETION_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_IO_QUEUE_IDENTIFIER, OS_TEST_NVME_MODEL_IO_QUEUE_DEPTH,
            OS_TEST_NVME_MODEL_IO_QUEUE_PHYSICAL_ADDRESS, true, 0U,
            queue_command) == os::kernel::NvmeModelStatus::Succeeded &&
        queue_command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_CREATE_CQ_DWORD_ZERO &&
        queue_command.dwords[10] == OS_TEST_NVME_MODEL_EXPECTED_CREATE_QUEUE_DWORD_TEN &&
        queue_command.dwords[11] == OS_TEST_NVME_MODEL_EXPECTED_CREATE_CQ_DWORD_ELEVEN;
    const bool submission_queue_command_valid =
        os::kernel::BuildNvmeCreateIoSubmissionQueueCommand(
            OS_TEST_NVME_MODEL_CREATE_SUBMISSION_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_IO_QUEUE_IDENTIFIER, OS_TEST_NVME_MODEL_IO_QUEUE_IDENTIFIER,
            OS_TEST_NVME_MODEL_IO_QUEUE_DEPTH, OS_TEST_NVME_MODEL_IO_QUEUE_PHYSICAL_ADDRESS,
            queue_command) == os::kernel::NvmeModelStatus::Succeeded &&
        queue_command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_CREATE_SQ_DWORD_ZERO &&
        queue_command.dwords[10] == OS_TEST_NVME_MODEL_EXPECTED_CREATE_QUEUE_DWORD_TEN &&
        queue_command.dwords[11] == OS_TEST_NVME_MODEL_EXPECTED_CREATE_SQ_DWORD_ELEVEN &&
        os::kernel::BuildNvmeSetQueueCountCommand(
            OS_TEST_NVME_MODEL_SET_QUEUE_COUNT_COMMAND_IDENTIFIER, 0ULL, 1ULL,
            queue_command) == os::kernel::NvmeModelStatus::InvalidQueueCount &&
        os::kernel::BuildNvmeCreateIoSubmissionQueueCommand(
            OS_TEST_NVME_MODEL_CREATE_SUBMISSION_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_IO_QUEUE_IDENTIFIER, OS_TEST_NVME_MODEL_IO_QUEUE_IDENTIFIER,
            OS_TEST_NVME_MODEL_INVALID_IO_SUBMISSION_QUEUE_DEPTH,
            OS_TEST_NVME_MODEL_IO_QUEUE_PHYSICAL_ADDRESS,
            queue_command) == os::kernel::NvmeModelStatus::InvalidQueueDepth;
    test_context.Expect(queue_count_command_valid && completion_queue_command_valid &&
                            submission_queue_command_valid,
                        OS_TEST_NVME_MODEL_IO_QUEUE_COMMANDS);

    os::kernel::NvmeSubmissionEntry io_command{};
    const bool write_command_valid =
        os::kernel::BuildNvmeIoCommand(
            os::kernel::NvmeIoOperation::Write,
            OS_TEST_NVME_MODEL_CONTROLLER_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER,
            OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_ADDRESS,
            OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_COUNT,
            OS_TEST_NVME_MODEL_DATA_PHYSICAL_ADDRESS, 0ULL,
            io_command) == os::kernel::NvmeModelStatus::Succeeded &&
        io_command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_WRITE_DWORD_ZERO &&
        io_command.dwords[10] ==
            static_cast<uint32_t>(OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_ADDRESS) &&
        io_command.dwords[11] == 1U &&
        io_command.dwords[12] == OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_COUNT - 1ULL;
    const bool read_command_valid =
        os::kernel::BuildNvmeIoCommand(
            os::kernel::NvmeIoOperation::Read,
            OS_TEST_NVME_MODEL_NAMESPACE_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER,
            OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_ADDRESS,
            OS_TEST_NVME_MODEL_IO_LOGICAL_BLOCK_COUNT,
            OS_TEST_NVME_MODEL_DATA_PHYSICAL_ADDRESS, 0ULL,
            io_command) == os::kernel::NvmeModelStatus::Succeeded &&
        io_command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_READ_DWORD_ZERO;
    const bool flush_command_valid =
        os::kernel::BuildNvmeIoCommand(
            os::kernel::NvmeIoOperation::Flush,
            OS_TEST_NVME_MODEL_SET_QUEUE_COUNT_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER, 0ULL, 0ULL, 0ULL, 0ULL,
            io_command) == os::kernel::NvmeModelStatus::Succeeded &&
        io_command.dwords[0] == OS_TEST_NVME_MODEL_EXPECTED_FLUSH_DWORD_ZERO &&
        io_command.dwords[1] == OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER &&
        io_command.dwords[6] == 0U && io_command.dwords[12] == 0U &&
        os::kernel::BuildNvmeIoCommand(
            os::kernel::NvmeIoOperation::Flush,
            OS_TEST_NVME_MODEL_SET_QUEUE_COUNT_COMMAND_IDENTIFIER,
            OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER, 0ULL, 1ULL,
            OS_TEST_NVME_MODEL_DATA_PHYSICAL_ADDRESS, 0ULL,
            io_command) == os::kernel::NvmeModelStatus::InvalidTransfer;
    test_context.Expect(write_command_valid && read_command_valid && flush_command_valid,
                        OS_TEST_NVME_MODEL_IO_COMMANDS);

    const uint64_t data_page_physical_addresses[OS_TEST_NVME_MODEL_PRP_DATA_PAGE_COUNT] = {
        OS_TEST_NVME_MODEL_PRP_PAGE_ONE,
        OS_TEST_NVME_MODEL_PRP_PAGE_TWO,
        OS_TEST_NVME_MODEL_PRP_PAGE_THREE,
        OS_TEST_NVME_MODEL_PRP_PAGE_FOUR,
    };
    uint64_t prp_list_entries[OS_TEST_NVME_MODEL_PRP_LIST_CAPACITY]{};
    os::kernel::NvmePrpMapping prp_mapping{};
    const bool packed_prp_list_valid =
        os::kernel::BuildNvmePrpMapping(
            data_page_physical_addresses, OS_TEST_NVME_MODEL_PRP_DATA_PAGE_COUNT,
            OS_TEST_NVME_MODEL_PRP_LIST_PHYSICAL_ADDRESS, prp_list_entries,
            OS_TEST_NVME_MODEL_PRP_LIST_CAPACITY,
            prp_mapping) == os::kernel::NvmeModelStatus::Succeeded &&
        prp_mapping.first_data_pointer == OS_TEST_NVME_MODEL_PRP_PAGE_ONE &&
        prp_mapping.second_data_pointer == OS_TEST_NVME_MODEL_PRP_LIST_PHYSICAL_ADDRESS &&
        prp_mapping.list_entry_count == OS_TEST_NVME_MODEL_PRP_DATA_PAGE_COUNT - 1ULL &&
        prp_list_entries[0] == OS_TEST_NVME_MODEL_PRP_PAGE_TWO &&
        prp_list_entries[1] == OS_TEST_NVME_MODEL_PRP_PAGE_THREE &&
        prp_list_entries[2] == OS_TEST_NVME_MODEL_PRP_PAGE_FOUR;
    const bool direct_second_prp_valid =
        os::kernel::BuildNvmePrpMapping(
            data_page_physical_addresses, 2ULL, 0ULL, nullptr, 0ULL,
            prp_mapping) == os::kernel::NvmeModelStatus::Succeeded &&
        prp_mapping.first_data_pointer == OS_TEST_NVME_MODEL_PRP_PAGE_ONE &&
        prp_mapping.second_data_pointer == OS_TEST_NVME_MODEL_PRP_PAGE_TWO &&
        prp_mapping.list_entry_count == 0ULL;
    const bool short_prp_list_rejected =
        os::kernel::BuildNvmePrpMapping(
            data_page_physical_addresses, OS_TEST_NVME_MODEL_PRP_DATA_PAGE_COUNT,
            OS_TEST_NVME_MODEL_PRP_LIST_PHYSICAL_ADDRESS, prp_list_entries, 2ULL,
            prp_mapping) == os::kernel::NvmeModelStatus::InvalidPrpList;
    test_context.Expect(packed_prp_list_valid && direct_second_prp_valid &&
                            short_prp_list_rejected,
                        OS_TEST_NVME_MODEL_PRP_LIST);

    os::kernel::NvmeCompletionEntry completion{};
    completion.dwords[2] = 1U;
    completion.dwords[0] = OS_TEST_NVME_MODEL_ALLOCATED_QUEUE_COUNTS;
    completion.dwords[3] = (1U << 16U) | OS_TEST_NVME_MODEL_CONTROLLER_COMMAND_IDENTIFIER;
    os::kernel::NvmeCompletionResult completion_result{};
    const bool successful_completion =
        os::kernel::DecodeNvmeCompletion(
            completion, true, OS_TEST_NVME_MODEL_CONTROLLER_COMMAND_IDENTIFIER,
            completion_result) == os::kernel::NvmeModelStatus::Succeeded &&
        completion_result.command_identifier ==
            OS_TEST_NVME_MODEL_CONTROLLER_COMMAND_IDENTIFIER &&
        completion_result.submission_queue_identifier == 0U &&
        completion_result.submission_queue_head == 1U &&
        completion_result.command_specific == OS_TEST_NVME_MODEL_ALLOCATED_QUEUE_COUNTS;
    os::kernel::NvmeAllocatedQueueCounts allocated_queue_counts{};
    const bool allocated_queue_counts_valid =
        os::kernel::DecodeNvmeAllocatedQueueCounts(completion_result,
                                                   allocated_queue_counts) ==
            os::kernel::NvmeModelStatus::Succeeded &&
        allocated_queue_counts.submission_queue_count ==
            OS_TEST_NVME_MODEL_EXPECTED_SUBMISSION_QUEUE_COUNT &&
        allocated_queue_counts.completion_queue_count ==
            OS_TEST_NVME_MODEL_EXPECTED_COMPLETION_QUEUE_COUNT;
    os::kernel::NvmeCompletionResult invalid_queue_count_completion{};
    invalid_queue_count_completion.command_specific = UINT32_MAX;
    const bool invalid_allocated_queue_counts_rejected =
        os::kernel::DecodeNvmeAllocatedQueueCounts(
            invalid_queue_count_completion, allocated_queue_counts) ==
        os::kernel::NvmeModelStatus::InvalidQueueCount;
    completion.dwords[3] |= 1U << 17U;
    const bool failed_completion =
        os::kernel::DecodeNvmeCompletion(
            completion, true, OS_TEST_NVME_MODEL_CONTROLLER_COMMAND_IDENTIFIER,
            completion_result) == os::kernel::NvmeModelStatus::CompletionFailed &&
        completion_result.status_code == 1U;
    test_context.Expect(successful_completion && allocated_queue_counts_valid &&
                            invalid_allocated_queue_counts_rejected && failed_completion,
                        OS_TEST_NVME_MODEL_COMPLETION);

    uint8_t identify_data[os::kernel::OS_KERNEL_NVME_IDENTIFY_DATA_SIZE_BYTES]{};
    StoreLittleEndian16(identify_data, OS_TEST_NVME_MODEL_VENDOR_IDENTIFIER);
    identify_data[OS_TEST_NVME_MODEL_CONTROLLER_MDTS_OFFSET_BYTES] =
        OS_TEST_NVME_MODEL_MDTS_EXPONENT;
    identify_data[OS_TEST_NVME_MODEL_CONTROLLER_SQES_OFFSET_BYTES] =
        OS_TEST_NVME_MODEL_SUBMISSION_ENTRY_SIZES;
    identify_data[OS_TEST_NVME_MODEL_CONTROLLER_CQES_OFFSET_BYTES] =
        OS_TEST_NVME_MODEL_COMPLETION_ENTRY_SIZES;
    StoreLittleEndian32(identify_data + OS_TEST_NVME_MODEL_CONTROLLER_NN_OFFSET_BYTES,
                        OS_TEST_NVME_MODEL_NAMESPACE_IDENTIFIER);
    os::kernel::NvmeControllerIdentity controller_identity{};
    const bool controller_identity_valid =
        os::kernel::ParseNvmeControllerIdentity(
            identify_data, sizeof(identify_data), capabilities, controller_identity) ==
            os::kernel::NvmeModelStatus::Succeeded &&
        controller_identity.vendor_identifier == OS_TEST_NVME_MODEL_VENDOR_IDENTIFIER &&
        controller_identity.namespace_count == 1ULL &&
        controller_identity.maximum_transfer_size_bytes ==
            OS_TEST_NVME_MODEL_EXPECTED_MAXIMUM_TRANSFER_SIZE_BYTES;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(identify_data); ++byte_index) {
        identify_data[byte_index] = 0U;
    }
    StoreLittleEndian64(identify_data, OS_TEST_NVME_MODEL_NAMESPACE_BLOCK_COUNT);
    StoreLittleEndian64(identify_data + OS_TEST_NVME_MODEL_NAMESPACE_CAPACITY_OFFSET_BYTES,
                        OS_TEST_NVME_MODEL_NAMESPACE_BLOCK_COUNT);
    identify_data[OS_TEST_NVME_MODEL_NAMESPACE_NLBAF_OFFSET_BYTES] = 0U;
    identify_data[OS_TEST_NVME_MODEL_NAMESPACE_FLBAS_OFFSET_BYTES] = 0U;
    StoreLittleEndian32(
        identify_data + OS_TEST_NVME_MODEL_NAMESPACE_LBAF0_OFFSET_BYTES,
        static_cast<uint32_t>(OS_TEST_NVME_MODEL_LBA_DATA_SIZE_EXPONENT
                              << OS_TEST_NVME_MODEL_LBA_DATA_SIZE_SHIFT_BITS));
    os::kernel::NvmeNamespaceIdentity namespace_identity{};
    const bool namespace_identity_valid =
        os::kernel::ParseNvmeNamespaceIdentity(identify_data, sizeof(identify_data),
                                               namespace_identity) ==
            os::kernel::NvmeModelStatus::Succeeded &&
        namespace_identity.logical_block_count == OS_TEST_NVME_MODEL_NAMESPACE_BLOCK_COUNT &&
        namespace_identity.logical_block_size_bytes ==
            OS_TEST_NVME_MODEL_EXPECTED_LBA_SIZE_BYTES &&
        namespace_identity.metadata_size_bytes == 0ULL;
    test_context.Expect(controller_identity_valid && namespace_identity_valid,
                        OS_TEST_NVME_MODEL_IDENTIFY_DATA);

    os::kernel::BlockDeviceGeometry geometry{};
    uint64_t io_queue_depth = 0ULL;
    os::kernel::NvmeNamespaceIdentity metadata_namespace_identity = namespace_identity;
    metadata_namespace_identity.metadata_size_bytes = 8ULL;
    test_context.Expect(
        os::kernel::CalculateNvmeBlockDeviceGeometry(
            controller_identity, namespace_identity, geometry) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            geometry.logical_block_size_bytes == OS_TEST_NVME_MODEL_EXPECTED_LBA_SIZE_BYTES &&
            geometry.logical_block_count == OS_TEST_NVME_MODEL_NAMESPACE_BLOCK_COUNT &&
            geometry.maximum_transfer_block_count ==
                OS_TEST_NVME_MODEL_EXPECTED_MAXIMUM_TRANSFER_BLOCK_COUNT &&
            geometry.maximum_outstanding_request_count ==
                os::kernel::OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT &&
            geometry.write_supported &&
            geometry.flush_supported &&
            os::kernel::SelectNvmeIoQueueDepth(capabilities, io_queue_depth) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            io_queue_depth == OS_TEST_NVME_MODEL_IO_QUEUE_DEPTH &&
            os::kernel::CalculateNvmeBlockDeviceGeometry(
                controller_identity, metadata_namespace_identity, geometry) ==
                os::kernel::NvmeModelStatus::InvalidTransfer,
        OS_TEST_NVME_MODEL_BLOCK_GEOMETRY);

    uint64_t submission_tail = 0ULL;
    uint64_t completion_head = 0ULL;
    bool completion_phase = true;
    test_context.Expect(
        os::kernel::AdvanceNvmeSubmissionTail(os::kernel::OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH,
                                              submission_tail) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            submission_tail == 1ULL &&
            os::kernel::AdvanceNvmeSubmissionTail(os::kernel::OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH,
                                                  submission_tail) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            submission_tail == 0ULL &&
            os::kernel::AdvanceNvmeCompletionHead(os::kernel::OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH,
                                                  completion_head, completion_phase) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            completion_head == 1ULL && completion_phase &&
            os::kernel::AdvanceNvmeCompletionHead(os::kernel::OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH,
                                                  completion_head, completion_phase) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            completion_head == 0ULL && !completion_phase,
        OS_TEST_NVME_MODEL_QUEUE_WRAP);
    return test_context.ExitCode();
}
