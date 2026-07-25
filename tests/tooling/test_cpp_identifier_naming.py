from pathlib import Path
import unittest

from tools.os_tools.cpp_identifier_naming import (
    findNamespaceNamingViolations,
    maskCppCommentsAndLiterals,
)


OS_TEST_CPP_IDENTIFIER_NAMING_SOURCE_PATH = Path("sample.cpp")


class CppIdentifierNamingToolTests(unittest.TestCase):
    def testAcceptsNestedLowercaseWordsAndAnonymousNamespace(self) -> None:
        source_text = """
namespace os::kernel::memory {
}
namespace {
}
namespace detail = os::kernel;
"""

        self.assertEqual(
            findNamespaceNamingViolations(
                OS_TEST_CPP_IDENTIFIER_NAMING_SOURCE_PATH,
                source_text,
            ),
            [],
        )

    def testRejectsUnderscoreUppercaseAndDigitNamespaceLevels(self) -> None:
        source_text = """
namespace os::memory_manager {
}
namespace os::Kernel {
}
namespace os::v2 {
}
"""

        violations = findNamespaceNamingViolations(
            OS_TEST_CPP_IDENTIFIER_NAMING_SOURCE_PATH,
            source_text,
        )

        self.assertEqual(
            [violation.namespace_level for violation in violations],
            ["memory_manager", "Kernel", "v2"],
        )
        self.assertEqual(
            [violation.line_number for violation in violations],
            [2, 4, 6],
        )

    def testIgnoresNamespaceTextInsideCommentsAndLiterals(self) -> None:
        source_text = r'''
// namespace invalid_comment {
/* namespace invalid_block {
} */
const char *text = "namespace InvalidString {";
const char *raw_text = R"tag(namespace invalid_raw {
})tag";
namespace os::kernel {
}
'''

        self.assertEqual(
            findNamespaceNamingViolations(
                OS_TEST_CPP_IDENTIFIER_NAMING_SOURCE_PATH,
                source_text,
            ),
            [],
        )

    def testMaskPreservesLineLayoutForDiagnostics(self) -> None:
        source_text = 'const char *text = "first\\nsecond";\nnamespace os {}\n'

        masked_source_text = maskCppCommentsAndLiterals(source_text)

        self.assertEqual(
            masked_source_text.count("\n"),
            source_text.count("\n"),
        )
        self.assertIn("namespace os", masked_source_text)


if __name__ == "__main__":
    unittest.main()
