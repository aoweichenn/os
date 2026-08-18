#include "os/user/shell_line_editor.hpp"
#include "test_context.hpp"

#include <random>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_EDITOR_RANDOM_SUITE_NAME =
    "user/shell_line_editor/randomized";
constexpr std::string_view OS_TEST_SHELL_EDITOR_RANDOM_MODEL =
    "20000 步插入、退格、左右移动和清空必须逐步匹配独立字符串模型";
constexpr os::test::RandomSeed OS_TEST_SHELL_EDITOR_RANDOM_SEED = 0xED170220260818ULL;
constexpr os::test::TestCount OS_TEST_SHELL_EDITOR_RANDOM_ITERATIONS = 20000ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_EDITOR_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_SHELL_EDITOR_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> operation_distribution{0ULL, 4ULL};
    std::uniform_int_distribution<uint64_t> character_distribution{0ULL, 25ULL};
    os::user::ShellLineEditor editor{};
    std::string model{};
    uint64_t cursor = 0ULL;
    bool valid = true;
    for (os::test::TestCount iteration = 0ULL;
         iteration < OS_TEST_SHELL_EDITOR_RANDOM_ITERATIONS && valid; ++iteration) {
        const uint64_t operation = operation_distribution(generator);
        if (operation == 0ULL) {
            const char character =
                static_cast<char>('a' + static_cast<char>(character_distribution(generator)));
            const bool result = editor.Insert(character);
            const bool expected = model.size() < os::user::OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES;
            valid = result == expected;
            if (expected) {
                model.insert(model.begin() + static_cast<std::string::difference_type>(cursor),
                             character);
                ++cursor;
            }
        } else if (operation == 1ULL) {
            const bool result = editor.Backspace();
            const bool expected = cursor != 0ULL;
            valid = result == expected;
            if (expected) {
                model.erase(cursor - 1ULL, 1ULL);
                --cursor;
            }
        } else if (operation == 2ULL) {
            const bool result = editor.MoveLeft();
            const bool expected = cursor != 0ULL;
            valid = result == expected;
            if (expected) {
                --cursor;
            }
        } else if (operation == 3ULL) {
            const bool result = editor.MoveRight();
            const bool expected = cursor < model.size();
            valid = result == expected;
            if (expected) {
                ++cursor;
            }
        } else {
            editor.Clear();
            model.clear();
            cursor = 0ULL;
        }
        valid = valid && editor.Length() == model.size() && editor.Cursor() == cursor &&
                std::string_view{editor.Bytes(), editor.Length()} == model && editor.Validate();
    }
    test_context.Expect(valid, OS_TEST_SHELL_EDITOR_RANDOM_MODEL);
    return test_context.ExitCode();
}
