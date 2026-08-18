#include "os/user/shell_line_editor.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_EDITOR_SUITE_NAME = "user/shell_line_editor/unit";
constexpr std::string_view OS_TEST_SHELL_EDITOR_INSERT = "光标中间插入与退格必须保持字节顺序和边界";
constexpr std::string_view OS_TEST_SHELL_EDITOR_HISTORY = "历史必须去重、上下浏览并恢复未提交草稿";
constexpr std::string_view OS_TEST_SHELL_EDITOR_COMPLETION =
    "唯一补全必须追加空格，多候选只扩展共同前缀";
constexpr char OS_TEST_SHELL_EDITOR_EXPECTED_LINE[] = "abc";
constexpr char OS_TEST_SHELL_EDITOR_FIRST_HISTORY[] = "first";
constexpr char OS_TEST_SHELL_EDITOR_SECOND_HISTORY[] = "second";
constexpr char OS_TEST_SHELL_EDITOR_DRAFT[] = "draft";
constexpr char OS_TEST_SHELL_EDITOR_ECHO[] = "echo";
constexpr char OS_TEST_SHELL_EDITOR_ENV[] = "env";
constexpr char OS_TEST_SHELL_EDITOR_ERROR[] = "err";
constexpr uint64_t OS_TEST_SHELL_EDITOR_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_EDITOR_FIRST_VALUE = 1ULL;

[[nodiscard]] bool LineEquals(const os::user::ShellLineEditor &editor, const char *const expected,
                              const uint64_t expected_length_bytes) noexcept {
    if (expected == nullptr || editor.Length() != expected_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_SHELL_EDITOR_EMPTY_VALUE; byte_index < expected_length_bytes;
         ++byte_index) {
        if (editor.Bytes()[byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return true;
}

void InsertText(os::user::ShellLineEditor &editor, const char *const text,
                const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_TEST_SHELL_EDITOR_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        static_cast<void>(editor.Insert(text[byte_index]));
    }
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_EDITOR_SUITE_NAME};
    os::user::ShellLineEditor editor{};
    static_cast<void>(editor.Insert('a'));
    static_cast<void>(editor.Insert('c'));
    const bool insert_valid =
        editor.MoveLeft() && editor.Insert('b') && editor.MoveRight() && editor.Backspace() &&
        editor.Insert('c') &&
        LineEquals(editor, OS_TEST_SHELL_EDITOR_EXPECTED_LINE,
                   sizeof(OS_TEST_SHELL_EDITOR_EXPECTED_LINE) - OS_TEST_SHELL_EDITOR_FIRST_VALUE) &&
        editor.Cursor() == editor.Length() && editor.Validate();
    test_context.Expect(insert_valid, OS_TEST_SHELL_EDITOR_INSERT);

    editor.Clear();
    InsertText(editor, OS_TEST_SHELL_EDITOR_FIRST_HISTORY,
               sizeof(OS_TEST_SHELL_EDITOR_FIRST_HISTORY) - OS_TEST_SHELL_EDITOR_FIRST_VALUE);
    editor.CommitHistory();
    editor.Clear();
    InsertText(editor, OS_TEST_SHELL_EDITOR_SECOND_HISTORY,
               sizeof(OS_TEST_SHELL_EDITOR_SECOND_HISTORY) - OS_TEST_SHELL_EDITOR_FIRST_VALUE);
    editor.CommitHistory();
    editor.Clear();
    InsertText(editor, OS_TEST_SHELL_EDITOR_DRAFT,
               sizeof(OS_TEST_SHELL_EDITOR_DRAFT) - OS_TEST_SHELL_EDITOR_FIRST_VALUE);
    const bool history_valid =
        editor.SelectPreviousHistory() &&
        LineEquals(editor, OS_TEST_SHELL_EDITOR_SECOND_HISTORY,
                   sizeof(OS_TEST_SHELL_EDITOR_SECOND_HISTORY) -
                       OS_TEST_SHELL_EDITOR_FIRST_VALUE) &&
        editor.SelectPreviousHistory() &&
        LineEquals(editor, OS_TEST_SHELL_EDITOR_FIRST_HISTORY,
                   sizeof(OS_TEST_SHELL_EDITOR_FIRST_HISTORY) - OS_TEST_SHELL_EDITOR_FIRST_VALUE) &&
        editor.SelectNextHistory() && editor.SelectNextHistory() &&
        LineEquals(editor, OS_TEST_SHELL_EDITOR_DRAFT,
                   sizeof(OS_TEST_SHELL_EDITOR_DRAFT) - OS_TEST_SHELL_EDITOR_FIRST_VALUE) &&
        editor.HistoryCount() == 2ULL && editor.Validate();
    test_context.Expect(history_valid, OS_TEST_SHELL_EDITOR_HISTORY);

    constexpr os::user::ShellCompletionCandidate candidates[]{
        {OS_TEST_SHELL_EDITOR_ECHO, 4U},
        {OS_TEST_SHELL_EDITOR_ENV, 3U},
        {OS_TEST_SHELL_EDITOR_ERROR, 3U},
    };
    editor.Clear();
    InsertText(editor, "ech", 3ULL);
    const bool unique_completion =
        editor.CompleteCommand(candidates, 3ULL) && LineEquals(editor, "echo ", 5ULL);
    editor.Clear();
    InsertText(editor, "e", OS_TEST_SHELL_EDITOR_FIRST_VALUE);
    const bool common_completion = editor.CompleteCommand(candidates, 3ULL) &&
                                   LineEquals(editor, "e", OS_TEST_SHELL_EDITOR_FIRST_VALUE);
    test_context.Expect(unique_completion && !common_completion && editor.Validate(),
                        OS_TEST_SHELL_EDITOR_COMPLETION);

    return test_context.ExitCode();
}
