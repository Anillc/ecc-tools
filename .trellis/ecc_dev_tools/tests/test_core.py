"""Unit tests for ecc_dev_tools core modules.

Tests cover pure functions and algorithmic logic across:
  - models.py    (Scope, Finding, ExecutionPlan, CMakeTarget)
  - profiles.py  (resolve_execution_plan, presets, passes)
  - checkers.py  (parallel_map, dedupe, cycles, parsing, command building)
  - reporting.py (JSON output, compiler-style output, summary)
  - scope.py     (build_scope)
  - utils.py     (parse_version_text, default_jobs, version_meets_minimum)
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

# ---------------------------------------------------------------------------
# Make the parent package importable without installing it.
# ---------------------------------------------------------------------------
_PACKAGE_ROOT = Path(__file__).resolve().parent.parent
if str(_PACKAGE_ROOT.parent) not in sys.path:
    sys.path.insert(0, str(_PACKAGE_ROOT.parent))

# ruff: noqa: E402
from ecc_dev_tools.models import (
    BuildContext,
    CMakeTarget,
    CheckResult,
    CompileCommand,
    EnvironmentSnapshot,
    ExecutionPlan,
    Finding,
    RuntimeEntry,
    Scope,
    TidyPass,
    ToolStatus,
)
from ecc_dev_tools.profiles import (
    PROFILES,
    VALIDATION_PRESETS,
    get_profile,
    get_validation_preset,
    resolve_execution_plan,
)
from ecc_dev_tools.checkers import (
    _build_header_include_first_command,
    _build_source_syntax_command,
    _classify_compiler_diagnostic,
    _classify_tidy_diagnostic,
    _dedupe_findings,
    _detect_cycles,
    _extract_diagnostic_checks,
    _has_indirect_path,
    _parallel_map,
    _parse_clang_tidy_output,
    _parse_iwyu_output,
    _scan_deps_for_file,
    _strip_diagnostic_suffix,
    run_header_dependency_check,
)
from ecc_dev_tools.reporting import (
    format_check_result,
    format_exit_summary,
    format_results_compiler_style,
    format_results_json,
)
from ecc_dev_tools.build_context import (
    _apply_trace_link_metadata,
    _load_declared_graph,
    _load_trace_link_metadata,
    _merge_link_scope,
)
from ecc_dev_tools.scope import build_scope
from ecc_dev_tools.environment import _binary_sort_key, _probe_tool
from ecc_dev_tools.utils import (
    dedupe_keep_order,
    default_jobs,
    parse_version_suffix,
    parse_version_text,
    version_meets_minimum,
)


# ===================================================================
# models.py tests
# ===================================================================


class TestScopeContains(unittest.TestCase):
    """Scope.contains() -- path-in-scope membership checks."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        (self.tmp / "src" / "module").mkdir(parents=True)
        (self.tmp / "src" / "module" / "file.cc").touch()
        (self.tmp / "other").mkdir()
        (self.tmp / "other" / "external.cc").touch()

    def test_file_inside_scope(self):
        scope = Scope(
            repo_root=self.tmp,
            raw_paths=["src/module"],
            resolved_paths=[(self.tmp / "src" / "module").resolve()],
        )
        self.assertTrue(scope.contains(self.tmp / "src" / "module" / "file.cc"))

    def test_file_outside_scope(self):
        scope = Scope(
            repo_root=self.tmp,
            raw_paths=["src/module"],
            resolved_paths=[(self.tmp / "src" / "module").resolve()],
        )
        self.assertFalse(scope.contains(self.tmp / "other" / "external.cc"))

    def test_directory_itself_is_in_scope(self):
        scope = Scope(
            repo_root=self.tmp,
            raw_paths=["src/module"],
            resolved_paths=[(self.tmp / "src" / "module").resolve()],
        )
        self.assertTrue(scope.contains(self.tmp / "src" / "module"))

    def test_parent_directory_is_not_in_scope(self):
        scope = Scope(
            repo_root=self.tmp,
            raw_paths=["src/module"],
            resolved_paths=[(self.tmp / "src" / "module").resolve()],
        )
        self.assertFalse(scope.contains(self.tmp / "src"))

    def test_multiple_scope_roots(self):
        (self.tmp / "lib").mkdir(exist_ok=True)
        (self.tmp / "lib" / "util.hh").touch()
        scope = Scope(
            repo_root=self.tmp,
            raw_paths=["src/module", "lib"],
            resolved_paths=[
                (self.tmp / "src" / "module").resolve(),
                (self.tmp / "lib").resolve(),
            ],
        )
        self.assertTrue(scope.contains(self.tmp / "src" / "module" / "file.cc"))
        self.assertTrue(scope.contains(self.tmp / "lib" / "util.hh"))
        self.assertFalse(scope.contains(self.tmp / "other" / "external.cc"))


class TestScopeRelativeItems(unittest.TestCase):
    """Scope.relative_items() -- returns paths relative to repo root."""

    def test_relative_items(self):
        tmp = Path(tempfile.mkdtemp())
        (tmp / "src" / "mod").mkdir(parents=True)
        scope = Scope(
            repo_root=tmp,
            raw_paths=["src/mod"],
            resolved_paths=[(tmp / "src" / "mod").resolve()],
        )
        items = scope.relative_items()
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0], "src/mod")


class TestFindingSortKey(unittest.TestCase):
    """Finding.sort_key() -- deterministic sort ordering."""

    def test_sorts_by_path_then_line(self):
        f1 = Finding(check="tidy", severity="warning", path=Path("/a/b.cc"), message="m1", line=10)
        f2 = Finding(check="tidy", severity="warning", path=Path("/a/b.cc"), message="m2", line=5)
        f3 = Finding(check="tidy", severity="error", path=Path("/a/a.cc"), message="m3", line=1)
        ordered = sorted([f1, f2, f3], key=Finding.sort_key)
        self.assertEqual(ordered[0].message, "m3")  # /a/a.cc comes first
        self.assertEqual(ordered[1].line, 5)          # line 5 before line 10
        self.assertEqual(ordered[2].line, 10)

    def test_none_path_sorts_first(self):
        f1 = Finding(check="cmake", severity="error", path=None, message="cycle")
        f2 = Finding(check="cmake", severity="error", path=Path("/z.cc"), message="other")
        ordered = sorted([f2, f1], key=Finding.sort_key)
        self.assertEqual(ordered[0].message, "cycle")  # empty string sorts before /z.cc

    def test_none_line_becomes_zero(self):
        f = Finding(check="tidy", severity="warning", path=Path("/a.cc"), message="m")
        self.assertEqual(f.sort_key()[1], 0)


class TestExecutionPlanMethods(unittest.TestCase):
    """ExecutionPlan.uses_deep_tidy(), requires_build_context(), has_kind()."""

    def _make_plan(self, kinds, tidy_mode="deep", pass_plan="complete"):
        return ExecutionPlan(
            validation_preset="default",
            kinds=tuple(kinds),
            tidy_mode=tidy_mode,
            pass_plan=pass_plan,
        )

    def test_uses_deep_tidy_true(self):
        plan = self._make_plan(["tidy"], tidy_mode="deep")
        self.assertTrue(plan.uses_deep_tidy())

    def test_uses_deep_tidy_false(self):
        plan = self._make_plan(["tidy"], tidy_mode="naming")
        self.assertFalse(plan.uses_deep_tidy())

    def test_requires_build_context_tidy(self):
        plan = self._make_plan(["tidy"])
        self.assertTrue(plan.requires_build_context())

    def test_requires_build_context_headers(self):
        plan = self._make_plan(["headers"])
        self.assertTrue(plan.requires_build_context())

    def test_requires_build_context_cmake(self):
        plan = self._make_plan(["cmake"])
        self.assertTrue(plan.requires_build_context())

    def test_requires_build_context_iwyu(self):
        plan = self._make_plan(["iwyu"])
        self.assertTrue(plan.requires_build_context())

    def test_requires_build_context_format_only(self):
        plan = self._make_plan(["format"])
        self.assertFalse(plan.requires_build_context())

    def test_has_kind_present(self):
        plan = self._make_plan(["format", "tidy"])
        self.assertTrue(plan.has_kind("format"))
        self.assertTrue(plan.has_kind("tidy"))

    def test_has_kind_absent(self):
        plan = self._make_plan(["format"])
        self.assertFalse(plan.has_kind("cmake"))


class TestCMakeTargetOwnsPath(unittest.TestCase):
    """CMakeTarget.owns_path() -- source matching."""

    def test_path_in_sources_list(self):
        target = CMakeTarget(name="lib", sources=[Path("/a/b.cc"), Path("/a/c.cc")])
        self.assertTrue(target.owns_path(Path("/a/b.cc")))

    def test_path_not_in_sources_and_no_source_dir(self):
        target = CMakeTarget(name="lib", sources=[Path("/a/b.cc")])
        self.assertFalse(target.owns_path(Path("/a/d.cc")))

    def test_path_under_source_dir(self):
        target = CMakeTarget(
            name="lib",
            source_dir=Path("/project/src/module"),
            sources=[],
        )
        self.assertTrue(target.owns_path(Path("/project/src/module/sub/file.cc")))

    def test_path_outside_source_dir(self):
        target = CMakeTarget(
            name="lib",
            source_dir=Path("/project/src/module"),
            sources=[],
        )
        self.assertFalse(target.owns_path(Path("/project/src/other/file.cc")))

    def test_path_equals_source_dir(self):
        target = CMakeTarget(
            name="lib",
            source_dir=Path("/project/src/module"),
            sources=[],
        )
        self.assertTrue(target.owns_path(Path("/project/src/module")))


class TestCheckResultFilters(unittest.TestCase):
    """CheckResult.in_scope_findings(), out_of_scope_findings(), triggered_in_scope_findings()."""

    def test_in_scope_findings(self):
        result = CheckResult(kind="tidy", findings=[
            Finding(check="tidy", severity="warning", path=Path("/a.cc"), message="m1", location_scope_class="in_scope"),
            Finding(check="tidy", severity="warning", path=Path("/b.cc"), message="m2", location_scope_class="out_of_scope"),
        ])
        self.assertEqual(len(result.in_scope_findings()), 1)
        self.assertEqual(result.in_scope_findings()[0].message, "m1")

    def test_out_of_scope_findings(self):
        result = CheckResult(kind="tidy", findings=[
            Finding(check="tidy", severity="warning", path=Path("/a.cc"), message="m1", location_scope_class="in_scope"),
            Finding(check="tidy", severity="warning", path=Path("/b.cc"), message="m2", location_scope_class="out_of_scope"),
        ])
        self.assertEqual(len(result.out_of_scope_findings()), 1)
        self.assertEqual(result.out_of_scope_findings()[0].message, "m2")

    def test_triggered_in_scope_findings(self):
        result = CheckResult(kind="tidy", findings=[
            Finding(check="tidy", severity="warning", path=Path("/a.cc"), message="m1", trigger_scope_class="in_scope"),
            Finding(check="tidy", severity="warning", path=Path("/b.cc"), message="m2", trigger_scope_class="out_of_scope"),
            Finding(check="tidy", severity="warning", path=Path("/c.cc"), message="m3", trigger_scope_class=None),
        ])
        self.assertEqual(len(result.triggered_in_scope_findings()), 1)
        self.assertEqual(result.triggered_in_scope_findings()[0].message, "m1")


# ===================================================================
# profiles.py tests
# ===================================================================


class TestGetProfile(unittest.TestCase):
    """get_profile() -- lookup and error handling."""

    def test_known_profile(self):
        profile = get_profile("icts")
        self.assertEqual(profile.name, "icts")

    def test_unknown_profile_raises(self):
        with self.assertRaises(ValueError) as ctx:
            get_profile("nonexistent")
        self.assertIn("Unsupported profile", str(ctx.exception))


class TestGetValidationPreset(unittest.TestCase):
    """get_validation_preset() -- lookup and error handling."""

    def test_known_preset(self):
        preset = get_validation_preset("default")
        self.assertEqual(preset.name, "default")

    def test_unknown_preset_raises(self):
        with self.assertRaises(ValueError) as ctx:
            get_validation_preset("fantasy")
        self.assertIn("Unsupported validation preset", str(ctx.exception))


class TestResolveExecutionPlanDefaults(unittest.TestCase):
    """resolve_execution_plan() -- default plan from icts profile."""

    def setUp(self):
        self.profile = get_profile("icts")

    def test_default_plan_kinds(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(set(plan.kinds), {"format", "tidy", "headers", "cmake", "iwyu"})

    def test_default_plan_tidy_mode(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(plan.tidy_mode, "deep")

    def test_default_plan_pass_plan(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(plan.pass_plan, "complete")

    def test_default_plan_has_tidy_passes(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertGreater(len(plan.tidy_passes), 0)
        pass_names = [p.name for p in plan.tidy_passes]
        self.assertIn("tidy-tu", pass_names)


class TestResolveExecutionPlanPresetOverride(unittest.TestCase):
    """resolve_execution_plan() -- preset overrides."""

    def setUp(self):
        self.profile = get_profile("icts")

    def test_quality_preset(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset="quality",
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(set(plan.kinds), {"format", "tidy"})
        self.assertFalse(plan.has_kind("cmake"))
        self.assertFalse(plan.has_kind("headers"))

    def test_structure_preset(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset="structure",
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(set(plan.kinds), {"headers", "cmake"})
        self.assertFalse(plan.has_kind("tidy"))

    def test_tidy_only_preset_has_no_format(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset="tidy-only",
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(set(plan.kinds), {"tidy"})

    def test_iwyu_only_preset(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset="iwyu-only",
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(set(plan.kinds), {"iwyu"})
        self.assertTrue(plan.has_kind("iwyu"))
        self.assertFalse(plan.has_kind("format"))
        self.assertFalse(plan.has_kind("tidy"))

    def test_structure_preset_no_tidy_passes(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset="structure",
            tidy_mode=None,
            pass_plan=None,
        )
        self.assertEqual(len(plan.tidy_passes), 0)


class TestResolveExecutionPlanTidyModeOverride(unittest.TestCase):
    """resolve_execution_plan() -- tidy mode overrides."""

    def setUp(self):
        self.profile = get_profile("icts")

    def test_naming_mode(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode="naming",
            pass_plan=None,
        )
        self.assertEqual(plan.tidy_mode, "naming")
        self.assertFalse(plan.uses_deep_tidy())

    def test_deep_mode_has_analyzer_pass(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode="deep",
            pass_plan="complete",
        )
        pass_names = [p.name for p in plan.tidy_passes]
        self.assertIn("analyzer-tu", pass_names)

    def test_naming_mode_no_analyzer_pass(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode="naming",
            pass_plan="complete",
        )
        pass_names = [p.name for p in plan.tidy_passes]
        self.assertNotIn("analyzer-tu", pass_names)


class TestResolveExecutionPlanPassPlanOverride(unittest.TestCase):
    """resolve_execution_plan() -- pass plan overrides."""

    def setUp(self):
        self.profile = get_profile("icts")

    def test_complete_plan_has_clang_frontend(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan="complete",
        )
        pass_names = [p.name for p in plan.tidy_passes]
        self.assertIn("clang-frontend", pass_names)

    def test_legacy_plan_no_clang_frontend(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan="legacy",
        )
        pass_names = [p.name for p in plan.tidy_passes]
        self.assertNotIn("clang-frontend", pass_names)

    def test_tidy_only_plan_no_native_fallback(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan="tidy-only",
        )
        pass_names = [p.name for p in plan.tidy_passes]
        self.assertNotIn("native-fallback", pass_names)
        self.assertNotIn("clang-frontend", pass_names)

    def test_legacy_plan_has_native_fallback(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan="legacy",
        )
        pass_names = [p.name for p in plan.tidy_passes]
        self.assertIn("native-fallback", pass_names)


class TestResolveExecutionPlanLegacyDeepTidy(unittest.TestCase):
    """resolve_execution_plan() -- legacy_deep_tidy compatibility flag."""

    def setUp(self):
        self.profile = get_profile("icts")

    def test_legacy_deep_tidy_true_sets_deep(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
            legacy_deep_tidy=True,
        )
        self.assertEqual(plan.tidy_mode, "deep")
        self.assertGreater(len(plan.compatibility_notes), 0)

    def test_legacy_deep_tidy_false_sets_naming(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
            legacy_deep_tidy=False,
        )
        self.assertEqual(plan.tidy_mode, "naming")

    def test_legacy_flag_overridden_by_explicit_tidy_mode(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode="naming",
            pass_plan=None,
            legacy_deep_tidy=True,
        )
        # Explicit --tidy-mode wins
        self.assertEqual(plan.tidy_mode, "naming")
        # Should have two compatibility notes
        self.assertGreaterEqual(len(plan.compatibility_notes), 2)


class TestResolveExecutionPlanKindsOverride(unittest.TestCase):
    """resolve_execution_plan() -- kinds_override parameter."""

    def setUp(self):
        self.profile = get_profile("icts")

    def test_kinds_override_replaces_preset(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset="default",
            tidy_mode=None,
            pass_plan=None,
            kinds_override=("format",),
        )
        self.assertEqual(plan.kinds, ("format",))
        self.assertGreater(len(plan.compatibility_notes), 0)

    def test_kinds_override_no_tidy_means_no_passes(self):
        plan = resolve_execution_plan(
            profile=self.profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
            kinds_override=("format", "cmake"),
        )
        self.assertEqual(len(plan.tidy_passes), 0)


# ===================================================================
# checkers.py tests
# ===================================================================


class TestParallelMap(unittest.TestCase):
    """_parallel_map() -- sequential, parallel, and error cases."""

    def test_sequential_single_job(self):
        results = _parallel_map(lambda x: x * 2, [1, 2, 3], jobs=1)
        self.assertEqual(sorted(results), [2, 4, 6])

    def test_parallel_multiple_jobs(self):
        results = _parallel_map(lambda x: x * 2, [1, 2, 3], jobs=2)
        self.assertEqual(sorted(results), [2, 4, 6])

    def test_empty_items(self):
        results = _parallel_map(lambda x: x, [], jobs=4)
        self.assertEqual(results, [])

    def test_single_item_any_jobs(self):
        results = _parallel_map(lambda x: x + 10, [5], jobs=4)
        self.assertEqual(results, [15])

    def test_error_is_returned_as_exception(self):
        def failing(_x):
            raise ValueError("boom")

        results = _parallel_map(failing, [1, 2], jobs=2)
        for result in results:
            self.assertIsInstance(result, Exception)

    def test_sequential_error_propagates(self):
        """In sequential mode errors are not caught -- they propagate."""
        def failing(_x):
            raise ValueError("boom")

        with self.assertRaises(ValueError):
            _parallel_map(failing, [1], jobs=1)


class TestDedupeFindings(unittest.TestCase):
    """_dedupe_findings() -- deduplication with different priorities."""

    def _make_finding(self, path, line, message, origin="tidy-tu", category="bugprone", subtype="sub"):
        return Finding(
            check="tidy",
            severity="warning",
            path=Path(path),
            line=line,
            message=message,
            origin=origin,
            category=category,
            subtype=subtype,
        )

    def test_no_duplicates_kept_intact(self):
        findings = [
            self._make_finding("/a.cc", 1, "msg1"),
            self._make_finding("/a.cc", 2, "msg2"),
        ]
        result = _dedupe_findings(findings)
        self.assertEqual(len(result), 2)

    def test_exact_duplicates_collapsed(self):
        f1 = self._make_finding("/a.cc", 10, "same msg", origin="tidy-tu")
        f2 = self._make_finding("/a.cc", 10, "same msg", origin="tidy-tu")
        result = _dedupe_findings([f1, f2])
        self.assertEqual(len(result), 1)

    def test_higher_priority_wins(self):
        passes = (
            TidyPass(name="tidy-tu", description="", tool_name="clang-tidy", runner="clang-tidy-tu", dedupe_priority=10),
            TidyPass(name="clang-frontend", description="", tool_name="clang++", runner="clang-frontend", dedupe_priority=30),
        )
        f_low = self._make_finding("/a.cc", 10, "msg", origin="tidy-tu")
        f_high = self._make_finding("/a.cc", 10, "msg", origin="clang-frontend")
        result = _dedupe_findings([f_low, f_high], passes)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].origin, "clang-frontend")

    def test_lower_priority_does_not_replace(self):
        passes = (
            TidyPass(name="tidy-tu", description="", tool_name="clang-tidy", runner="clang-tidy-tu", dedupe_priority=10),
            TidyPass(name="analyzer-tu", description="", tool_name="clang-tidy", runner="clang-tidy-tu", dedupe_priority=15),
        )
        f_high = self._make_finding("/a.cc", 10, "msg", origin="analyzer-tu")
        f_low = self._make_finding("/a.cc", 10, "msg", origin="tidy-tu")
        result = _dedupe_findings([f_high, f_low], passes)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].origin, "analyzer-tu")

    def test_different_messages_not_deduped(self):
        f1 = self._make_finding("/a.cc", 10, "msg1")
        f2 = self._make_finding("/a.cc", 10, "msg2")
        result = _dedupe_findings([f1, f2])
        self.assertEqual(len(result), 2)


class TestDetectCycles(unittest.TestCase):
    """_detect_cycles() -- cycle detection on known graphs."""

    def test_no_cycles(self):
        adjacency = {"a": ["b"], "b": ["c"], "c": []}
        cycles = _detect_cycles(adjacency)
        self.assertEqual(len(cycles), 0)

    def test_simple_self_cycle(self):
        adjacency = {"a": ["a"]}
        cycles = _detect_cycles(adjacency)
        self.assertEqual(len(cycles), 1)
        self.assertIn("a", cycles[0])

    def test_two_node_cycle(self):
        adjacency = {"a": ["b"], "b": ["a"]}
        cycles = _detect_cycles(adjacency)
        self.assertGreater(len(cycles), 0)
        # At least one cycle should contain both a and b
        all_nodes = set()
        for cycle in cycles:
            all_nodes.update(cycle)
        self.assertIn("a", all_nodes)
        self.assertIn("b", all_nodes)

    def test_three_node_cycle(self):
        adjacency = {"a": ["b"], "b": ["c"], "c": ["a"]}
        cycles = _detect_cycles(adjacency)
        self.assertGreater(len(cycles), 0)

    def test_disconnected_graph_no_cycles(self):
        adjacency = {"a": ["b"], "b": [], "c": ["d"], "d": []}
        cycles = _detect_cycles(adjacency)
        self.assertEqual(len(cycles), 0)

    def test_diamond_no_cycle(self):
        adjacency = {"a": ["b", "c"], "b": ["d"], "c": ["d"], "d": []}
        cycles = _detect_cycles(adjacency)
        self.assertEqual(len(cycles), 0)


class TestHasIndirectPath(unittest.TestCase):
    """_has_indirect_path() -- indirect path detection."""

    def test_no_indirect_path(self):
        adjacency = {"a": ["b"], "b": []}
        self.assertFalse(_has_indirect_path("a", "b", adjacency))

    def test_simple_indirect_path(self):
        # a -> b, a -> c -> b
        adjacency = {"a": ["b", "c"], "b": [], "c": ["b"]}
        self.assertTrue(_has_indirect_path("a", "b", adjacency))

    def test_longer_indirect_path(self):
        # a -> b, a -> c -> d -> b
        adjacency = {"a": ["b", "c"], "b": [], "c": ["d"], "d": ["b"]}
        self.assertTrue(_has_indirect_path("a", "b", adjacency))

    def test_no_path_through_other_deps(self):
        # a -> b, a -> c but c does not reach b
        adjacency = {"a": ["b", "c"], "b": [], "c": ["d"], "d": []}
        self.assertFalse(_has_indirect_path("a", "b", adjacency))

    def test_non_existent_source(self):
        adjacency = {"a": ["b"], "b": []}
        self.assertFalse(_has_indirect_path("z", "b", adjacency))


class TestParseClangTidyOutput(unittest.TestCase):
    """_parse_clang_tidy_output() -- parsing of typical clang-tidy output lines."""

    def _make_scope_and_command(self, tmp_dir):
        src = tmp_dir / "src"
        src.mkdir(exist_ok=True)
        cc = src / "File.cc"
        cc.touch()
        scope = Scope(
            repo_root=tmp_dir,
            raw_paths=["src"],
            resolved_paths=[src.resolve()],
        )
        command = CompileCommand(
            file=cc.resolve(),
            directory=tmp_dir,
            command="g++ -c src/File.cc",
        )
        return scope, command

    def test_parses_warning_line(self):
        tmp = Path(tempfile.mkdtemp())
        scope, command = self._make_scope_and_command(tmp)
        text = f"{command.file}:10:5: warning: some issue [bugprone-use-after-move]"
        result = _parse_clang_tidy_output(text, scope, command, origin="tidy-tu")
        self.assertEqual(len(result.findings), 1)
        self.assertEqual(result.findings[0].severity, "warning")
        self.assertEqual(result.findings[0].line, 10)
        self.assertIn("some issue", result.findings[0].message)

    def test_parses_error_line(self):
        tmp = Path(tempfile.mkdtemp())
        scope, command = self._make_scope_and_command(tmp)
        text = f"{command.file}:20:1: error: undeclared identifier [clang-diagnostic-error]"
        result = _parse_clang_tidy_output(text, scope, command, origin="tidy-tu")
        self.assertEqual(len(result.findings), 1)
        self.assertEqual(result.findings[0].severity, "error")

    def test_skips_note_lines(self):
        tmp = Path(tempfile.mkdtemp())
        scope, command = self._make_scope_and_command(tmp)
        text = f"{command.file}:10:5: note: this is a note"
        result = _parse_clang_tidy_output(text, scope, command, origin="tidy-tu")
        self.assertEqual(len(result.findings), 0)

    def test_empty_input_returns_empty(self):
        tmp = Path(tempfile.mkdtemp())
        scope, command = self._make_scope_and_command(tmp)
        result = _parse_clang_tidy_output("", scope, command, origin="tidy-tu")
        self.assertEqual(len(result.findings), 0)
        self.assertEqual(result.parsed_count, 0)

    def test_scope_classification(self):
        tmp = Path(tempfile.mkdtemp())
        scope, command = self._make_scope_and_command(tmp)
        text = f"{command.file}:10:5: warning: in-scope issue [readability-identifier-naming]"
        result = _parse_clang_tidy_output(text, scope, command, origin="tidy-tu")
        self.assertEqual(result.findings[0].location_scope_class, "in_scope")

    def test_out_of_scope_path(self):
        tmp = Path(tempfile.mkdtemp())
        scope, command = self._make_scope_and_command(tmp)
        (tmp / "external").mkdir(exist_ok=True)
        ext_file = tmp / "external" / "Lib.hh"
        ext_file.touch()
        text = f"{ext_file.resolve()}:5:1: warning: external issue [misc-unused]"
        result = _parse_clang_tidy_output(text, scope, command, origin="tidy-tu")
        self.assertEqual(len(result.findings), 1)
        self.assertEqual(result.findings[0].location_scope_class, "out_of_scope")

    def test_suppression_note_generated(self):
        tmp = Path(tempfile.mkdtemp())
        scope, command = self._make_scope_and_command(tmp)
        text = "42 warnings generated.\nSuppressed 40 warnings (38 in non-user code, 2 NOLINT)."
        result = _parse_clang_tidy_output(text, scope, command, origin="tidy-tu")
        self.assertIsNotNone(result.suppression_note)


class TestExtractDiagnosticChecks(unittest.TestCase):
    """_extract_diagnostic_checks() -- bracket extraction."""

    def test_single_bracket(self):
        checks = _extract_diagnostic_checks("some message [bugprone-use-after-move]")
        self.assertEqual(checks, ["bugprone-use-after-move"])

    def test_nested_brackets(self):
        checks = _extract_diagnostic_checks("msg [bugprone-use-after-move] [readability-identifier-naming]")
        self.assertIn("readability-identifier-naming", checks)
        self.assertIn("bugprone-use-after-move", checks)

    def test_no_brackets(self):
        checks = _extract_diagnostic_checks("plain message with no checks")
        self.assertEqual(checks, [])

    def test_leading_dash_stripped(self):
        checks = _extract_diagnostic_checks("msg [-Wunused-variable]")
        self.assertEqual(checks, ["Wunused-variable"])


class TestStripDiagnosticSuffix(unittest.TestCase):
    """_strip_diagnostic_suffix() -- remove bracket suffixes."""

    def test_strips_single_suffix(self):
        result = _strip_diagnostic_suffix("some message [bugprone-foo]")
        self.assertEqual(result, "some message")

    def test_strips_multiple_suffixes(self):
        result = _strip_diagnostic_suffix("msg [a] [b]")
        self.assertEqual(result, "msg")

    def test_no_suffix_unchanged(self):
        result = _strip_diagnostic_suffix("plain message")
        self.assertEqual(result, "plain message")


class TestClassifyTidyDiagnostic(unittest.TestCase):
    """_classify_tidy_diagnostic() -- category classification."""

    def test_bugprone_check(self):
        category, subtype, _family = _classify_tidy_diagnostic(["bugprone-use-after-move"])
        self.assertEqual(category, "bugprone")
        self.assertEqual(subtype, "bugprone-use-after-move")

    def test_clang_diagnostic_check(self):
        category, subtype, family = _classify_tidy_diagnostic(["clang-diagnostic-error"])
        self.assertEqual(category, "clang-diagnostic")
        self.assertEqual(subtype, "clang-diagnostic-error")
        self.assertEqual(family, "compiler-diagnostic")

    def test_readability_identifier_naming(self):
        # "readability-identifier-naming" starts with "readability-" prefix,
        # so it matches the "readability" group first in iteration order.
        category, subtype, _family = _classify_tidy_diagnostic(["readability-identifier-naming"])
        self.assertEqual(category, "readability")
        self.assertEqual(subtype, "readability-identifier-naming")

    def test_empty_checks(self):
        category, subtype, _family = _classify_tidy_diagnostic([])
        self.assertEqual(category, "clang-tidy")
        self.assertEqual(subtype, "unclassified")

    def test_unknown_check_uses_primary(self):
        category, subtype, _family = _classify_tidy_diagnostic(["some-unknown-check"])
        self.assertEqual(category, "clang-tidy")
        self.assertEqual(subtype, "some-unknown-check")


class TestClassifyCompilerDiagnostic(unittest.TestCase):
    """_classify_compiler_diagnostic() -- gcc/clang diagnostic classification."""

    def test_clang_diagnostic_error(self):
        category, subtype = _classify_compiler_diagnostic("msg", ["clang-diagnostic-error"])
        self.assertEqual(category, "clang-diagnostic")
        self.assertEqual(subtype, "clang-diagnostic-error")

    def test_plain_clang_diagnostic(self):
        category, subtype = _classify_compiler_diagnostic("msg", ["clang-diagnostic"])
        self.assertEqual(category, "clang-diagnostic")
        self.assertEqual(subtype, "compiler-diagnostic")

    def test_no_checks_falls_back(self):
        category, subtype = _classify_compiler_diagnostic("msg", [])
        self.assertEqual(category, "clang-diagnostic")
        self.assertEqual(subtype, "compiler-syntax-only")



class TestBuildSourceSyntaxCommand(unittest.TestCase):
    """_build_source_syntax_command() -- flag stripping (-c, -o, -MF, etc.)."""

    def test_strips_c_flag(self):
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -std=c++20 -I/inc /src/File.cc",
        )
        result = _build_source_syntax_command(command)
        self.assertNotIn("-c", result)
        self.assertIn("-fsyntax-only", result)
        self.assertIn(str(command.file), result)

    def test_strips_output_flag(self):
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -o /build/File.o -std=c++20 /src/File.cc",
        )
        result = _build_source_syntax_command(command)
        self.assertNotIn("-o", result)
        self.assertNotIn("/build/File.o", result)

    def test_strips_dependency_flags(self):
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -MD -MF /build/File.d -std=c++20 /src/File.cc",
        )
        result = _build_source_syntax_command(command)
        self.assertNotIn("-MD", result)
        self.assertNotIn("-MF", result)
        self.assertNotIn("/build/File.d", result)

    def test_compiler_override(self):
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -std=c++20 /src/File.cc",
        )
        result = _build_source_syntax_command(command, compiler_override="clang++")
        self.assertEqual(result[0], "clang++")

    def test_preserves_include_flags(self):
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -I/include -std=c++20 /src/File.cc",
        )
        result = _build_source_syntax_command(command)
        self.assertIn("-I/include", result)
        self.assertIn("-std=c++20", result)

    def test_strips_source_file_from_original(self):
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -std=c++20 /src/File.cc",
        )
        result = _build_source_syntax_command(command)
        # The original source path should be removed from middle of command
        # and re-added at the end after -fsyntax-only
        last_two = result[-2:]
        self.assertEqual(last_two, ["-fsyntax-only", str(command.file)])

    def test_adds_extra_flags_once(self):
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -Wconversion -std=c++20 /src/File.cc",
        )
        result = _build_source_syntax_command(command, extra_flags=("-Wall", "-Wextra", "-Wconversion", "-Wsign-conversion"))
        self.assertEqual(result.count("-Wconversion"), 1)
        self.assertIn("-Wall", result)
        self.assertIn("-Wextra", result)
        self.assertIn("-Wsign-conversion", result)


class TestProbeTool(unittest.TestCase):
    """_probe_tool() -- preserves multiline version output so version parsing can see later lines."""

    @patch("ecc_dev_tools.environment.shutil.which", return_value="/usr/bin/clang-tidy")
    @patch("ecc_dev_tools.environment.run_command")
    def test_keeps_multiline_version_output(self, run_command_mock, _which_mock):
        run_command_mock.return_value = type(
            "Result",
            (),
            {
                "returncode": 0,
                "stdout": "LLVM (http://llvm.org/):\n  LLVM version 22.1.2\n  Optimized build with assertions.\n",
                "stderr": "",
            },
        )()
        requirement = type("Requirement", (), {"name": "clang-tidy", "required": True, "min_version": None})()

        status = _probe_tool(Path("/tmp"), requirement)

        self.assertEqual(parse_version_text(status.version_text or ""), (22, 1, 2))
        self.assertIn("LLVM version 22.1.2", status.version_text or "")


class TestBinarySortKey(unittest.TestCase):
    """_binary_sort_key() -- prefer explicit versioned binary suffix over reported output."""

    def test_prefers_higher_suffix_even_if_reported_version_is_major_only(self):
        older = ToolStatus(
            name="clang-tidy",
            required=True,
            found=True,
            executable="/usr/bin/clang-tidy-18",
            version_text="Debian LLVM version 18",
            ok=True,
            selected_candidate="clang-tidy-18",
        )
        newer = ToolStatus(
            name="clang-tidy",
            required=True,
            found=True,
            executable="/usr/bin/clang-tidy-22",
            version_text="Debian LLVM version 22",
            ok=True,
            selected_candidate="clang-tidy-22",
        )
        self.assertGreater(_binary_sort_key(newer, "clang-tidy"), _binary_sort_key(older, "clang-tidy"))


    def test_prefers_higher_reported_version_when_unversioned_binary_is_newer(self):
        direct = ToolStatus(
            name="clang-format",
            required=True,
            found=True,
            executable="/usr/bin/clang-format",
            version_text="clang-format version 22.1.2",
            ok=True,
            selected_candidate="clang-format",
        )
        versioned = ToolStatus(
            name="clang-format",
            required=True,
            found=True,
            executable="/usr/bin/clang-format-18",
            version_text="Debian clang-format version 18.1.8",
            ok=True,
            selected_candidate="clang-format-18",
        )
        self.assertGreater(_binary_sort_key(direct, "clang-format"), _binary_sort_key(versioned, "clang-format"))


class TestScanDepsForFile(unittest.TestCase):
    """_scan_deps_for_file() -- uses resolved clang++ binary rather than hard-coded clang++."""

    @patch("ecc_dev_tools.checkers.run_command")
    def test_uses_selected_compiler_binary(self, run_command_mock):
        run_command_mock.return_value = type("Result", (), {"returncode": 0, "stdout": "", "stderr": ""})()
        command = CompileCommand(
            file=Path("/src/File.cc"),
            directory=Path("/build"),
            command="g++ -c -std=c++20 /src/File.cc",
        )

        _scan_deps_for_file(command, "/usr/bin/clang-scan-deps", "/opt/llvm/bin/clang++")

        args = run_command_mock.call_args.args[0]
        self.assertEqual(args[0], "/usr/bin/clang-scan-deps")
        self.assertIn("--", args)
        self.assertEqual(args[2], "/opt/llvm/bin/clang++")


class TestHeaderDependencyCheckFallback(unittest.TestCase):
    """run_header_dependency_check() -- degrades cleanly when clang++ is unavailable."""

    @patch("ecc_dev_tools.checkers._scan_deps_batch")
    def test_falls_back_to_regex_when_clangxx_missing_from_snapshot(self, scan_deps_mock):
        tmp = Path(tempfile.mkdtemp())
        src_dir = tmp / "src"
        src_dir.mkdir()
        source = src_dir / "File.cc"
        source.write_text('#include "File.hh"\n', encoding="utf-8")
        header = src_dir / "File.hh"
        header.write_text("#pragma once\n", encoding="utf-8")

        scope = Scope(
            repo_root=tmp,
            raw_paths=["src"],
            resolved_paths=[src_dir.resolve()],
        )
        source_path = source.resolve()
        src_dir_path = src_dir.resolve()
        context = BuildContext(
            build_dir=tmp / "build",
            compile_commands_path=tmp / "build" / "compile_commands.json",
            file_api_reply_dir=tmp / "build" / ".cmake" / "api" / "v1" / "reply",
            compile_commands=[
                CompileCommand(
                    file=source_path,
                    directory=tmp,
                    command=f"g++ -I{src_dir_path} -c {source_path}",
                )
            ],
            targets={
                "icts_source_module_topology_linear_clustering": CMakeTarget(
                    name="icts_source_module_topology_linear_clustering",
                    source_dir=src_dir_path,
                    sources=[source_path],
                    include_dirs=[src_dir_path],
                )
            },
            declared_graph={"icts_source_module_topology_linear_clustering": []},
            cmake_text_graph={},
            cmake_public_graph={},
            profile_name="icts",
        )
        snapshot = EnvironmentSnapshot(
            repo_root=tmp,
            build_dir=tmp / "build",
            jobs=1,
            total_cpus=1,
            idle_threads_estimate=1,
            tool_statuses=[
                ToolStatus(name="g++", required=True, found=True, executable="/usr/bin/g++", ok=True),
                ToolStatus(
                    name="clang-scan-deps",
                    required=False,
                    found=True,
                    executable="/usr/bin/clang-scan-deps",
                    ok=True,
                ),
            ],
        )

        result = run_header_dependency_check(tmp, scope, context, get_profile("icts"), snapshot, cpp_files=[source_path])

        scan_deps_mock.assert_not_called()
        self.assertTrue(
            any("falling back to regex-based include scanning" in note for note in result.notes),
            result.notes,
        )


class TestParseIwyuOutput(unittest.TestCase):
    """_parse_iwyu_output() -- parsing of IWYU stderr output."""

    def _make_scope(self, tmp_dir):
        src = tmp_dir / "src"
        src.mkdir(exist_ok=True)
        return Scope(
            repo_root=tmp_dir,
            raw_paths=["src"],
            resolved_paths=[src.resolve()],
        )

    def test_parses_missing_include(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        header = tmp / "src" / "Foo.hh"
        header.touch()
        source = tmp / "src" / "Foo.cc"
        source.touch()
        output = f"{header.resolve()} should add these lines:\n#include \"Bar.hh\"\n\n---\n"
        findings = _parse_iwyu_output(output, source.resolve(), scope)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].subtype, "missing-include")
        self.assertIn("#include \"Bar.hh\"", findings[0].message)
        self.assertEqual(findings[0].location_scope_class, "in_scope")

    def test_parses_angle_include_as_missing_include(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        header = tmp / "src" / "Foo.hh"
        header.touch()
        source = tmp / "src" / "Foo.cc"
        source.touch()
        output = f"{header.resolve()} should add these lines:\n#include <string>\n\n---\n"
        findings = _parse_iwyu_output(output, source.resolve(), scope)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].subtype, "missing-include")

    def test_parses_unnecessary_include(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        header = tmp / "src" / "Bar.hh"
        header.touch()
        source = tmp / "src" / "Bar.cc"
        source.touch()
        output = f'{header.resolve()} should remove these lines:\n- #include "Unused.hh"  // lines 3-3\n\n---\n'
        findings = _parse_iwyu_output(output, source.resolve(), scope)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].subtype, "unnecessary-include")
        self.assertIn("Unused.hh", findings[0].message)

    def test_parses_missing_forward_decl(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        header = tmp / "src" / "Baz.hh"
        header.touch()
        source = tmp / "src" / "Baz.cc"
        source.touch()
        output = f"{header.resolve()} should add these lines:\nnamespace icts {{ class Config; }}\n\n---\n"
        findings = _parse_iwyu_output(output, source.resolve(), scope)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].subtype, "missing-forward-decl")
        self.assertIn("class Config", findings[0].message)

    def test_out_of_scope_file(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        (tmp / "external").mkdir(exist_ok=True)
        ext_header = tmp / "external" / "Ext.hh"
        ext_header.touch()
        source = tmp / "src" / "Main.cc"
        source.touch()
        output = f"{ext_header.resolve()} should add these lines:\n#include <vector>\n\n---\n"
        findings = _parse_iwyu_output(output, source.resolve(), scope)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].location_scope_class, "out_of_scope")
        self.assertEqual(findings[0].trigger_scope_class, "in_scope")

    def test_empty_output(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        source = tmp / "src" / "Empty.cc"
        source.touch()
        findings = _parse_iwyu_output("", source.resolve(), scope)
        self.assertEqual(len(findings), 0)

    def test_full_include_list_section_ignored(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        header = tmp / "src" / "Full.hh"
        header.touch()
        source = tmp / "src" / "Full.cc"
        source.touch()
        output = (
            f"{header.resolve()} should add these lines:\n"
            f"#include <map>\n\n"
            f"The full include-list for {header.resolve()}:\n"
            f"#include <map>\n"
            f"#include <string>\n"
            f"---\n"
        )
        findings = _parse_iwyu_output(output, source.resolve(), scope)
        # Should only get the "should add" finding, not the full include-list
        self.assertEqual(len(findings), 1)
        self.assertIn("#include <map>", findings[0].message)

    def test_mixed_add_and_remove(self):
        tmp = Path(tempfile.mkdtemp())
        scope = self._make_scope(tmp)
        header = tmp / "src" / "Mix.hh"
        header.touch()
        source = tmp / "src" / "Mix.cc"
        source.touch()
        output = (
            f"{header.resolve()} should add these lines:\n"
            f"#include <string>\n"
            f"namespace foo {{ struct Bar; }}\n\n"
            f"{header.resolve()} should remove these lines:\n"
            f'- #include "Old.hh"  // lines 2-2\n\n'
            f"---\n"
        )
        findings = _parse_iwyu_output(output, source.resolve(), scope)
        subtypes = [f.subtype for f in findings]
        self.assertIn("missing-include", subtypes)
        self.assertIn("missing-forward-decl", subtypes)
        self.assertIn("unnecessary-include", subtypes)
        self.assertEqual(len(findings), 3)


# ===================================================================
# reporting.py tests
# ===================================================================


class TestFormatResultsJson(unittest.TestCase):
    """format_results_json() -- JSON output structure."""

    def test_basic_structure(self):
        profile = get_profile("icts")
        plan = resolve_execution_plan(
            profile=profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        results = [
            CheckResult(kind="format", findings=[
                Finding(
                    check="format",
                    severity="warning",
                    path=Path("/a.cc"),
                    message="needs reformat",
                    category="format",
                    subtype="needs-reformat",
                    location_scope_class="in_scope",
                ),
            ]),
        ]
        json_str = format_results_json(results, plan, profile)
        data = json.loads(json_str)
        self.assertEqual(data["profile"], "icts")
        self.assertEqual(data["summary"]["in_scope"], 1)
        self.assertEqual(data["summary"]["total"], 1)
        self.assertEqual(len(data["findings"]), 1)
        self.assertEqual(data["findings"][0]["check"], "format")

    def test_empty_results(self):
        profile = get_profile("icts")
        plan = resolve_execution_plan(
            profile=profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        json_str = format_results_json([], plan, profile)
        data = json.loads(json_str)
        self.assertEqual(data["summary"]["total"], 0)
        self.assertEqual(len(data["findings"]), 0)

    def test_scope_field_present(self):
        profile = get_profile("icts")
        plan = resolve_execution_plan(
            profile=profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        results = [
            CheckResult(kind="tidy", findings=[
                Finding(
                    check="tidy",
                    severity="warning",
                    path=Path("/a.cc"),
                    message="msg",
                    location_scope_class="out_of_scope",
                ),
            ]),
        ]
        json_str = format_results_json(results, plan, profile)
        data = json.loads(json_str)
        self.assertEqual(data["findings"][0]["scope"], "out_of_scope")
        self.assertEqual(data["summary"]["out_of_scope"], 1)

    def test_notes_are_included_by_check_kind(self):
        profile = get_profile("icts")
        plan = resolve_execution_plan(
            profile=profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        results = [
            CheckResult(kind="cmake", notes=[
                "Analyzed 15 targets under profile target prefixes.",
                "Skipped 2 generator-expression link items while reconstructing direct link scopes from CMake trace.",
            ]),
            CheckResult(kind="iwyu", notes=["Analyzing 25 translation units with IWYU."]),
        ]
        json_str = format_results_json(results, plan, profile)
        data = json.loads(json_str)
        self.assertEqual(
            data["notes"]["cmake"],
            [
                "Analyzed 15 targets under profile target prefixes.",
                "Skipped 2 generator-expression link items while reconstructing direct link scopes from CMake trace.",
            ],
        )
        self.assertEqual(data["notes"]["iwyu"], ["Analyzing 25 translation units with IWYU."])

    def test_runtime_section_is_included(self):
        profile = get_profile("icts")
        plan = resolve_execution_plan(
            profile=profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        results = [
            CheckResult(
                kind="tidy",
                runtime_seconds=12.5,
                runtime_entries=[
                    RuntimeEntry(label="tidy-tu", seconds=7.25, category="phase", count=10),
                    RuntimeEntry(label="foo.cc", seconds=1.5, category="unit"),
                ],
            ),
        ]
        json_str = format_results_json(results, plan, profile)
        data = json.loads(json_str)
        self.assertEqual(data["summary"]["runtime_seconds_total"], 12.5)
        self.assertEqual(data["runtime"]["tidy"]["seconds"], 12.5)
        self.assertEqual(len(data["runtime"]["tidy"]["entries"]), 2)
        self.assertEqual(data["runtime"]["tidy"]["entries"][0]["label"], "tidy-tu")


class TestFormatCheckResult(unittest.TestCase):
    """format_check_result() -- text output structure."""

    def test_runtime_is_rendered_in_text_output(self):
        result = CheckResult(
            kind="tidy",
            runtime_seconds=8.25,
            runtime_entries=[
                RuntimeEntry(label="analyzer-tu", seconds=6.0, category="phase", count=4),
                RuntimeEntry(label="src/foo.cc", seconds=2.2, category="unit"),
            ],
        )
        rendered = format_check_result(result, Path("/repo"))
        self.assertIn("- Runtime: 8.250s", rendered)
        self.assertIn("- Runtime breakdown:", rendered)
        self.assertIn("analyzer-tu: 6.000s, count=4", rendered)
        self.assertIn("- Runtime details:", rendered)
        self.assertIn("src/foo.cc: 2.200s", rendered)

    def test_runtime_fields_are_serialized(self):
        profile = get_profile("icts")
        plan = resolve_execution_plan(
            profile=profile,
            validation_preset=None,
            tidy_mode=None,
            pass_plan=None,
        )
        results = [
            CheckResult(
                kind="tidy",
                runtime_seconds=12.5,
                runtime_entries=[
                    RuntimeEntry(label="tidy-tu", seconds=7.0, category="phase", count=12),
                    RuntimeEntry(label="src/foo.cc", seconds=1.2, category="unit"),
                ],
            ),
        ]
        json_str = format_results_json(results, plan, profile)
        data = json.loads(json_str)
        self.assertEqual(data["summary"]["runtime_seconds_total"], 12.5)
        self.assertEqual(data["runtime"]["tidy"]["seconds"], 12.5)
        self.assertEqual(data["runtime"]["tidy"]["entries"][0]["label"], "tidy-tu")
        self.assertEqual(data["runtime"]["tidy"]["entries"][1]["category"], "unit")


class TestFormatCheckResultRuntime(unittest.TestCase):
    """format_check_result() -- runtime information formatting."""

    def test_runtime_sections_are_rendered(self):
        result = CheckResult(
            kind="tidy",
            runtime_seconds=5.25,
            runtime_entries=[
                RuntimeEntry(label="analyzer-tu", seconds=3.5, category="phase", count=8),
                RuntimeEntry(label="src/a.cc", seconds=1.1, category="unit"),
            ],
        )
        rendered = format_check_result(result, Path("/repo"))
        self.assertIn("Runtime: 5.250s", rendered)
        self.assertIn("Runtime breakdown:", rendered)
        self.assertIn("analyzer-tu: 3.500s, count=8", rendered)
        self.assertIn("Runtime details:", rendered)
        self.assertIn("src/a.cc: 1.100s", rendered)


class TestHeaderIncludeFirstHelpers(unittest.TestCase):
    """Header include-first and direct-include helper behavior."""

    def test_build_header_include_first_command_creates_wrapper(self):
        tmp = Path(tempfile.mkdtemp())
        header = tmp / "Foo.hh"
        header.write_text("#pragma once\n", encoding="utf-8")
        command, cwd, wrapper = _build_header_include_first_command(
            header=header,
            include_dirs=[str(tmp)],
            trigger_command=None,
            compiler="g++",
            repo_root=tmp,
        )
        try:
            self.assertEqual(command[0], "g++")
            self.assertEqual(cwd, tmp)
            self.assertTrue(wrapper.exists())
            self.assertEqual(command[-1], str(wrapper))
        finally:
            wrapper.unlink(missing_ok=True)


class TestTraceLinkMetadata(unittest.TestCase):
    """Trace-based link metadata loading and graph derivation."""

    def test_load_trace_link_metadata(self):
        tmp = Path(tempfile.mkdtemp())
        repo_root = tmp / "repo"
        cmake_dir = repo_root / "src" / "operation" / "iCTS"
        cmake_dir.mkdir(parents=True)
        trace = tmp / "cmake_trace.json"
        trace.write_text(
            "\n".join(
                [
                    json.dumps({"version": {"major": 1, "minor": 2}}),
                    json.dumps(
                        {
                            "cmd": "target_link_libraries",
                            "file": str(cmake_dir / "CMakeLists.txt"),
                            "args": ["foo", "PUBLIC", "bar", "PRIVATE", "baz"],
                        }
                    ),
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        loaded, skipped = _load_trace_link_metadata(trace, repo_root)
        self.assertEqual(skipped, 0)
        self.assertEqual(loaded["foo"][0]["scope"], "public")
        self.assertEqual(loaded["foo"][1]["scope"], "private")

    def test_apply_trace_link_metadata_and_graphs(self):
        foo = CMakeTarget(name="foo")
        bar = CMakeTarget(name="bar")
        baz = CMakeTarget(name="baz")
        targets = {"foo": foo, "bar": bar, "baz": baz}
        _apply_trace_link_metadata(
            targets,
            {
                "foo": [
                    {"name": "bar", "scope": "public"},
                    {"name": "baz", "scope": "private"},
                ]
            },
        )
        self.assertEqual(foo.declared_links, ["bar", "baz"])
        self.assertEqual(foo.declared_link_scopes["bar"], "public")
        merged_graph, cmake_text_graph, cmake_public_graph = _load_declared_graph(targets)
        self.assertEqual(merged_graph["foo"], ["bar", "baz"])
        self.assertEqual(cmake_text_graph["foo"], ["bar", "baz"])
        self.assertEqual(cmake_public_graph["foo"], ["bar"])

    def test_merge_link_scope_promotes_private_and_interface_to_public(self):
        self.assertEqual(_merge_link_scope(None, "private"), "private")
        self.assertEqual(_merge_link_scope("private", "interface"), "public")
        self.assertEqual(_merge_link_scope("public", "private"), "public")

    def test_load_trace_link_metadata_uses_public_as_default_scope(self):
        tmp = Path(tempfile.mkdtemp())
        repo_root = tmp / "repo"
        cmake_dir = repo_root / "src" / "operation" / "iCTS"
        cmake_dir.mkdir(parents=True)
        trace = tmp / "cmake_trace.json"
        trace.write_text(
            "\n".join(
                [
                    json.dumps({"version": {"major": 1, "minor": 2}}),
                    json.dumps(
                        {
                            "cmd": "target_link_libraries",
                            "file": str(cmake_dir / "CMakeLists.txt"),
                            "args": ["foo", "bar"],
                        }
                    ),
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        loaded, skipped = _load_trace_link_metadata(trace, repo_root)
        self.assertEqual(skipped, 0)
        self.assertEqual(loaded["foo"][0]["scope"], "public")

    def test_load_trace_link_metadata_counts_generator_expressions(self):
        tmp = Path(tempfile.mkdtemp())
        repo_root = tmp / "repo"
        cmake_dir = repo_root / "src" / "operation" / "iCTS"
        cmake_dir.mkdir(parents=True)
        trace = tmp / "cmake_trace.json"
        trace.write_text(
            "\n".join(
                [
                    json.dumps({"version": {"major": 1, "minor": 2}}),
                    json.dumps(
                        {
                            "cmd": "target_link_libraries",
                            "file": str(cmake_dir / "CMakeLists.txt"),
                            "args": ["foo", "PRIVATE", "$<IF:1,bar,baz>", "bar"],
                        }
                    ),
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        loaded, skipped = _load_trace_link_metadata(trace, repo_root)
        self.assertEqual(skipped, 1)
        self.assertEqual(loaded["foo"][0]["name"], "bar")


class TestFormatResultsCompilerStyle(unittest.TestCase):
    """format_results_compiler_style() -- compiler-style output format."""

    def test_in_scope_findings_only(self):
        results = [
            CheckResult(kind="tidy", findings=[
                Finding(
                    check="tidy",
                    severity="warning",
                    path=Path("/a.cc"),
                    line=10,
                    message="msg",
                    category="bugprone",
                    subtype="use-after-move",
                    location_scope_class="in_scope",
                ),
                Finding(
                    check="tidy",
                    severity="error",
                    path=Path("/b.cc"),
                    line=20,
                    message="external",
                    category="misc",
                    subtype="unused",
                    location_scope_class="out_of_scope",
                ),
            ]),
        ]
        output = format_results_compiler_style(results)
        self.assertIn("/a.cc:10", output)
        self.assertNotIn("/b.cc", output)

    def test_empty_findings(self):
        results = [CheckResult(kind="format")]
        output = format_results_compiler_style(results)
        self.assertEqual(output, "")


class TestFormatExitSummary(unittest.TestCase):
    """format_exit_summary() -- summary counts."""

    def test_counts_are_correct(self):
        results = [
            CheckResult(kind="tidy", runtime_seconds=4.5, findings=[
                Finding(check="tidy", severity="warning", path=Path("/a.cc"), message="m1", location_scope_class="in_scope", trigger_scope_class="in_scope"),
                Finding(check="tidy", severity="warning", path=Path("/b.cc"), message="m2", location_scope_class="out_of_scope", trigger_scope_class="out_of_scope"),
                Finding(check="tidy", severity="warning", path=Path("/c.cc"), message="m3", location_scope_class="in_scope", trigger_scope_class=None),
            ]),
        ]
        summary = format_exit_summary(results)
        self.assertIn("In-scope findings: 2", summary)
        self.assertIn("Out-of-scope findings: 1", summary)
        self.assertIn("Triggered by in-scope translation units: 1", summary)
        self.assertIn("Total runtime: 4.500s", summary)

    def test_fail_on_findings_true(self):
        summary = format_exit_summary([], fail_on_findings=True)
        self.assertIn("Exit code is 1 when in-scope findings exist", summary)

    def test_fail_on_findings_false(self):
        summary = format_exit_summary([], fail_on_findings=False)
        self.assertIn("Exit code is forced to 0", summary)


# ===================================================================
# scope.py tests
# ===================================================================


class TestBuildScope(unittest.TestCase):
    """build_scope() -- valid paths, non-existent paths, paths outside repo."""

    def test_valid_directory(self):
        tmp = Path(tempfile.mkdtemp())
        (tmp / "src").mkdir()
        scope = build_scope(tmp, ["src"])
        self.assertEqual(len(scope.resolved_paths), 1)
        self.assertEqual(scope.resolved_paths[0], (tmp / "src").resolve())

    def test_valid_file(self):
        tmp = Path(tempfile.mkdtemp())
        (tmp / "file.cc").touch()
        scope = build_scope(tmp, ["file.cc"])
        self.assertEqual(len(scope.resolved_paths), 1)

    def test_nonexistent_path_raises(self):
        tmp = Path(tempfile.mkdtemp())
        with self.assertRaises(ValueError) as ctx:
            build_scope(tmp, ["does_not_exist"])
        self.assertIn("does not exist", str(ctx.exception))

    def test_empty_paths_raises(self):
        tmp = Path(tempfile.mkdtemp())
        with self.assertRaises(ValueError) as ctx:
            build_scope(tmp, [])
        self.assertIn("At least one", str(ctx.exception))

    def test_path_outside_repo_raises(self):
        tmp = Path(tempfile.mkdtemp())
        other = Path(tempfile.mkdtemp())
        with self.assertRaises(ValueError) as ctx:
            build_scope(tmp, [str(other)])
        self.assertIn("inside the repository", str(ctx.exception))

    def test_multiple_paths(self):
        tmp = Path(tempfile.mkdtemp())
        (tmp / "src").mkdir()
        (tmp / "lib").mkdir()
        scope = build_scope(tmp, ["src", "lib"])
        self.assertEqual(len(scope.resolved_paths), 2)


# ===================================================================
# utils.py tests
# ===================================================================


class TestParseVersionText(unittest.TestCase):
    """parse_version_text() -- version extraction from typical tool outputs."""

    def test_clang_format_version(self):
        text = "clang-format version 17.0.6 (Fedora 17.0.6-2.fc39)"
        result = parse_version_text(text)
        self.assertEqual(result, (17, 0, 6))

    def test_clang_tidy_version(self):
        text = "LLVM version 15.0.7"
        result = parse_version_text(text)
        self.assertEqual(result, (15, 0, 7))

    def test_cmake_version(self):
        text = "cmake version 3.28.1\n\nCMake suite maintained..."
        result = parse_version_text(text)
        self.assertEqual(result, (3, 28, 1))

    def test_no_version_found(self):
        text = "no version info here"
        result = parse_version_text(text)
        self.assertEqual(result, ())

    def test_major_only_version(self):
        text = "Debian LLVM version 18"
        result = parse_version_text(text)
        self.assertEqual(result, (18,))

    def test_gcc_version(self):
        text = "g++ (Ubuntu 10.5.0-1ubuntu1~22.04) 10.5.0"
        result = parse_version_text(text)
        self.assertEqual(result, (10, 5, 0))

    def test_two_component_version(self):
        text = "version 3.11"
        result = parse_version_text(text)
        self.assertEqual(result, (3, 11))


class TestParseVersionSuffix(unittest.TestCase):
    """parse_version_suffix() -- version extraction from versioned binary names."""

    def test_extracts_major_suffix(self):
        self.assertEqual(parse_version_suffix("clang-tidy-18", "clang-tidy"), (18,))

    def test_extracts_multi_part_suffix(self):
        self.assertEqual(parse_version_suffix("clang-tidy-18.1", "clang-tidy"), (18, 1))

    def test_non_matching_name_returns_empty(self):
        self.assertEqual(parse_version_suffix("clang-tidy", "clang-tidy"), ())


class TestVersionMeetsMinimum(unittest.TestCase):
    """version_meets_minimum() -- comparison logic."""

    def test_equal_versions(self):
        self.assertTrue(version_meets_minimum((10, 0, 0), (10, 0, 0)))

    def test_actual_higher(self):
        self.assertTrue(version_meets_minimum((11, 0, 0), (10, 0, 0)))

    def test_actual_lower(self):
        self.assertFalse(version_meets_minimum((9, 0, 0), (10, 0, 0)))

    def test_empty_actual(self):
        self.assertFalse(version_meets_minimum((), (10, 0, 0)))

    def test_different_lengths(self):
        self.assertTrue(version_meets_minimum((10, 1), (10, 0, 0)))

    def test_minor_version_matters(self):
        self.assertFalse(version_meets_minimum((10, 0, 0), (10, 1, 0)))


class TestDefaultJobs(unittest.TestCase):
    """default_jobs() -- job count calculation."""

    def test_minimum_is_one(self):
        self.assertGreaterEqual(default_jobs(1, 1), 1)

    def test_half_idle_threads(self):
        result = default_jobs(16, 8)
        self.assertEqual(result, 4)  # 8 // 2 = 4

    def test_capped_at_total_cpus(self):
        result = default_jobs(2, 100)
        self.assertEqual(result, 2)  # min(2, 50) = 2

    def test_zero_idle_returns_one(self):
        result = default_jobs(8, 0)
        self.assertEqual(result, 1)  # idle_threads // 2 = 0, fallback to 1

    def test_one_idle_thread(self):
        result = default_jobs(8, 1)
        self.assertEqual(result, 1)  # 1 // 2 = 0, or 1 = 1


class TestDedupeKeepOrder(unittest.TestCase):
    """dedupe_keep_order() -- ordered deduplication."""

    def test_no_duplicates(self):
        result = dedupe_keep_order(["a", "b", "c"])
        self.assertEqual(result, ["a", "b", "c"])

    def test_removes_duplicates_keeps_first(self):
        result = dedupe_keep_order(["a", "b", "a", "c", "b"])
        self.assertEqual(result, ["a", "b", "c"])

    def test_empty_input(self):
        result = dedupe_keep_order([])
        self.assertEqual(result, [])

    def test_all_same(self):
        result = dedupe_keep_order(["x", "x", "x"])
        self.assertEqual(result, ["x"])


# ===================================================================
# Validation presets consistency tests
# ===================================================================


class TestValidationPresetsConsistency(unittest.TestCase):
    """Verify all presets have valid kinds."""

    def test_all_preset_kinds_are_valid(self):
        valid_kinds = {"format", "tidy", "headers", "cmake", "iwyu"}
        for name, preset in VALIDATION_PRESETS.items():
            for kind in preset.kinds:
                self.assertIn(kind, valid_kinds, f"Preset '{name}' has invalid kind '{kind}'")

    def test_default_preset_has_all_kinds(self):
        preset = VALIDATION_PRESETS["default"]
        self.assertEqual(set(preset.kinds), {"format", "tidy", "headers", "cmake", "iwyu"})

    def test_has_kind_method(self):
        preset = VALIDATION_PRESETS["quality"]
        self.assertTrue(preset.has_kind("format"))
        self.assertTrue(preset.has_kind("tidy"))
        self.assertFalse(preset.has_kind("cmake"))


# ===================================================================
# Profile consistency tests
# ===================================================================


class TestProfileConsistency(unittest.TestCase):
    """Verify icts profile has expected structure."""

    def test_icts_profile_scope_roots(self):
        profile = PROFILES["icts"]
        self.assertIn("src/operation/iCTS", profile.scope_roots)

    def test_icts_profile_target_prefixes(self):
        profile = PROFILES["icts"]
        self.assertIn("icts_", profile.target_prefixes)

    def test_icts_profile_extensions(self):
        profile = PROFILES["icts"]
        self.assertEqual(profile.source_extensions, (".cc",))
        self.assertEqual(profile.header_extensions, (".hh",))

    def test_icts_profile_default_preset(self):
        profile = PROFILES["icts"]
        self.assertEqual(profile.default_validation_preset, "default")


if __name__ == "__main__":
    unittest.main()
