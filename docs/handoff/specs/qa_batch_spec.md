# Specification: Headless Batch QA Agent for Multi-Map Texture, Material & UV Validation

## 1. Goal
Build and run a headless batch QA agent that imports every Valorant map the importer supports, reports texture, material, UV, and sky problems per map, and fixes parameter-alias and extractor gaps through **data only**.

---

## 2. Prerequisites & Scope Boundaries
- **Prerequisite**: The data-driven `MaterialRules.json` importer refactor must be completed.
- **Data-Only Fixes**: Fixes made by this QA agent are strictly restricted to editing `MaterialRules.json` (new parameter aliases, rule rows) and the extractor file lists.
- **C++ Boundary**: If a defect requires a C++ code modification, **stop and report it immediately** instead of altering code.

---

## 3. Safety & Isolation Protocol
1. **Isolated Scratch Copy**:
   - Work exclusively in a scratch copy of the project (excluding `Saved/`, `Intermediate/`, and `DerivedDataCache/`), never directly in the main project.
   - Check and report free disk space estimates upfront, as each imported map generates 1,000+ materials and textures.
2. **Prevent Cross-Map Contamination**:
   - The importer writes assets into shared folders (`/Game/ValorantContent/...`), which causes overlapping maps to overwrite or cross-contaminate assets.
   - Import each map into a clean copy or fully reset scratch `Content/` between map imports. Prove that zero cross-map contamination occurred.
3. **Process Isolation & Crash Resilience**:
   - Launch a fresh editor process for each map.
   - If an editor process crashes: preserve the `Saved/Crashes/` dump folder, mark the map status as `CRASH`, and proceed to the next map.
   - **Never abort the entire batch due to a single map failure**.
   - Enforce an explicit per-map execution timeout and record all timeout occurrences.
4. **Renderer Settings Invariant**:
   - Maintain the locked profile: `DX12`, `SM5 only`, `Deferred Shading`.
   - For any rendered checks, prove effective CVars from `ValoPreaim.log` ("LogConfig: Set CVar [[...]]").

---

## 4. Execution Workflow

### Step 1: Map Enumeration
- Retrieve the map list dynamically using the importer's discovery mechanism (`UianaDataSettings.get_map_names()` or equivalent filesystem scanner).
- Verify that the Valorant PAK path and `cue4extractor.exe` operate correctly for each target map.
- List any map that cannot be extracted.

### Step 2: Headless Per-Map Import
- Run headless import per map.
- Capture the complete log and parse for:
  - `LogUObjectGlobals: Warning: Failed to find object 'MaterialInterface ...'` (record total and unique missing parent material counts).
  - CoreRedirects errors.
  - Texture import failures.
- Record the contents of `_assets_textures_missing.txt` before and after the extraction pass.

### Step 3: Material & Texture Audit
Perform an exhaustive inspection over every `UMaterialInstanceConstant` asset generated for the map:
1. **Missing or Defaulted Textures**:
   - Query all texture parameters defined on the master material (retrieved from master definitions and aliases in `MaterialRules.json`).
   - Flag unset textures, engine defaults, project placeholder textures (`T_EV_BlankWhite_01`), or textures that fail to load.
   - Group findings by semantic role: `diffuse`, `normal`, `mask`, `emissive`, and `other`.
2. **JSON-to-Material Binding Parity**:
   - Compare the exported per-material JSON (parameter name $\rightarrow$ texture asset path) with the parameters bound on the runtime material instance.
   - Flag every texture referenced in JSON that was not bound in Unreal.
   - Flag every parameter name in JSON that matched no alias or master parameter.
3. **Flat Texture Content Detection (Heuristic)**:
   - Sample pixel statistics for diffuse, normal, and mask textures:
     - Flat diffuse (especially pure white $R=G=B=1.0$).
     - Flat normal map (close to tangent space default: $R \approx 128, G \approx 128, B \approx 255$).
     - Constant mask (zero variance across channels).
   - Document explicit mathematical thresholds used for each check and classify as **HEURISTIC**.
4. **Texture Asset Settings Validation**:
   - Normal maps must have normal map compression (`TC_Normalmap`) and sRGB disabled (`bSRGB = false`).
   - Mask and data textures must have linear gamma (`bSRGB = false`).
   - Flag any compression or color space mismatches.
5. **Parent Material Hierarchy & Fallbacks**:
   - Flag any material instance resolving to the engine default material (`DefaultMaterial`) or having a broken parent hierarchy chain.

### Step 4: Mesh & UV Channel Audit
Inspect every placed static mesh and LOD resource:
1. **Lightmap UV Integrity**:
   - For every LOD, confirm that `LightMapCoordinateIndex < NumUVChannels`.
   - Verify that the lightmap UV does not simply duplicate a tiling UV0.
   - Flag UV coordinates outside $[0, 1]$ or heavy overlapping clusters as **HEURISTIC**.
2. **Mesh UV Channel Availability**:
   - Flag materials that reference UV coordinate channels that do not exist on the Static Mesh.
   - State clearly which checks are **EXACT** vs **HEURISTIC**.

### Step 5: Sky Dome & Vista Verification
- Verify that sky and vista meshes, material instances, and textures exist and are properly bound.
- Launch the real editor (`UnrealEditor.exe`, not `-Cmd`) to capture automated screenshots from fixed camera angles covering both the sky dome and horizon.
- Evaluate pixel statistics to flag blank, pure black, flat, or blown-out white views.
- Use Ascent screenshots as the known-good reference; explicitly note that other maps lack visual baselines.

### Step 6: Reporting & Telemetry
Generate the following deliverables per map:
1. **Markdown Report**:
   - Detailed breakdown of all flagged assets, reasons, and classifications (**EXACT** vs **HEURISTIC**).
2. **CSV Export**:
   - Structured asset table: `AssetPath`, `AssetType`, `IssueCategory`, `ParameterName`, `CurrentValue`, `ExpectedValue`, `CheckType`.
3. **Cross-Map Summary Table**:
   - High-level overview:
     - Map Name
     - Total Materials OK
     - Missing Diffuse Count (with example flagged assets)
     - Missing Normal Count
     - Missing Mask Count
     - Unbound from JSON Count
     - Flat Normal Count
     - Texture Settings Mismatch Count
     - Lightmap UV Error Count
     - Sky / Vista Status
     - Parent Fallback Count
     - Log Warning Count
     - Process Status (`PASS`, `CRASH`, `TIMEOUT`)
   *Report concrete integer counts, not just percentages.*

### Step 7: Data-Driven Fix Loop
For each map with detected gaps:
1. Resolve findings strictly by editing `MaterialRules.json` (adding parameter aliases, slot overrides, or parent mapping rows) and the extractor file lists.
2. Re-import the map and rerun the audit (maximum **3 iterations** per map).
3. **Strict Constraints**:
   - Never fix a finding by assigning a default or placeholder texture.
   - Never suppress or delete findings from reports.
4. **Changelog Maintenance**:
   - Maintain a ledger: rule modified, target map, finding addressed, counts before and after.
5. **Regression Verification**:
   - If a fix is ambiguous, touches C++, or alters a rule that affects an already verified map (such as Ascent), **stop and request user guidance**.
   - After any rule update, rerun Ascent and Bind to confirm zero regression against their baseline dumps.

---

## 5. Deliverables
1. Individual markdown and CSV audit reports per map.
2. Iterative fix changelog.
3. Updated `MaterialRules.json` incorporating all validated map rules.
4. List of unresolved findings with hypotheses tagged explicitly as `hypothesis`.
