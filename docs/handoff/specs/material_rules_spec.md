# Specification: Data-Driven Material Rules Refactor (`MaterialRules.json`)

## 1. Goal
Make the Uiana importer data-driven. Shader-to-master-material mappings, parameter aliases, slot overrides, and related material rules must come from an external `MaterialRules.json` instead of hardcoded C++ name checks, so supporting a new map means adding a row to the JSON with no C++ recompile.
- Start from `BPFL.cpp` (around lines 88–128 / slot resolution) and the material creation code in `UianaImporter.cpp` (around lines 804 / 860 and 1345 / 1400), finding every hardcoded check directly from source since line numbers drift.

---

## 2. Ground Rules
1. **Branching**: Work on a new git branch of the Uiana repo and commit in small, atomic steps.
2. **Content Protection**: Do not touch the main project's `Content/`.
3. **Renderer Invariant**: Do not change renderer settings (`DX12`, `SM5`, `Deferred Shading` locked).
4. **Gates**: Stop at each **GATE** specified below and report before continuing.
5. **Truthfulness**: Label anything unverified explicitly as `unverified`.

---

## 3. Phased Implementation Plan

### Phase 0: Baseline (Before Changing Code)
- **Map Capability Confirmation**: Confirm which maps the importer can currently handle. If Bind has never been extracted or imported, report that before continuing.
- **Rule Inventory**: Inventory every hardcoded rule:
  - Name checks (`Contains`, `StartsWith`, `==`)
  - Reparenting heuristics
  - Slot-name fixups (such as DCC placeholder names: `lambert`, `phong`, `MaterialSlot_`, `WorldGridMaterial`)
  - Per-mesh special cases (such as the `MidFloor_1` fallback)
  - Parameter-name mappings and aliases
  - Static switch handling
  - Blend-mode and opaque enforcement (glass, windows)
  - HISM mixed-slot guard
  - Foliage remap patterns and exclusions
  - Sky and vista handling
  *Output a table*: `file:line`, condition, action, maps affected, classification.
- **Rule Classification**:
  - `DATA`: Expressible as a clean declarative rule in JSON.
  - `CODE-WITH-FLAG`: The algorithm stays in C++ but the JSON switches or parameterizes it (such as `bEnableHismMixedSlotGuard`).
  - `CODE-ONLY`: Complex algorithmic code that should not be forced into JSON (explain why).
- **Regression Baseline Capture**:
  - Import Ascent and Bind headlessly using the existing script calling `BPFL.import_valorant_map`.
  - Dump the results to JSON stored outside `Content/` (e.g. `scratch/baseline_dumps/`):
    - For every material instance: asset path, parent material path, scalar parameters, vector parameters, texture parameters, static-switch parameters, and base property overrides (blend mode, two-sided).
    - For every placed mesh and HISM component: slot-to-material assignment.
    - Counts and all import log warnings.

> [!IMPORTANT]
> **GATE 0**: Show the inventory table and proposed schema to the user before writing the loader.

---

### Phase 1: Schema and Structs
- **C++ Reflection Structs**:
  - Define `USTRUCT`s: `FUianaParentRule`, `FUianaSlotOverride`, `FUianaSecondaryParameterRoute`, `FUianaRuleFlags`, and root `FUianaMaterialRules` with `SchemaVersion`.
- **Draft `MaterialRules.json`**:
  - Populate with every `DATA` rule identified in Phase 0 for Ascent and Bind.
  - *Requirements*:
    - Explicit match types (`Exact`, `Prefix`, `Contains`, `Regex`).
    - Case sensitivity toggle (`bCaseSensitive`).
    - Explicit integer priority (`Priority`, lower number = evaluated first).
    - Documented match semantics (first match wins vs all matches).
    - Master material soft object paths (e.g. `/Uiana/Materials/BaseEnv_MAT_V4.BaseEnv_MAT_V4`).
    - Parameter aliases (source name to canonical target parameter).
    - Slot overrides by mesh name and slot index.
    - Optional static switch defaults and blend mode overrides.
    - Enable flags for `CODE-WITH-FLAG` behaviors (`Flags` block).
    - Optional per-map section if map-specific overrides are needed.
- **Documentation**:
  - Write `SCHEMA.md` in repository root documenting every field, type, default, and match behavior.

---

### Phase 2: Loader and Resolver
- **Configuration Manager (`FUianaConfigManager`)**:
  - Locate plugin base directory via `IPluginManager::Get().FindPlugin(TEXT("Uiana"))`.
  - Load `Config/MaterialRules.json` using `FFileHelper` and `FJsonObjectConverter`.
  - Ensure `Json` and `JsonUtilities` modules are present in `Uiana.Build.cs`.
  - Validate rules on load.
  - **Reload Policy**: Reload the file at the start of *every* import (not just at module startup), enabling live JSON tuning without restarting the editor or recompiling C++.
- **Strict Validation & Fail-Fast Policy**:
  - Verify `SchemaVersion`.
  - Check for unknown or misspelled keys.
  - Detect duplicate or conflicting rule names / priorities.
  - Verify that referenced master materials under `/Uiana/Materials/` exist on disk via `StaticLoadObject`.
  - **Fatal Abort**: Malformed JSON, missing file, duplicate rules, or nonexistent master material targets must each emit a clear error naming the rule index/name and **abort the import immediately**. Never continue silently with partial or broken rules.
- **Telemetry & Logging**:
  - Log every resolution decision at `Verbose` (`LogUianaConfig`: input name, matched rule, assigned target).
  - Dump per-rule match counts at the end of each import (`DumpMatchStatistics`) to identify active vs dead rules.

---

### Phase 3: Replace Hardcoded Checks
- **C++ Refactoring**:
  - In `UianaImporter.cpp` and `BPFL.cpp`, replace hardcoded string checks and parent/slot mappings with calls to `FUianaConfigManager::Get()`.
  - Keep behavior 100% identical to the pre-refactor baseline.
  - Retain legacy logic behind a temporary compile-time flag until verification passes, then clean it up.

> [!IMPORTANT]
> **GATE 3**: The build must succeed using UBT with `-maxproc=1`. Present the diff summary to the user before testing.

---

### Phase 4: Verification
- **Baseline Diff Check**:
  - Re-import Ascent and Bind headlessly.
  - Diff against the Phase 0 baseline dumps.
  - Zero unexplained differences allowed; list and justify any intentional fixes.
- **Interactive Viewport Render Check**:
  - Open each imported map in the real editor (`UnrealEditor.exe`, normal launch, no special commandlet flags).
  - Render for 2 minutes plus a camera sweep across key areas.
  - Report any assertion, missing material, flat white texture, or default grid material.
- **No-Recompile Proof**:
  - Without rebuilding binaries or restarting the editor, edit `MaterialRules.json` on disk (e.g. add a mock rule mapping a test shader name to a different master material and adding a parameter alias).
  - Re-import and prove the change took effect via log output and material properties.
  - Show binary timestamps of `UnrealEditor-Uiana.dll` proving nothing was recompiled.
- **Negative Tests**:
  - Test missing `MaterialRules.json` file $\rightarrow$ verify clean error dialog and immediate abort.
  - Test malformed JSON syntax $\rightarrow$ verify parse error dialog and abort.
  - Test unknown key $\rightarrow$ verify validation failure and abort.
  - Test nonexistent master material target $\rightarrow$ verify asset verification failure and abort.
  - Test conflicting rule names / priorities $\rightarrow$ verify validation failure and abort.

---

## 4. Deliverables
1. Git branch with clean, atomic commits.
2. `Config/MaterialRules.json` populated with all Ascent & Bind rules.
3. `SCHEMA.md` documenting every JSON field and type.
4. Hardcoded rule inventory table from Phase 0.
5. Before/after diff report showing deleted C++ heuristics.
6. A concise "Adding a New Map" guide explaining exactly which rows to add to `MaterialRules.json`.
7. Comprehensive list of everything that could not be verified.
