import pathlib
import unittest


class FixtureWiringTest(unittest.TestCase):
    def test_quote_store_has_guarded_fixture_routes(self):
        source = pathlib.Path("src/quote_store.cpp").read_text()
        self.assertIn("QUOTE_TEST_PARTIAL_FIXTURE", source)
        self.assertIn("QUOTE_TEST_UNTRADED_FIXTURE", source)
        self.assertIn("QUOTE_TEST_FORCE_NO_CACHE", source)
        self.assertIn("fillQuoteFixture", source)
        self.assertIn("fixture partial", source)
        self.assertIn("fixture untraded", source)
        self.assertIn("fixture no cache", source)
