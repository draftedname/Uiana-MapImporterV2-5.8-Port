# State Comparison Report: Known-Good vs Current State

Investigation conducted on **ValoPreaim** (`C:\Users\Asus\Desktop\ValoPreaim UE 5.8\ValoPreaim`) and **Uiana** (`C:\Users\Asus\Desktop\Uiana-MapImporter-main`). No files were modified or reverted during this analysis.

---

## 1. Timeline & Baseline Definition

- **Last Known-Good State**:
  - **Timestamp**: `2026-10-19 09:11:31` (Log: `ValoPreaim_2-backup-2026.10.19-09.11.31.log`)
  - **Git Commit**: `9d5d77f` (*"Fix texture parameter alias mapping and path combination in UianaImporter"*)
  - **Renderer Profile**:
    - `DefaultGraphicsRHI=DefaultGraphicsRHI_DX12`
    - `shaderplatform="PCD3D_SM6"`
    - `r.ForwardShading=0` (**Deferred Shading**)
    - `r.Substrate=0` (**Substrate Disabled**)
    - `r.Nanite.ProjectEnabled=1` (**Nanite Enabled**)
    - `r.Shadow.Virtual.Enable=1` (**Virtual Shadow Maps Enabled**)
    - `r.DynamicGlobalIlluminationMethod=0` (None / Baked)
    - `r.ReflectionMethod=0` (None)
  - **Status**: Ascent imported and rendered cleanly without crash. Editor exited cleanly (`LogExit: Exiting`).

- **Transition to Crash State**:
  - Between `2026-10-19 19:24` and `19:30` (`ValoPreaim_2-backup-2026.10.19-19.30.18.log`), `r.ForwardShading=1` was enabled in `DefaultEngine.ini` while `r.Substrate=1` remained active.
  - **First Crash**: At `2026-10-19 19:30:18`, the engine crashed with:
    ```text
    Assertion failed: false [File:D:\build\++UE5\Sync\Engine\Source\Runtime\RHICore\Private\RHICoreShader.cpp] [Line: 53]
    Shader attempted to bind uniform buffer 'FOpaqueBasePassUniformParameters' at slot [Name: SceneTextures, Slot: 13] with hash '439736432', but the shader expected 'TranslucentBasePass' with hash '2077857058'.
    ```
  - **Crash Pattern Across All Logs**:
    - Every run prior to `19:30` had `r.ForwardShading=0` and **zero** `RHICoreShader.cpp` assertions.
    - Every single crash with `RHICoreShader.cpp:53` occurred with `r.ForwardShading=1` active.
    - In the run at `2026-10-20 08:40` (`ValoPreaim_2.log`), although the user intended DX12 deferred, `r.ForwardShading=1` was still active in `DefaultEngine.ini`, producing the identical crash.

---

## 2. Findings Ordered by Likelihood of Affecting Material or Pass Selection

1. **`r.ForwardShading=True` combined with `r.Substrate=True` in [DefaultEngine.ini](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Config/DefaultEngine.ini)**
   - **Verdict: [VERIFIED]**
   - **Impact**: In Unreal Engine 5.8, Substrate is tailored for deferred GBuffer pipelines. Under Forward Shading, Substrate material permutations and forward mesh processors mismatch during base pass draw preparation, attempting to bind `FOpaqueBasePassUniformParameters` to a pipeline state object expecting `TranslucentBasePass`. In the known-good state, `r.ForwardShading` was `0` and `r.Substrate` was `0`.

2. **Force-reparenting translucent / decal / parallax materials to opaque `BaseEnv_MAT_V4` in Uiana C++**
   - **Verdict: [VERIFIED]**
   - **Impact**: In commits `f7b8429`, `005a33e`, and `0fae5c5` ([UianaImporter.cpp:804](file:///C:/Users/Asus/Desktop/Uiana-MapImporter-main/Uiana/Source/Uiana/Private/UianaImporter.cpp#L804)), materials with `BaseOpacity_RGB_Env_MAT`, `BaseOpacitySpecEnv_MAT`, and `HorizontalParallax` were redirected to parent `BaseEnv_MAT_V4`. In commit `be566c8`, glass materials were remapped with translucent blend modes, while commit `5278690` enforced opaque on windows. This created contradictory blend modes and material permutation states across static mesh sections and HISM components.

3. **HISM Material Slot Override and Blend Mode Guard in Uiana C++**
   - **Verdict: [VERIFIED]**
   - **Impact**: Commit `0fae5c5` added logic in [UianaImporter.cpp:1345](file:///C:/Users/Asus/Desktop/Uiana-MapImporter-main/Uiana/Source/Uiana/Private/UianaImporter.cpp#L1345) scanning HISM components for mixed opaque and translucent slots, forcefully replacing translucent slots with `BaseEnv_MAT_V4`. This directly alters material slot assignment and mesh pass processor grouping.

4. **Static Parameter Runtime Switching (`StaticSwitchParameters`)**
   - **Verdict: [VERIFIED]**
   - **Impact**: Commit `be566c8` in [UianaImporter.cpp](file:///C:/Users/Asus/Desktop/Uiana-MapImporter-main/Uiana/Source/Uiana/Private/UianaImporter.cpp) began parsing and applying `StaticParametersRuntime.StaticSwitchParameters` on material instances. Static switches generate different shader permutations and alter whether a material uses depth prepass, masked vs opaque evaluation, or virtual texture lookups.

5. **`r.Nanite.ProjectEnabled=False` (Changed from `True`)**
   - **Verdict: [VERIFIED]**
   - **Impact**: In the known-good state, Nanite was enabled (`r.Nanite.ProjectEnabled=1`). When Nanite is enabled, compatible static meshes are drawn via the Nanite rasterizer/compute pipeline rather than traditional `FBasePassMeshProcessor` draw calls. Disabling Nanite routes all meshes through traditional forward/deferred base pass mesh processors.

6. **`r.ReflectionMethod=2` (Lumen Reflections enabled, changed from `0` / None)**
   - **Verdict: [VERIFIED]**
   - **Impact**: Known-good had `r.ReflectionMethod=0`. Setting this to `2` invokes Lumen reflection passes and front-layer translucency reflection uniform buffers.

7. **`r.Shadow.Virtual.Enable=0` (VSM disabled, changed from `1` / enabled)**
   - **Verdict: [VERIFIED]**
   - **Impact**: Switches shadow depth rendering from Virtual Shadow Map page caching to standard Cascaded Shadow Maps (CSM). Affects shadow pass selection, but not base pass uniform buffer assignment.

8. **Asset Resaving during recent import runs (Oct 20 08:46)**
   - **Verdict: [VERIFIED]**
   - **Impact**: 687 materials and 1,064 meshes were regenerated and resaved to disk with the newer Uiana C++ logic active. Even when reopening without re-importing, the serialized assets on disk retain the modified material instance parentage and slot assignments.

9. **DirectX RHI Selection (`D3D11` vs `D3D12`)**
   - **Verdict: [VERIFIED - Ruled Out as Primary Cause]**
   - **Impact**: Both D3D11 (`SM5`) and D3D12 (`SM6`) crash identically when `r.ForwardShading=True`. The assertion is in engine-level RHI pass dispatch ([RHICoreShader.cpp:53](file:///D:/build/++UE5/Sync/Engine/Source/Runtime/RHICore/Private/RHICoreShader.cpp)), independent of the underlying D3D API backend.

---

## 3. Configuration Comparison (Current vs Last Known-Good)

### [Config/DefaultEngine.ini](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Config/DefaultEngine.ini)
Comparison between Known-Good (`ValoPreaim_2-backup-2026.10.19-09.11.31.log` startup) and current `DefaultEngine.ini`:

| Setting | Known-Good (Oct 19 09:11:31) | Current State | Affected Pass / Area |
| :--- | :--- | :--- | :--- |
| **`r.ForwardShading`** | **`0` (Deferred)** | **`True` (Forward)** | **BasePass pipeline (CRITICAL)** |
| **`r.Substrate`** | **`0` (Disabled)** | **`True` (Enabled)** | **Material architecture (CRITICAL)** |
| **`r.Nanite.ProjectEnabled`** | **`1` (Enabled)** | **`False` (Disabled)** | **Rasterization pipeline** |
| **`r.Shadow.Virtual.Enable`** | **`1` (Enabled)** | **`0` (Disabled)** | **Shadow passes** |
| **`r.ReflectionMethod`** | **`0` (None)** | **`2` (Lumen)** | **Reflection / Translucent passes** |
| **`r.DefaultFeature.Bloom`** | `1` (True) | `False` | Post-processing |
| **`r.DefaultFeature.LightUnits`** | `1` (EV100) | `0` (Unitless) | Lighting calculations |
| **`r.DefaultFeature.AutoExposure.Method`** | `0` (Manual) | `2` | Exposure |
| **`r.DefaultFeature.AutoExposure.Bias`** | `1.000000` | `0.800000` | Exposure |
| **`r.AntiAliasingMethod`** | `0` (None) | `3` (TSR) | Anti-aliasing pass |
| **`r.MSAACount`** | `4` | `2` | Forward MSAA resolve |
| **`r.Mobile.AntiAliasing`** | `2` | `5` | Mobile AA |
| **`r.Mobile.ExtendedGBuffer`** | `0` | `True` | Mobile GBuffer |
| **`DefaultGraphicsRHI`** | `DefaultGraphicsRHI_DX12` | `DefaultGraphicsRHI_DX12` | Graphics RHI (Unchanged) |
| **Targeted Shader Formats** | SM6 / SM5 enabled | SM6 / SM5 enabled | Shader compilation targets |
| **`bGenerateNaniteFallbackMeshes`** | `True` | `False` | Nanite fallback generation |
| **`r.GenerateMeshDistanceFields`** | `1` (True) | `True` | Distance fields (Unchanged) |
| **`r.DynamicGlobalIlluminationMethod`** | `0` (None) | `0` (None) | GI Method (Unchanged) |

### Other Configuration Files:
- **[Config/DefaultGame.ini](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Config/DefaultGame.ini)**: Unchanged since initial project creation (`2026-10-18 15:02`). Only sets ProjectID and CommonUI settings.
- **[Config/DefaultEditor.ini](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Config/DefaultEditor.ini)**: Only contains `[/Script/AdvancedPreviewScene.SharedProfiles]` (last modified `2026-10-19 09:00`). No renderer settings.
- **`Saved/Config/WindowsEditor/Editor.ini`**: Only contains `[/Script/UnrealEd.UnrealEdOptions] UsingXGE=False`.
- **`Saved/Config/WindowsEditor/GameUserSettings.ini`**: Window resolution settings; unchanged since `2026-10-18 15:05`.
- **`Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`**: Editor layout and MRU data; no renderer override CVars.

---

## 4. Files under `Content/` Modified Since Known-Good Point

Examined `C:\Users\Asus\Desktop\ValoPreaim UE 5.8\ValoPreaim\Content` (total 4,164 files):

- **`Content/ValorantContent/Textures/`** (1,021 files):
  - **0 files modified** since known-good.

- **`Content/ValorantContent/Materials/`** (1,124 files total):
  - **687 files modified** since known-good.
  - All 687 are `.uasset` Material Instances (`_MI`).
  - Resaved at `2026-10-20 08:46:01` during the import run.
  - Parent distribution of saved material instances:
    - `BaseEnv_MAT_V4` (Opaque master): 849 total (687 modified).
    - `BaseEnv_MAT_V4_Foliage` (Masked cutout master): 22 instances.
    - `BaseEnv_Unlit_Texture_MAT_V4` (Unlit master): 16 instances.
    - `BaseOpacity_RGB_Env_MAT` / `HorizontalParallax`: 0 instances (all redirected to `BaseEnv_MAT_V4`).

- **`Content/ValorantContent/Meshes/`** (2,019 files total):
  - **`Ascent.umap`**: Modified at `2026-10-19 21:37:37` (10,300,214 bytes).
  - **1,063 Static Mesh assets (`.uasset`)**: Resaved at `2026-10-20 08:46:12`–`08:46:14` with updated slot names and material interface bindings.

- **Master Materials inside Uiana Plugin (`Plugins/Uiana/Content/Materials/`)**:
  - `BaseEnv_MAT_V4.uasset`: Modified `2026-10-19 12:46:39` (size changed from 60,506 to 64,675 bytes).
  - `BaseEnv_MAT_V4_Foliage.uasset`: Modified `2026-10-19 12:48:38` (size changed from 60,394 to 64,540 bytes).
  - `BaseEnv_Unlit_Texture_MAT_V4.uasset`: Modified `2026-10-19 14:04:11` (size changed from 60,072 to 64,423 bytes).
  - `M_Sky_Ascent_Daytime.uasset`: Modified `2026-10-18 21:17:10`.

---

## 5. Map & Level Status

- **Maps imported when it worked**:
  - **`Ascent`** is the **only** Valorant map that has ever been imported on this project. No other Valorant maps exist in `Content/` or logs.
- **Maps being opened / imported now**:
  - `Ascent` (via Uiana map importer or loading `Ascent.umap`).
- **Which maps crash and which don't**:
  - **Template map (`Engine/Content/Maps/Templates/OpenWorld.umap` / `Untitled`)**: **Does NOT crash.** Loads and renders cleanly even under current engine settings.
  - **`Ascent.umap` (and importing Ascent)**: **CRASHES consistently** as soon as the viewport draws the base pass under `r.ForwardShading=True`.
  - **Does the Ascent level that worked before also crash now?**
    - **Yes [VERIFIED]**: Opening `Ascent.umap` directly in the editor crashes at the identical assertion (`RHICoreShader.cpp:53`) under `r.ForwardShading=True`.

---

## 6. Changes Made to Uiana Source & Scripts

### A. Plugin C++ Source (`Uiana-MapImporter-main/Uiana/Source/Uiana/`)
Total changes between last known-good commit `9d5d77f` and current `HEAD` (`0fae5c5`): **+953 lines, -126 lines across 7 files**:

1. **[UianaImporter.cpp](file:///C:/Users/Asus/Desktop/Uiana-MapImporter-main/Uiana/Source/Uiana/Private/UianaImporter.cpp)**:
   - **Commit `0fae5c5`**: Redirected `HorizontalParallax` to `BaseEnv_MAT_V4`. Added HISM blend mode inspection loop to replace non-opaque materials with `BaseEnv_MAT_V4` if any slot is opaque.
   - **Commit `005a33e`**: Redirected `BaseOpacity_RGB_Env_MAT` and `BaseOpacitySpecEnv_MAT` to `BaseEnv_MAT_V4`. Added periodic memory trimming / GC (`CollectGarbage`) every 40 assets during import.
   - **Commit `3f852de`**: Set `LightComp->SetCastShadows(false)` for non-directional lights. Overrode post-process volume exposure (`AutoExposureBias = 0.5f`, `AmbientOcclusionIntensity = 0.25f`).
   - **Commit `f7b8429`**: Removed `BaseOpacity_RGB_Env_MAT` reparenting logic.
   - **Commit `721a31f`**: Implemented `IsDccPlaceholderName` and slot name restoration from JSON. Hardcoded fallback material assignments for `MidFloor_1`.
   - **Commit `3aec46d`**: Scaffold color blowout clamping, material variation parsing.
   - **Commit `5278690`**: Window / shard material opacity enforcement, vista material masking, cloth mesh filtering.
   - **Commit `be566c8`**: Filtered `Floater_*` and `Wires_*` clutter meshes. Parsed and applied `StaticSwitchParameters` on material instances. Remapped foliage to `BaseEnv_MAT_V4_Foliage` (masked).

2. **[BPFL.cpp](file:///C:/Users/Asus/Desktop/Uiana-MapImporter-main/Uiana/Source/Uiana/Private/BPFL.cpp)**:
   - Material variation resolution table (`StoneTile`, `Concrete_M0_BareBlend`, `_Grass`, `_Dirt`, `_Sand`).
   - Slot name override logic when DCC exported placeholder names (`lambert*`, `phong*`, `MaterialSlot_*`, `WorldGridMaterial`).
   - Periodic garbage collection during texture and mesh imports.

### B. Python Scripts
- **Plugin Python Scripts**: The Uiana plugin does not use Python for map importation; all import logic is in C++ and `cue4extractor.exe`.
- **Project Test Scripts in `C:\Users\Asus\Desktop\ValoPreaim UE 5.8\`**:
  - `test_import_ascent.py` (Created `2026-10-18 17:25`): Headless test script invoking `unreal.BPFL.import_valorant_map`.
  - `verify_lighting_fix.py` (Created `2026-10-18 18:42`): Verifies lightmap resolution and texture parameters.
  - `test_dynamic_map_selector.py` (Created `2026-10-18 18:56`): Tests `UianaDataSettings.get_map_names()`.
- **Scratch Diagnostic Scripts (`Uiana-MapImporter-main/scratch/`)**:
  - 30 diagnostic Python scripts created on `2026-10-19` to inspect blend materials, floor materials, and actor parameters (`inspect_blend_mat.py`, `check_materials.py`, `find_baseopacity_actors.py`, etc.). None of these modify plugin runtime code.
