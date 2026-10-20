# Controlled Test Matrix Report: ValoPreaim Renderer Settings vs Ascent.umap

Test matrix executed on **ValoPreaim** (`C:\Users\Asus\Desktop\ValoPreaim UE 5.8\ValoPreaim`) loading level `Content/ValorantContent/Meshes/Ascent.umap`.
- **Target Map**: `/Game/ValorantContent/Meshes/Ascent`
- **Engine Binary**: `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe` (Real editor GUI process, not `-Cmd.exe`)
- **Pass Criteria**: Editor process stays alive and renders viewport for $\ge 90$ seconds with zero assertions or early exits.
- **Fail Criteria**: Process terminates early, produces a new crash dump folder in `Saved\Crashes\UECC-*`, or writes `Assertion failed` to `ValoPreaim.log`.
- **CVar Verification**: Every CVar setting was **verified directly from `LogConfig: Set CVar [[...]]` lines** in each run's dedicated log file. Zero values were assumed.
- **Cleanup**: `DefaultEngine.ini` was backed up prior to testing and restored to its exact original state (SHA-256 match verified).

---

## 1. Test Matrix Results Table

| Run | `r.ForwardShading` | `r.Substrate` | `r.Nanite.ProjectEnabled` | `r.Shadow.Virtual.Enable` | `r.ReflectionMethod` | Result | Crash Folder | Uptime / Frames Rendered |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- | :--- |
| **A** | `0` | `0` | `1` | `1` | `0` | **PASS** | *None* | **90.4s (906 frames)** |
| **B** | `1` | `0` | `1` | `1` | `0` | **FAIL** | `UECC-Windows-44BB1C4D4C2C48BFC2FA4BA9E6968DF7_0000` | 42.1s (Frame 768) |
| **C** | `0` | `1` | `1` | `1` | `0` | **PASS** | *None* | **90.4s (993 frames)** |
| **D** | `1` | `0` | `0` | `1` | `0` | **FAIL** | `UECC-Windows-795FC32A416E7E4F72EB57A5D5192390_0000` | 30.7s (Frame 2) |
| **E** | `1` | `1` | `1` | `1` | `0` | **FAIL** | `UECC-Windows-B65D653F4F66533D9CF22FB71A574A78_0000` | 54.8s (Frame 3) |
| **E (Orig)** | `1` | `1` | `0` | `0` | `2` | **FAIL** | `UECC-Windows-6D290DA0446C795FAFF910B00CF3AAD7_0000` | 35.5s (Frame 3) |

---

## 2. Detailed Run Analysis

### Run A (Known-Good Baseline) — PASS
- **Settings Confirmed**: `r.ForwardShading=0`, `r.Substrate=0`, `r.Nanite.ProjectEnabled=1`, `r.Shadow.Virtual.Enable=1`, `r.ReflectionMethod=0`.
- **Behavior**: Level `/Game/ValorantContent/Meshes/Ascent` loaded cleanly. Editor rendered 906 frames over 90.4 seconds with zero errors or assertions.
- **Implication**: The level geometry, serialized material instances, and textures are **not corrupt**. Under standard Deferred Shading, the scene is 100% stable.

### Run B (ForwardShading=1 only) — FAIL
- **Settings Confirmed**: `r.ForwardShading=1`, `r.Substrate=0`, `r.Nanite.ProjectEnabled=1`, `r.Shadow.Virtual.Enable=1`, `r.ReflectionMethod=0`.
- **Crash Dump**: `UECC-Windows-44BB1C4D4C2C48BFC2FA4BA9E6968DF7_0000`
- **Assertion**:
  ```text
  Assertion failed: false [File:D:\build\++UE5\Sync\Engine\Source\Runtime\RHICore\Private\RHICoreShader.cpp] [Line: 53] 
  Shader attempted to bind uniform buffer 'FOpaqueBasePassUniformParameters' at slot [Name: SceneTextures, Slot: 13] with hash '439736432', but the shader expected 'TranslucentBasePass' with hash '2077857058'.
  ```
- **Implication**: Simply turning on Forward Shading (`r.ForwardShading=1`) without Substrate or any other change causes the exact assertion failure.

### Run C (Substrate=1 only) — PASS
- **Settings Confirmed**: `r.ForwardShading=0`, `r.Substrate=1`, `r.Nanite.ProjectEnabled=1`, `r.Shadow.Virtual.Enable=1`, `r.ReflectionMethod=0`.
- **Behavior**: Loaded and rendered 993 frames over 90.4 seconds without errors or assertions.
- **Implication**: Substrate is **not** the cause of the crash. Substrate works cleanly with the scene when Deferred Shading is used.

### Run D (ForwardShading=1, Substrate=0, Nanite=0) — FAIL
- **Settings Confirmed**: `r.ForwardShading=1`, `r.Substrate=0`, `r.Nanite.ProjectEnabled=0`, `r.Shadow.Virtual.Enable=1`, `r.ReflectionMethod=0`.
- **Crash Dump**: `UECC-Windows-795FC32A416E7E4F72EB57A5D5192390_0000`
- **Assertion**: Identical `RHICoreShader.cpp:53` assertion. Crashed almost immediately (Frame 2).
- **Implication**: Disabling Nanite does not resolve the crash under Forward Shading; in fact, routing all geometry through traditional base pass draw calls triggers the assertion even faster.

### Run E & E (Orig) (ForwardShading=1 with Substrate=1) — FAIL
- **Crash Dumps**:
  - Run E (Isolated): `UECC-Windows-B65D653F4F66533D9CF22FB71A574A78_0000`
  - Run E (Orig Config): `UECC-Windows-6D290DA0446C795FAFF910B00CF3AAD7_0000`
- **Assertion**: Identical `RHICoreShader.cpp:53` assertion in both configurations.

---

## 3. Conclusions

1. **`r.ForwardShading=1` is the single, isolated trigger** for the `RHICoreShader.cpp:53` uniform buffer assertion in this project.
2. **Substrate, Nanite, Virtual Shadow Maps, and Reflection Methods are ruled out**:
   - Substrate enabled (`r.Substrate=1`) passes as long as Forward Shading is `0`.
   - Forward Shading crashes whether Substrate is `0` or `1`, and whether Nanite is `0` or `1`.
3. Setting `r.ForwardShading=0` (Deferred Shading) restores full stability when opening or working with `Ascent.umap`.
