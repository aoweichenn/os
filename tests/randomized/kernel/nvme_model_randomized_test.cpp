#include <os/kernel/device/nvme_model.hpp>
#include <test_context.hpp>

#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_NVME_RANDOM_SUITE_NAME = "kernel/nvme_model/randomized";
constexpr std::string_view OS_TEST_NVME_RANDOM_QUEUE_CURSOR =
    "十万步 NVMe SQ/CQ 游标必须与独立回绕和 phase 模型一致";
constexpr std::string_view OS_TEST_NVME_RANDOM_IO_COMMAND =
    "十万组 NVMe Read 命令必须精确保留 64 位 SLBA 与零基 NLB";
constexpr std::string_view OS_TEST_NVME_RANDOM_PRP_MAPPING =
    "十万组 1..16 页 NVMe PRP 必须选择空 PRP2、直接页或 packed list";
constexpr os::test::RandomSeed OS_TEST_NVME_RANDOM_SEED = 0x4E564D4551554555ULL;
constexpr os::test::TestCount OS_TEST_NVME_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_MINIMUM_QUEUE_DEPTH = 2ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_MAXIMUM_QUEUE_DEPTH = 4096ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_MAXIMUM_LOGICAL_BLOCK_ADDRESS = UINT64_MAX - 8ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_MINIMUM_LOGICAL_BLOCK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_MAXIMUM_LOGICAL_BLOCK_COUNT = 8ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_DATA_PHYSICAL_ADDRESS = 0x0000000000400000ULL;
constexpr uint32_t OS_TEST_NVME_RANDOM_NAMESPACE_IDENTIFIER = 1U;
constexpr uint64_t OS_TEST_NVME_RANDOM_DWORD_BIT_COUNT = 32ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_COMMAND_IDENTIFIER_RANGE = 65534ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_PRP_PAGE_BASE = 0x0000000000800000ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_PRP_LIST_PHYSICAL_ADDRESS = 0x0000000000900000ULL;
constexpr uint64_t OS_TEST_NVME_RANDOM_PRP_LIST_CAPACITY = 16ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_NVME_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_NVME_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> depth_distribution{
        OS_TEST_NVME_RANDOM_MINIMUM_QUEUE_DEPTH, OS_TEST_NVME_RANDOM_MAXIMUM_QUEUE_DEPTH};
    std::uniform_int_distribution<uint64_t> logical_block_address_distribution{
        0ULL, OS_TEST_NVME_RANDOM_MAXIMUM_LOGICAL_BLOCK_ADDRESS};
    std::uniform_int_distribution<uint64_t> logical_block_count_distribution{
        OS_TEST_NVME_RANDOM_MINIMUM_LOGICAL_BLOCK_COUNT,
        OS_TEST_NVME_RANDOM_MAXIMUM_LOGICAL_BLOCK_COUNT};
    std::uniform_int_distribution<uint64_t> data_page_count_distribution{
        1ULL, os::kernel::OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT};
    const uint64_t queue_depth = depth_distribution(generator);
    uint64_t submission_tail = 0ULL;
    uint64_t completion_head = 0ULL;
    bool completion_phase = true;
    uint64_t expected_submission_tail = 0ULL;
    uint64_t expected_completion_head = 0ULL;
    bool expected_completion_phase = true;
    bool consistent = true;
    bool commands_consistent = true;
    bool prps_consistent = true;
    for (os::test::TestCount operation_index = 0ULL;
         consistent && operation_index < OS_TEST_NVME_RANDOM_OPERATION_COUNT;
         ++operation_index) {
        expected_submission_tail =
            expected_submission_tail + 1ULL == queue_depth ? 0ULL
                                                           : expected_submission_tail + 1ULL;
        if (expected_completion_head + 1ULL == queue_depth) {
            expected_completion_head = 0ULL;
            expected_completion_phase = !expected_completion_phase;
        } else {
            ++expected_completion_head;
        }
        consistent =
            os::kernel::AdvanceNvmeSubmissionTail(queue_depth, submission_tail) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            os::kernel::AdvanceNvmeCompletionHead(queue_depth, completion_head,
                                                  completion_phase) ==
                os::kernel::NvmeModelStatus::Succeeded &&
            submission_tail == expected_submission_tail &&
            completion_head == expected_completion_head &&
            completion_phase == expected_completion_phase;

        const uint64_t logical_block_address =
            logical_block_address_distribution(generator);
        const uint64_t logical_block_count =
            logical_block_count_distribution(generator);
        const uint16_t command_identifier = static_cast<uint16_t>(
            operation_index % OS_TEST_NVME_RANDOM_COMMAND_IDENTIFIER_RANGE + 1ULL);
        os::kernel::NvmeSubmissionEntry command{};
        commands_consistent =
            os::kernel::BuildNvmeIoCommand(
                os::kernel::NvmeIoOperation::Read, command_identifier,
                OS_TEST_NVME_RANDOM_NAMESPACE_IDENTIFIER, logical_block_address,
                logical_block_count, OS_TEST_NVME_RANDOM_DATA_PHYSICAL_ADDRESS, 0ULL,
                command) == os::kernel::NvmeModelStatus::Succeeded &&
            command.dwords[10] == static_cast<uint32_t>(logical_block_address) &&
            command.dwords[11] == static_cast<uint32_t>(
                                      logical_block_address >>
                                      OS_TEST_NVME_RANDOM_DWORD_BIT_COUNT) &&
            command.dwords[12] == static_cast<uint32_t>(logical_block_count - 1ULL) &&
            commands_consistent;

        const uint64_t data_page_count = data_page_count_distribution(generator);
        uint64_t data_page_physical_addresses
            [os::kernel::OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT]{};
        uint64_t prp_list_entries[OS_TEST_NVME_RANDOM_PRP_LIST_CAPACITY]{};
        for (uint64_t page_index = 0ULL; page_index < data_page_count; ++page_index) {
            data_page_physical_addresses[page_index] =
                OS_TEST_NVME_RANDOM_PRP_PAGE_BASE +
                page_index * os::kernel::OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES;
        }
        os::kernel::NvmePrpMapping mapping{};
        const bool mapping_succeeded =
            os::kernel::BuildNvmePrpMapping(
                data_page_physical_addresses, data_page_count,
                OS_TEST_NVME_RANDOM_PRP_LIST_PHYSICAL_ADDRESS, prp_list_entries,
                OS_TEST_NVME_RANDOM_PRP_LIST_CAPACITY,
                mapping) == os::kernel::NvmeModelStatus::Succeeded;
        prps_consistent =
            mapping_succeeded &&
            mapping.first_data_pointer == data_page_physical_addresses[0] &&
            ((data_page_count == 1ULL && mapping.second_data_pointer == 0ULL &&
              mapping.list_entry_count == 0ULL) ||
             (data_page_count == 2ULL &&
              mapping.second_data_pointer == data_page_physical_addresses[1] &&
              mapping.list_entry_count == 0ULL) ||
             (data_page_count > 2ULL &&
              mapping.second_data_pointer ==
                  OS_TEST_NVME_RANDOM_PRP_LIST_PHYSICAL_ADDRESS &&
              mapping.list_entry_count == data_page_count - 1ULL &&
              prp_list_entries[data_page_count - 2ULL] ==
                  data_page_physical_addresses[data_page_count - 1ULL])) &&
            prps_consistent;
    }
    test_context.ExpectRandom(consistent, OS_TEST_NVME_RANDOM_QUEUE_CURSOR,
                              OS_TEST_NVME_RANDOM_SEED,
                              OS_TEST_NVME_RANDOM_OPERATION_COUNT);
    test_context.ExpectRandom(commands_consistent, OS_TEST_NVME_RANDOM_IO_COMMAND,
                              OS_TEST_NVME_RANDOM_SEED,
                              OS_TEST_NVME_RANDOM_OPERATION_COUNT);
    test_context.ExpectRandom(prps_consistent, OS_TEST_NVME_RANDOM_PRP_MAPPING,
                              OS_TEST_NVME_RANDOM_SEED,
                              OS_TEST_NVME_RANDOM_OPERATION_COUNT);
    return test_context.ExitCode();
}
