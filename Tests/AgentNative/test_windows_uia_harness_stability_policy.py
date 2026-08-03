"""Windows UIA harness stability policy for processor inventory tests.

Pins the proven v1.1 harness lifetime intent for v1.0.2 contract tests:

- MessageManager / UIA provider must stay process-stable across the three
  TEST_F cases (no per-test deleteInstance / DeletedAtShutdown teardown).
- Healthy observations must not unconditionally crawl TreeScope_Descendants
  only to build a diagnostic element dump before direct-child assertions.
- Existing direct-child inventory semantics and the 3s soft worker deadline
  must remain present; CTest hard timeout stays 30s.

This is a source/policy gate only — it does not launch GUI or UIA workers.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = (
    ROOT
    / "Tests"
    / "WindowsUIAutomation"
    / "ProcessorInventoryWindowsUIAutomationTests.cpp"
)
CMAKE = ROOT / "Tests" / "WindowsUIAutomation" / "CMakeLists.txt"

# The three semantic cases that share one process under gtest/CTest.
EXPECTED_TEST_CASES = (
    "ExternalClientFindsReadOnlyRootAndDynamicProcessorItem",
    "ExternalClientSeesEveryLoadedProcessorAcrossTabsExactlyOnce",
    "ExternalClientSeesRefreshedPredecessorOnVisibleGenericEditor",
)


def _fixture_body(source: str) -> str:
    match = re.search(
        r"class\s+ProcessorInventoryWindowsUIAutomationTests\s*:\s*public\s+testing::Test\s*"
        r"\{(?P<body>.*?)\};\s*\}?\s*//\s*namespace",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(
            "could not locate ProcessorInventoryWindowsUIAutomationTests fixture body"
        )
    return match.group("body")


def _observe_function(source: str) -> str:
    match = re.search(
        r"UiaObservation\s+observeProcessorInventory\s*\(\s*HWND\s+windowHandle\s*\)\s*"
        r"\{(?P<body>.*?)\n\}\n\nUiaObservation\s+observeWhilePumpingMessages",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError("could not locate observeProcessorInventory body")
    return match.group("body")


def _pump_function(source: str) -> str:
    match = re.search(
        r"UiaObservation\s+observeWhilePumpingMessages\s*\(\s*HWND\s+windowHandle\s*\)\s*"
        r"\{(?P<body>.*?)\n\}\n\nclass\s+ProcessorInventoryWindowsUIAutomationTests",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError("could not locate observeWhilePumpingMessages body")
    return match.group("body")


class WindowsUiaHarnessStabilityPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not HARNESS.is_file():
            raise unittest.SkipTest(f"missing harness: {HARNESS}")
        if not CMAKE.is_file():
            raise unittest.SkipTest(f"missing cmake: {CMAKE}")
        cls.source = HARNESS.read_text(encoding="utf-8")
        cls.cmake = CMAKE.read_text(encoding="utf-8")
        cls.fixture = _fixture_body(cls.source)
        cls.observe = _observe_function(cls.source)
        cls.pump = _pump_function(cls.source)

    def test_three_semantic_cases_remain_registered(self):
        for name in EXPECTED_TEST_CASES:
            with self.subTest(case=name):
                self.assertRegex(
                    self.source,
                    rf"TEST_F\s*\(\s*ProcessorInventoryWindowsUIAutomationTests\s*,\s*{name}\s*\)",
                )

    def test_message_manager_and_uia_provider_are_process_stable_across_cases(self):
        """v1.1 449f96f intent: do not tear down MessageManager per TEST_F."""
        self.assertIn("MessageManager::getInstance()", self.fixture)
        # Per-test teardown of the shared provider is the instability root cause.
        self.assertNotIn(
            "MessageManager::deleteInstance()",
            self.fixture,
            "fixture must not delete MessageManager per test; keep process-stable UIA provider",
        )
        self.assertNotIn(
            "DeletedAtShutdown::deleteAll()",
            self.fixture,
            "fixture must not run DeletedAtShutdown::deleteAll per test",
        )
        # TearDown that recreates process-global JUCE state is forbidden for this suite.
        self.assertNotRegex(
            self.fixture,
            r"void\s+TearDown\s*\(\s*\)\s*override",
            "no per-test TearDown that destroys MessageManager/UIA provider",
        )

    def test_healthy_observation_does_not_unconditionally_crawl_all_descendants(self):
        """Diagnostic full-tree FindAll must not run on the healthy path."""
        # The expensive diagnostic that only built discoveredElements.
        self.assertNotIn(
            "TreeScope_Descendants",
            self.observe,
            "healthy observeProcessorInventory must not FindAll(TreeScope_Descendants)",
        )
        self.assertNotRegex(
            self.observe,
            r"FindAll\s*\(\s*TreeScope_Descendants\b",
            "no unconditional all-descendants diagnostic crawl before direct-child asserts",
        )
        # discoveredElements dump was only filled by that crawl; keep it out of healthy path.
        self.assertNotIn(
            "discoveredElements",
            self.observe,
            "do not build discoveredElements via full descendants crawl on healthy path",
        )

    def test_direct_child_inventory_semantics_and_soft_deadline_remain(self):
        # Healthy path queries direct semantic children only.
        self.assertIn("TreeScope_Children", self.observe)
        self.assertRegex(
            self.observe,
            r"FindAll\s*\(\s*TreeScope_Children\b",
        )
        self.assertIn("directProcessorAutomationIds", self.observe)
        self.assertIn("directProcessorItems", self.observe)
        self.assertIn("UIA_FullDescriptionPropertyId", self.observe)
        self.assertIn("get_CurrentIsOffscreen", self.observe)
        self.assertIn("UIA_InvokePatternId", self.observe)

        # Semantic assertions across the suite stay present (not weakened).
        for token in (
            "EXPECT_TRUE (observation.itemFound)",
            "EXPECT_TRUE (observation.directChildrenAreOnlyProcessorItems)",
            "EXPECT_FALSE (observation.invokePatternAvailable)",
            "EXPECT_EQ (observation.directChildCount, 16)",
            "Predecessor node ID:",
            "item.isOffscreen",
            "EXPECT_FALSE (item.invokePatternAvailable)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.source)

        # External COM MTA worker + message pumping + 3s soft deadline remain.
        self.assertIn("COINIT_MULTITHREADED", self.source)
        self.assertIn("std::thread worker", self.pump)
        self.assertIn("runDispatchLoopUntil", self.pump)
        self.assertRegex(
            self.pump,
            r"getMillisecondCounter\s*\(\s*\)\s*\+\s*3000",
        )
        self.assertRegex(
            self.pump,
            r'(ADD_FAILURE\s*\(\s*\)|EXPECT_TRUE\s*\(\s*finished)',
        )

        # CTest hard process timeout remains 30s (not lengthened as a fake fix).
        self.assertIn(
            "set_tests_properties(WindowsUIAutomation_tests PROPERTIES TIMEOUT 30)",
            self.cmake,
        )


if __name__ == "__main__":
    unittest.main()
