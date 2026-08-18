from html.parser import HTMLParser
from pathlib import Path
import unittest


OS_TEST_PROJECT_ROOT = Path(__file__).resolve().parents[2]
OS_TEST_NOVNC_MOBILE_PAGE = OS_TEST_PROJECT_ROOT / "tools" / "novnc_mobile.html"
OS_TEST_NOVNC_REQUIRED_ELEMENT_IDENTIFIERS = {
    "screen",
    "status",
    "command",
    "send",
    "enter",
    "backspace",
    "interrupt",
    "stop",
    "scale",
}


class ElementIdentifierParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.identifiers: set[str] = set()

    def handle_starttag(
        self,
        tag: str,
        attributes: list[tuple[str, str | None]],
    ) -> None:
        del tag
        for name, value in attributes:
            if name == "id" and value is not None:
                self.identifiers.add(value)


class NovncMobilePageTests(unittest.TestCase):
    def testProvidesPersistentMobileCommandControls(self) -> None:
        source = OS_TEST_NOVNC_MOBILE_PAGE.read_text(encoding="utf-8")
        parser = ElementIdentifierParser()
        parser.feed(source)

        self.assertTrue(
            OS_TEST_NOVNC_REQUIRED_ELEMENT_IDENTIFIERS.issubset(
                parser.identifiers
            )
        )
        self.assertIn("inputmode=\"text\"", source)
        self.assertIn("SendControlCharacter('c', 'KeyC')", source)
        self.assertIn("SendControlCharacter('z', 'KeyZ')", source)

    def testUsesSameOriginWebsocketAndCrispNativeScaling(self) -> None:
        source = OS_TEST_NOVNC_MOBILE_PAGE.read_text(encoding="utf-8")

        self.assertIn("window.location.host", source)
        self.assertIn("/websockify", source)
        self.assertIn("rfb.viewOnly = false", source)
        self.assertIn("rfb.scaleViewport = false", source)
        self.assertIn("let fitViewport = true", source)
        self.assertIn("screen.classList.toggle('fit', fitViewport)", source)
        self.assertIn("min-width: 0", source)
        self.assertIn("height: calc(100vh - 112px)", source)
        self.assertIn("@media (orientation: landscape)", source)
        self.assertIn("image-rendering: pixelated", source)
        self.assertNotIn("wss://", source)
        self.assertNotIn("ws://", source)


if __name__ == "__main__":
    unittest.main()
