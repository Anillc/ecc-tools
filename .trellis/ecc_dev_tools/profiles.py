from __future__ import annotations

try:
    from .models import ExecutionPlan, PassPlanName, Profile, TidyMode, TidyPass, ValidationPreset
except ImportError:
    from models import ExecutionPlan, PassPlanName, Profile, TidyMode, TidyPass, ValidationPreset


DEEP_TIDY_CHECKS: tuple[str, ...] = (
    "bugprone-*",
    "-bugprone-easily-swappable-parameters",
    "-bugprone-macro-parentheses",
    "modernize-*",
    "-modernize-concat-nested-namespaces",
    "-modernize-deprecated-headers",
    "-modernize-pass-by-value",
    "-modernize-return-braced-init-list",
    "-modernize-use-designated-initializers",
    "-modernize-use-equals-default",
    "-modernize-use-nodiscard",
    "performance-*",
    "-performance-enum-size",
    "-performance-move-const-arg",
    "readability-*",
    "-readability-braces-around-statements",
    "-readability-const-return-type",
    "-readability-else-after-return",
    "-readability-function-cognitive-complexity",
    "-readability-identifier-length",
    "-readability-isolate-declaration",
    "-readability-magic-numbers",
    "-readability-math-missing-parentheses",
    "-readability-redundant-string-init",
    "-readability-redundant-typename",
    "-readability-use-anyofallof",
    "misc-*",
    "-misc-const-correctness",
    "-misc-include-cleaner",
    "-misc-non-private-member-variables-in-classes",
    "cppcoreguidelines-*",
    "-cppcoreguidelines-avoid-magic-numbers",
    "-cppcoreguidelines-macro-usage",
    "-cppcoreguidelines-non-private-member-variables-in-classes",
    "-cppcoreguidelines-pro-bounds-avoid-unchecked-container-access",
    "-cppcoreguidelines-pro-bounds-constant-array-index",
    "-cppcoreguidelines-special-member-functions",
    "-cppcoreguidelines-use-default-member-init",
    "readability-identifier-naming",
)

ANALYZER_ONLY_CHECKS = "-*,clang-analyzer-*"

VALIDATION_PRESETS: dict[str, ValidationPreset] = {
    "default": ValidationPreset(
        name="default",
        description="Run the default repository-local quality workflow.",
        kinds=("format", "tidy", "headers", "cmake", "iwyu"),
    ),
    "quality": ValidationPreset(
        name="quality",
        description="Run formatting and Clang-based code quality checks only.",
        kinds=("format", "tidy"),
    ),
    "structure": ValidationPreset(
        name="structure",
        description="Run header completeness and CMake dependency checks only.",
        kinds=("headers", "cmake"),
    ),
    "tidy-only": ValidationPreset(
        name="tidy-only",
        description="Run only the planned clang-tidy and syntax passes.",
        kinds=("tidy",),
    ),
    "iwyu-only": ValidationPreset(
        name="iwyu-only",
        description="Run only IWYU include analysis.",
        kinds=("iwyu",),
    ),
}

PROFILES: dict[str, Profile] = {
    "icts": Profile(
        name="icts",
        description="iCTS C++ module profile",
        scope_roots=("src/operation/iCTS",),
        target_prefixes=("icts_",),
        clang_tidy_config="src/utility/.clang-tidy",
        deep_tidy_checks=DEEP_TIDY_CHECKS,
        analyzer_checks=ANALYZER_ONLY_CHECKS,
        default_validation_preset="default",
        default_tidy_mode="deep",
        default_pass_plan="complete",
        build_target="iEDA",
        graph_root="src/operation/iCTS",
        include_inference_roots=(
            "src/operation/iCTS/source/database",
            "src/operation/iCTS/source/database/spatial",
            "src/operation/iCTS/source/database/config",
            "src/operation/iCTS/source/utils/geometry",
        ),
        source_extensions=(".cc",),
        header_extensions=(".hh",),
    ),
}

DEFAULT_PROFILE = "icts"
DEFAULT_VALIDATION_PRESET = "default"
DEFAULT_PASS_PLAN: PassPlanName = "complete"
DEFAULT_TIDY_MODE: TidyMode = "deep"


def get_profile(name: str) -> Profile:
    try:
        return PROFILES[name]
    except KeyError as exc:
        supported = ", ".join(sorted(PROFILES))
        raise ValueError(f"Unsupported profile: {name}. Supported profiles: {supported}") from exc


def get_validation_preset(name: str) -> ValidationPreset:
    try:
        return VALIDATION_PRESETS[name]
    except KeyError as exc:
        supported = ", ".join(sorted(VALIDATION_PRESETS))
        raise ValueError(f"Unsupported validation preset: {name}. Supported presets: {supported}") from exc


def available_validation_presets() -> list[ValidationPreset]:
    return [VALIDATION_PRESETS[name] for name in sorted(VALIDATION_PRESETS)]


def resolve_execution_plan(
    *,
    profile: Profile,
    validation_preset: str | None,
    tidy_mode: TidyMode | None,
    pass_plan: PassPlanName | None,
    kinds_override: tuple[str, ...] | None = None,
    legacy_deep_tidy: bool | None = None,
) -> ExecutionPlan:
    compatibility_notes: list[str] = []

    preset_name = validation_preset or profile.default_validation_preset
    preset = get_validation_preset(preset_name)

    resolved_tidy_mode = tidy_mode or profile.default_tidy_mode
    resolved_pass_plan = pass_plan or profile.default_pass_plan

    if legacy_deep_tidy is not None:
        compatibility_notes.append(
            "Legacy tidy compatibility flag was used; prefer --tidy-mode naming|deep for future runs."
        )
        if tidy_mode is None:
            resolved_tidy_mode = "deep" if legacy_deep_tidy else "naming"
        else:
            compatibility_notes.append(
                "Both legacy tidy flags and --tidy-mode were provided; honoring --tidy-mode."
            )

    kinds = preset.kinds
    if kinds_override is not None:
        kinds = tuple(kinds_override)
        compatibility_notes.append(
            "Legacy explicit --kinds override was used; prefer --preset for the primary workflow and only override kinds when necessary."
        )

    if "tidy" not in kinds:
        tidy_passes: tuple[TidyPass, ...] = ()
    else:
        tidy_passes = _resolve_tidy_passes(profile, resolved_tidy_mode, resolved_pass_plan)

    return ExecutionPlan(
        validation_preset=preset.name,
        kinds=kinds,
        tidy_mode=resolved_tidy_mode,
        pass_plan=resolved_pass_plan,
        tidy_passes=tidy_passes,
        compatibility_notes=tuple(compatibility_notes),
    )


def _resolve_tidy_passes(profile: Profile, tidy_mode: TidyMode, pass_plan: PassPlanName) -> tuple[TidyPass, ...]:
    tidy_checks = _tidy_checks_arg(profile, tidy_mode)
    passes: list[TidyPass] = [
        TidyPass(
            name="tidy-tu",
            description="clang-tidy translation-unit pass using compile_commands metadata",
            tool_name="clang-tidy",
            runner="clang-tidy-tu",
            checks_arg=tidy_checks,
            dedupe_priority=10,
        )
    ]

    if tidy_mode == "deep":
        passes.append(
            TidyPass(
                name="analyzer-tu",
                description="explicit clang static analyzer pass through clang-tidy",
                tool_name="clang-tidy",
                runner="clang-tidy-tu",
                checks_arg=profile.analyzer_checks,
                dedupe_priority=15,
            )
        )
        passes.append(
            TidyPass(
                name="tidy-headers",
                description="scope-local clang-tidy header pass using the same checks as translation units",
                tool_name="clang-tidy",
                runner="clang-tidy-header",
                checks_arg=tidy_checks,
                dedupe_priority=20,
            )
        )

    if tidy_mode == "naming":
        passes.append(
            TidyPass(
                name="tidy-headers",
                description="clang-tidy focused header naming pass for scope-local headers",
                tool_name="clang-tidy",
                runner="clang-tidy-header",
                checks_arg=tidy_checks,
                dedupe_priority=20,
            )
        )

    if pass_plan == "complete":
        passes.append(
            TidyPass(
                name="clang-frontend",
                description="explicit Clang frontend syntax-only pass using compile command flags",
                tool_name="clang++",
                runner="clang-frontend",
                dedupe_priority=30,
            )
        )
        passes.append(
            TidyPass(
                name="native-fallback",
                description="native compiler syntax-only fallback pass when prior passes stay quiet",
                tool_name="g++",
                runner="native-compiler",
                on_demand=True,
                dedupe_priority=40,
            )
        )
    elif pass_plan == "legacy":
        passes.append(
            TidyPass(
                name="native-fallback",
                description="native compiler syntax-only fallback pass when prior passes stay quiet",
                tool_name="g++",
                runner="native-compiler",
                on_demand=True,
                dedupe_priority=40,
            )
        )
    elif pass_plan == "tidy-only":
        pass
    else:
        raise ValueError(f"Unsupported pass plan: {pass_plan}")

    return tuple(passes)


def _tidy_checks_arg(profile: Profile, tidy_mode: TidyMode) -> str:
    if tidy_mode == "deep":
        return "-*," + ",".join(profile.deep_tidy_checks)
    if tidy_mode == "naming":
        return "-*,readability-identifier-naming"
    raise ValueError(f"Unsupported tidy mode: {tidy_mode}")
