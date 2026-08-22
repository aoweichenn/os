#include <os/kernel/memory/memory_pressure.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MEMORY_PRESSURE_SUITE_NAME = "kernel/memory_pressure/unit";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_WATERMARKS =
    "32 GiB 算法边界和 4 GiB 实体主规格必须生成有序 min/low/high 水位";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_DECISIONS =
    "用户、内核与回收分配必须遵守水位和紧急保留差异";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_ACCOUNTING = "驻留提交、释放和回收统计必须守恒";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_RECLAIM_PLAN =
    "回收计划必须先利用干净缓存并有界安排回写和 swap";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_RECLAIM_FAIRNESS =
    "swappiness 必须形成文件/匿名配额并把未用份额归还给可推进侧";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_RECLAIM_EXECUTION =
    "执行器必须固定 clean、writeback、swap 顺序并区分失败阶段";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_OVERCOMMIT =
    "overcommit 0/1/2 编号和严格模式 50% RAM 加 swap 上限必须稳定";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_OOM =
    "OOM 必须保护 PID 1 并按调整后分数、占用和 PID 确定性选择";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_FAILURES =
    "无效配置、记账下溢和无候选 OOM 必须明确失败";

constexpr uint64_t OS_TEST_MEMORY_PRESSURE_32_GIB_PAGE_COUNT = 8388608ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_EXPECTED_MINIMUM_PAGE_COUNT = 5792ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_EXPECTED_LOW_PAGE_COUNT = 14180ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_EXPECTED_HIGH_PAGE_COUNT = 22568ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_PRODUCTION_RESIDENT_PAGE_COUNT = 1048576ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_PRODUCTION_MINIMUM_PAGE_COUNT = 2048ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_PRODUCTION_LOW_PAGE_COUNT = 3096ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_PRODUCTION_HIGH_PAGE_COUNT = 4144ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_SMALL_RESIDENT_LIMIT = 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_SMALL_INITIAL_RESIDENT = 700ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_SMALL_SWAP_PAGE_COUNT = 512ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_USER_REQUEST_PAGE_COUNT = 100ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_KERNEL_REQUEST_PAGE_COUNT = 50ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_EXPECTED_USER_RECLAIM_PAGE_COUNT = 160ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_COMMIT_PAGE_COUNT = 10ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RELEASE_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RECLAIMED_PAGE_COUNT = 6ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RECONFIGURED_RESIDENT_LIMIT = 900ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_PHYSICAL_PAGES = 1000ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_SWAP_PAGES = 200ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_ADMIN_RESERVE = 20ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_USER_RESERVE = 30ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_NORMAL_LIMIT = 650ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_PRIVILEGED_LIMIT = 700ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_OOM_ALLOWED_PAGES = 1000ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RECLAIM_STAGE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_CLEAN_STAGE = 1ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_WRITEBACK_STAGE = 2ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_SWAP_STAGE = 3ULL;

struct ReclaimOperationContext final {
    uint64_t clean_capacity;
    uint64_t writeback_capacity;
    uint64_t swap_capacity;
    uint64_t stages[OS_TEST_MEMORY_PRESSURE_RECLAIM_STAGE_COUNT];
    uint64_t stage_count;
    uint64_t failing_stage;
};

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] bool ExecuteReclaimStage(ReclaimOperationContext &context, const uint64_t stage,
                                       const uint64_t capacity, const uint64_t requested_page_count,
                                       uint64_t &reclaimed_page_count) noexcept {
    reclaimed_page_count = 0ULL;
    if (context.stage_count >= OS_TEST_MEMORY_PRESSURE_RECLAIM_STAGE_COUNT ||
        context.failing_stage == stage) {
        return false;
    }
    context.stages[context.stage_count] = stage;
    ++context.stage_count;
    reclaimed_page_count = Minimum(capacity, requested_page_count);
    return true;
}

[[nodiscard]] bool ReclaimClean(void *const context, const uint64_t requested_page_count,
                                uint64_t &reclaimed_page_count) noexcept {
    return context != nullptr &&
           ExecuteReclaimStage(*static_cast<ReclaimOperationContext *>(context),
                               OS_TEST_MEMORY_PRESSURE_CLEAN_STAGE,
                               static_cast<ReclaimOperationContext *>(context)->clean_capacity,
                               requested_page_count, reclaimed_page_count);
}

[[nodiscard]] bool ReclaimWritten(void *const context, const uint64_t requested_page_count,
                                  uint64_t &reclaimed_page_count) noexcept {
    return context != nullptr &&
           ExecuteReclaimStage(*static_cast<ReclaimOperationContext *>(context),
                               OS_TEST_MEMORY_PRESSURE_WRITEBACK_STAGE,
                               static_cast<ReclaimOperationContext *>(context)->writeback_capacity,
                               requested_page_count, reclaimed_page_count);
}

[[nodiscard]] bool ReclaimAnonymous(void *const context, const uint64_t requested_page_count,
                                    uint64_t &reclaimed_page_count) noexcept {
    return context != nullptr &&
           ExecuteReclaimStage(*static_cast<ReclaimOperationContext *>(context),
                               OS_TEST_MEMORY_PRESSURE_SWAP_STAGE,
                               static_cast<ReclaimOperationContext *>(context)->swap_capacity,
                               requested_page_count, reclaimed_page_count);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_MEMORY_PRESSURE_SUITE_NAME};

    os::kernel::MemoryWatermarks reference_watermarks{};
    os::kernel::MemoryWatermarks production_watermarks{};
    const bool watermarks_valid =
        os::kernel::CalculateMemoryWatermarks(
            OS_TEST_MEMORY_PRESSURE_32_GIB_PAGE_COUNT,
            os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_WATERMARK_SCALE_FACTOR,
            reference_watermarks) == os::kernel::MemoryPressureStatus::Succeeded &&
        reference_watermarks.resident_limit_page_count ==
            OS_TEST_MEMORY_PRESSURE_32_GIB_PAGE_COUNT &&
        reference_watermarks.minimum_free_page_count ==
            OS_TEST_MEMORY_PRESSURE_EXPECTED_MINIMUM_PAGE_COUNT &&
        reference_watermarks.low_free_page_count ==
            OS_TEST_MEMORY_PRESSURE_EXPECTED_LOW_PAGE_COUNT &&
        reference_watermarks.high_free_page_count ==
            OS_TEST_MEMORY_PRESSURE_EXPECTED_HIGH_PAGE_COUNT &&
        os::kernel::ClassifyMemoryPressure(reference_watermarks,
                                           OS_TEST_MEMORY_PRESSURE_EXPECTED_HIGH_PAGE_COUNT) ==
            os::kernel::MemoryPressureLevel::Balanced &&
        os::kernel::ClassifyMemoryPressure(
            reference_watermarks, OS_TEST_MEMORY_PRESSURE_EXPECTED_MINIMUM_PAGE_COUNT - 1ULL) ==
            os::kernel::MemoryPressureLevel::BelowMinimum &&
        os::kernel::CalculateMemoryWatermarks(
            OS_TEST_MEMORY_PRESSURE_PRODUCTION_RESIDENT_PAGE_COUNT,
            os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_WATERMARK_SCALE_FACTOR,
            production_watermarks) == os::kernel::MemoryPressureStatus::Succeeded &&
        production_watermarks.minimum_free_page_count ==
            OS_TEST_MEMORY_PRESSURE_PRODUCTION_MINIMUM_PAGE_COUNT &&
        production_watermarks.low_free_page_count ==
            OS_TEST_MEMORY_PRESSURE_PRODUCTION_LOW_PAGE_COUNT &&
        production_watermarks.high_free_page_count ==
            OS_TEST_MEMORY_PRESSURE_PRODUCTION_HIGH_PAGE_COUNT;
    test_context.Expect(watermarks_valid, OS_TEST_MEMORY_PRESSURE_WATERMARKS);

    os::kernel::MemoryPressureController controller{};
    const os::kernel::MemoryPressureConfiguration pressure_configuration{
        .managed_page_count = OS_TEST_MEMORY_PRESSURE_32_GIB_PAGE_COUNT,
        .initial_resident_page_count = OS_TEST_MEMORY_PRESSURE_SMALL_INITIAL_RESIDENT,
        .resident_limit_page_count = OS_TEST_MEMORY_PRESSURE_SMALL_RESIDENT_LIMIT,
        .swap_page_count = OS_TEST_MEMORY_PRESSURE_SMALL_SWAP_PAGE_COUNT,
        .watermark_scale_factor =
            os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_WATERMARK_SCALE_FACTOR,
        .swappiness = os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS,
    };
    os::kernel::MemoryAllocationDecision user_decision{};
    os::kernel::MemoryAllocationDecision background_window_decision{};
    os::kernel::MemoryAllocationDecision kernel_decision{};
    os::kernel::MemoryAllocationDecision impossible_decision{};
    const bool decisions_valid =
        controller.Initialize(pressure_configuration) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        controller.PrepareAllocation(OS_TEST_MEMORY_PRESSURE_USER_REQUEST_PAGE_COUNT,
                                     os::kernel::MemoryAllocationClass::User, user_decision) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        user_decision.action == os::kernel::MemoryAllocationAction::Reclaim &&
        user_decision.level == os::kernel::MemoryPressureLevel::BelowMinimum &&
        user_decision.target_reclaim_page_count ==
            OS_TEST_MEMORY_PRESSURE_EXPECTED_USER_RECLAIM_PAGE_COUNT &&
        controller.PrepareAllocation(OS_TEST_MEMORY_PRESSURE_KERNEL_REQUEST_PAGE_COUNT,
                                     os::kernel::MemoryAllocationClass::User,
                                     background_window_decision) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        background_window_decision.action == os::kernel::MemoryAllocationAction::Allow &&
        background_window_decision.level == os::kernel::MemoryPressureLevel::BelowLow &&
        controller.PrepareAllocation(OS_TEST_MEMORY_PRESSURE_KERNEL_REQUEST_PAGE_COUNT,
                                     os::kernel::MemoryAllocationClass::Kernel, kernel_decision) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        kernel_decision.action == os::kernel::MemoryAllocationAction::Allow &&
        kernel_decision.level == os::kernel::MemoryPressureLevel::BelowLow &&
        controller.PrepareAllocation(OS_TEST_MEMORY_PRESSURE_SMALL_RESIDENT_LIMIT + 1ULL,
                                     os::kernel::MemoryAllocationClass::User,
                                     impossible_decision) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        impossible_decision.action == os::kernel::MemoryAllocationAction::Reject;
    test_context.Expect(decisions_valid, OS_TEST_MEMORY_PRESSURE_DECISIONS);

    const bool accounting_valid =
        controller.CommitAllocation(OS_TEST_MEMORY_PRESSURE_COMMIT_PAGE_COUNT) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        controller.ReleaseResident(OS_TEST_MEMORY_PRESSURE_RELEASE_PAGE_COUNT) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        controller.RecordReclaim(OS_TEST_MEMORY_PRESSURE_RECLAIMED_PAGE_COUNT) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        controller.SynchronizeResident(OS_TEST_MEMORY_PRESSURE_SMALL_INITIAL_RESIDENT) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        controller.ConfigureResidentLimit(OS_TEST_MEMORY_PRESSURE_RECONFIGURED_RESIDENT_LIMIT) ==
            os::kernel::MemoryPressureStatus::Succeeded &&
        controller.ConfigureResidentLimit(OS_TEST_MEMORY_PRESSURE_SMALL_INITIAL_RESIDENT - 1ULL) ==
            os::kernel::MemoryPressureStatus::ResidentLimitExceeded &&
        controller.Statistics().resident_page_count ==
            OS_TEST_MEMORY_PRESSURE_SMALL_INITIAL_RESIDENT &&
        controller.Statistics().watermarks.resident_limit_page_count ==
            OS_TEST_MEMORY_PRESSURE_RECONFIGURED_RESIDENT_LIMIT &&
        controller.Statistics().allocation_request_count == 4ULL &&
        controller.Statistics().reclaim_request_count == 1ULL &&
        controller.Statistics().allowed_allocation_count == 2ULL &&
        controller.Statistics().rejected_allocation_count == 1ULL &&
        controller.Statistics().reclaim_attempt_count == 1ULL &&
        controller.Validate() == os::kernel::MemoryPressureStatus::Succeeded;
    test_context.Expect(accounting_valid, OS_TEST_MEMORY_PRESSURE_ACCOUNTING);

    os::kernel::MemoryReclaimPlan reclaim_plan{};
    const bool reclaim_plan_valid =
        os::kernel::PlanMemoryReclaim(
            os::kernel::MemoryReclaimInput{
                .target_page_count = 12ULL,
                .clean_file_page_count = 3ULL,
                .dirty_file_page_count = 4ULL,
                .anonymous_page_count = 10ULL,
                .free_swap_page_count = 2ULL,
                .swappiness = os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS,
            },
            reclaim_plan) == os::kernel::MemoryReclaimPlanStatus::Succeeded &&
        reclaim_plan.file_budget_page_count == 7ULL &&
        reclaim_plan.anonymous_budget_page_count == 2ULL &&
        reclaim_plan.clean_file_page_count == 3ULL &&
        reclaim_plan.writeback_file_page_count == 4ULL &&
        reclaim_plan.swap_out_page_count == 2ULL &&
        reclaim_plan.planned_reclaim_page_count == 9ULL &&
        reclaim_plan.unreclaimable_page_count == 3ULL;
    test_context.Expect(reclaim_plan_valid, OS_TEST_MEMORY_PRESSURE_RECLAIM_PLAN);

    os::kernel::MemoryReclaimPlan balanced_plan{};
    os::kernel::MemoryReclaimPlan file_only_plan{};
    os::kernel::MemoryReclaimPlan anonymous_only_plan{};
    os::kernel::MemoryReclaimPlan donated_plan{};
    const bool fairness_valid =
        os::kernel::PlanMemoryReclaim(
            os::kernel::MemoryReclaimInput{
                .target_page_count = 64ULL,
                .clean_file_page_count = 100ULL,
                .dirty_file_page_count = 0ULL,
                .anonymous_page_count = 100ULL,
                .free_swap_page_count = 100ULL,
                .swappiness = os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS,
            },
            balanced_plan) == os::kernel::MemoryReclaimPlanStatus::Succeeded &&
        balanced_plan.file_budget_page_count == 45ULL &&
        balanced_plan.anonymous_budget_page_count == 19ULL &&
        os::kernel::PlanMemoryReclaim(
            os::kernel::MemoryReclaimInput{
                .target_page_count = 64ULL,
                .clean_file_page_count = 100ULL,
                .dirty_file_page_count = 0ULL,
                .anonymous_page_count = 100ULL,
                .free_swap_page_count = 100ULL,
                .swappiness = 0ULL,
            },
            file_only_plan) == os::kernel::MemoryReclaimPlanStatus::Succeeded &&
        file_only_plan.file_budget_page_count == 64ULL &&
        file_only_plan.anonymous_budget_page_count == 0ULL &&
        os::kernel::PlanMemoryReclaim(
            os::kernel::MemoryReclaimInput{
                .target_page_count = 64ULL,
                .clean_file_page_count = 100ULL,
                .dirty_file_page_count = 0ULL,
                .anonymous_page_count = 100ULL,
                .free_swap_page_count = 100ULL,
                .swappiness = os::kernel::OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS,
            },
            anonymous_only_plan) == os::kernel::MemoryReclaimPlanStatus::Succeeded &&
        anonymous_only_plan.file_budget_page_count == 0ULL &&
        anonymous_only_plan.anonymous_budget_page_count == 64ULL &&
        os::kernel::PlanMemoryReclaim(
            os::kernel::MemoryReclaimInput{
                .target_page_count = 64ULL,
                .clean_file_page_count = 10ULL,
                .dirty_file_page_count = 0ULL,
                .anonymous_page_count = 100ULL,
                .free_swap_page_count = 100ULL,
                .swappiness = os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS,
            },
            donated_plan) == os::kernel::MemoryReclaimPlanStatus::Succeeded &&
        donated_plan.file_budget_page_count == 10ULL &&
        donated_plan.anonymous_budget_page_count == 54ULL &&
        donated_plan.unreclaimable_page_count == 0ULL;
    test_context.Expect(fairness_valid, OS_TEST_MEMORY_PRESSURE_RECLAIM_FAIRNESS);

    ReclaimOperationContext execution_context{
        .clean_capacity = 3ULL,
        .writeback_capacity = 4ULL,
        .swap_capacity = 2ULL,
        .stages = {},
        .stage_count = 0ULL,
        .failing_stage = 0ULL,
    };
    os::kernel::MemoryReclaimExecutionResult execution_result{};
    const os::kernel::MemoryReclaimOperations reclaim_operations{
        .reclaim_clean_file_pages = ReclaimClean,
        .writeback_and_reclaim_file_pages = ReclaimWritten,
        .swap_out_anonymous_pages = ReclaimAnonymous,
    };
    const bool execution_valid =
        os::kernel::ExecuteMemoryReclaim(reclaim_plan, reclaim_operations, &execution_context,
                                         execution_result) ==
            os::kernel::MemoryReclaimExecutionStatus::Succeeded &&
        execution_context.stage_count == OS_TEST_MEMORY_PRESSURE_RECLAIM_STAGE_COUNT &&
        execution_context.stages[0ULL] == OS_TEST_MEMORY_PRESSURE_CLEAN_STAGE &&
        execution_context.stages[1ULL] == OS_TEST_MEMORY_PRESSURE_WRITEBACK_STAGE &&
        execution_context.stages[2ULL] == OS_TEST_MEMORY_PRESSURE_SWAP_STAGE &&
        execution_result.clean_file_page_count == 3ULL &&
        execution_result.reclaimed_written_file_page_count == 4ULL &&
        execution_result.swapped_anonymous_page_count == 2ULL &&
        execution_result.reclaimed_page_count == 9ULL;
    execution_context = ReclaimOperationContext{
        .clean_capacity = 3ULL,
        .writeback_capacity = 4ULL,
        .swap_capacity = 2ULL,
        .stages = {},
        .stage_count = 0ULL,
        .failing_stage = OS_TEST_MEMORY_PRESSURE_WRITEBACK_STAGE,
    };
    const bool failure_stage_valid =
        os::kernel::ExecuteMemoryReclaim(reclaim_plan, reclaim_operations, &execution_context,
                                         execution_result) ==
            os::kernel::MemoryReclaimExecutionStatus::FileWritebackFailed &&
        execution_context.stage_count == 1ULL &&
        execution_context.stages[0ULL] == OS_TEST_MEMORY_PRESSURE_CLEAN_STAGE;
    execution_context = ReclaimOperationContext{
        .clean_capacity = 0ULL,
        .writeback_capacity = 0ULL,
        .swap_capacity = 0ULL,
        .stages = {},
        .stage_count = 0ULL,
        .failing_stage = 0ULL,
    };
    const bool no_progress_valid =
        os::kernel::ExecuteMemoryReclaim(reclaim_plan, reclaim_operations, &execution_context,
                                         execution_result) ==
            os::kernel::MemoryReclaimExecutionStatus::NoProgress &&
        execution_result.reclaimed_page_count == 0ULL;
    execution_context = ReclaimOperationContext{
        .clean_capacity = 3ULL,
        .writeback_capacity = 4ULL,
        .swap_capacity = 2ULL,
        .stages = {},
        .stage_count = 0ULL,
        .failing_stage = OS_TEST_MEMORY_PRESSURE_SWAP_STAGE,
    };
    const bool swap_failure_valid =
        os::kernel::ExecuteMemoryReclaim(reclaim_plan, reclaim_operations, &execution_context,
                                         execution_result) ==
        os::kernel::MemoryReclaimExecutionStatus::AnonymousSwapFailed;
    test_context.Expect(execution_valid && failure_stage_valid && no_progress_valid &&
                            swap_failure_valid,
                        OS_TEST_MEMORY_PRESSURE_RECLAIM_EXECUTION);

    os::kernel::MemoryOvercommitAccountant strict_accountant{};
    const os::kernel::MemoryOvercommitConfiguration strict_configuration{
        .mode = os::kernel::MemoryOvercommitMode::Never,
        .physical_page_count = OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_PHYSICAL_PAGES,
        .swap_page_count = OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_SWAP_PAGES,
        .overcommit_ratio_percent =
            os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_OVERCOMMIT_RATIO_PERCENT,
        .admin_reserve_page_count = OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_ADMIN_RESERVE,
        .user_reserve_page_count = OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_USER_RESERVE,
    };
    const bool overcommit_valid =
        static_cast<uint64_t>(os::kernel::MemoryOvercommitMode::Heuristic) == 0ULL &&
        static_cast<uint64_t>(os::kernel::MemoryOvercommitMode::Always) == 1ULL &&
        static_cast<uint64_t>(os::kernel::MemoryOvercommitMode::Never) == 2ULL &&
        strict_accountant.Initialize(strict_configuration) ==
            os::kernel::MemoryOvercommitStatus::Succeeded &&
        strict_accountant.Statistics().normal_commit_limit_page_count ==
            OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_NORMAL_LIMIT &&
        strict_accountant.Statistics().privileged_commit_limit_page_count ==
            OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_PRIVILEGED_LIMIT &&
        strict_accountant.TryCommit(OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_NORMAL_LIMIT, false) ==
            os::kernel::MemoryOvercommitStatus::Succeeded &&
        strict_accountant.TryCommit(1ULL, false) ==
            os::kernel::MemoryOvercommitStatus::CommitLimitExceeded &&
        strict_accountant.TryCommit(OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_PRIVILEGED_LIMIT -
                                        OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_NORMAL_LIMIT,
                                    true) == os::kernel::MemoryOvercommitStatus::Succeeded &&
        strict_accountant.Uncommit(OS_TEST_MEMORY_PRESSURE_OVERCOMMIT_PRIVILEGED_LIMIT) ==
            os::kernel::MemoryOvercommitStatus::Succeeded &&
        strict_accountant.Validate() == os::kernel::MemoryOvercommitStatus::Succeeded;
    test_context.Expect(overcommit_valid, OS_TEST_MEMORY_PRESSURE_OVERCOMMIT);

    const os::kernel::OomCandidate oom_candidates[] = {
        {
            .process_id = 1ULL,
            .resident_page_count = 900ULL,
            .swapped_page_count = 0ULL,
            .score_adjustment = 1000LL,
            .protected_process = false,
            .active = true,
        },
        {
            .process_id = 2ULL,
            .resident_page_count = 100ULL,
            .swapped_page_count = 0ULL,
            .score_adjustment = 0LL,
            .protected_process = false,
            .active = true,
        },
        {
            .process_id = 3ULL,
            .resident_page_count = 50ULL,
            .swapped_page_count = 0ULL,
            .score_adjustment = 100LL,
            .protected_process = false,
            .active = true,
        },
        {
            .process_id = 4ULL,
            .resident_page_count = 180ULL,
            .swapped_page_count = 20ULL,
            .score_adjustment = -50LL,
            .protected_process = false,
            .active = true,
        },
    };
    os::kernel::OomVictim victim{};
    const bool oom_valid =
        os::kernel::SelectOomVictim(oom_candidates, 4ULL, OS_TEST_MEMORY_PRESSURE_OOM_ALLOWED_PAGES,
                                    victim) == os::kernel::OomSelectionStatus::Succeeded &&
        victim.process_id == 4ULL && victim.memory_page_count == 200ULL && victim.score == 150ULL &&
        victim.candidate_index == 3ULL;
    test_context.Expect(oom_valid, OS_TEST_MEMORY_PRESSURE_OOM);

    os::kernel::MemoryPressureController invalid_controller{};
    const os::kernel::OomCandidate protected_candidate{
        .process_id = 7ULL,
        .resident_page_count = 1ULL,
        .swapped_page_count = 0ULL,
        .score_adjustment = -1000LL,
        .protected_process = false,
        .active = true,
    };
    const bool failures_valid =
        invalid_controller.Initialize(os::kernel::MemoryPressureConfiguration{}) ==
            os::kernel::MemoryPressureStatus::InvalidConfiguration &&
        controller.ReleaseResident(OS_TEST_MEMORY_PRESSURE_SMALL_RESIDENT_LIMIT) ==
            os::kernel::MemoryPressureStatus::ResidentAccountingUnderflow &&
        os::kernel::SelectOomVictim(&protected_candidate, 1ULL,
                                    OS_TEST_MEMORY_PRESSURE_OOM_ALLOWED_PAGES,
                                    victim) == os::kernel::OomSelectionStatus::NoEligibleProcess;
    test_context.Expect(failures_valid, OS_TEST_MEMORY_PRESSURE_FAILURES);

    return test_context.ExitCode();
}
