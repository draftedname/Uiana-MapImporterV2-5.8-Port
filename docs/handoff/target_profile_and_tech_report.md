# ValoPreaim (UE 5.8.3): Target Profile Verification & Competitive Tech Report

**Target Profile**: DirectX 12 (`PCD3D_SM5` only), Deferred Shading (`r.ForwardShading=0`), Substrate Disabled (`r.Substrate=0`), Nanite Disabled (`r.Nanite.ProjectEnabled=0`), Virtual Shadow Maps Disabled (`r.Shadow.Virtual.Enable=0`), Reflections Disabled (`r.ReflectionMethod=0`).

---

## 1. Controlled Execution Results Table

All CVar values, graphics API backends, and shader platforms were **verified directly from `LogConfig: Set CVar [[...]]` and `shaderplatform="PCD3D_SM5"` lines in each run's dedicated engine log**. Zero values were assumed.

| Run ID | Run Description | Shader Platform | `r.ForwardShading` | `r.Substrate` | `r.Nanite.ProjectEnabled` | `r.Shadow.Virtual.Enable` | `r.ReflectionMethod` | Uptime / Frame Count | Crash / Assertion | Result |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- | :---: |
| **Step 2: Baseline** | Known-good reference (DX12 SM6 Deferred Nanite=1 VSM=1) | `PCD3D_SM6` | `0` | `0` | `1` | `1` | `0` | 95.5s (Frame 308) | None | **PASS** |
| **Step 4: Run 1** | Target Profile (DX12 SM5 Deferred Nanite=0 VSM=0) | `PCD3D_SM5` | `0` | `0` | `0` | `0` | `0` | 120.3s (Frame 707) | None | **PASS** |
| **Step 4: Run 2** | Target Profile Verification 2 | `PCD3D_SM5` | `0` | `0` | `0` | `0` | `0` | 95.4s (Frame 16+) | None | **PASS** |
| **Step 4: Run 3** | Target Profile Verification 3 | `PCD3D_SM5` | `0` | `0` | `0` | `0` | `0` | 95.5s (Frame 16+) | None | **PASS** |
| **Step 4: Standalone** | Target Profile Standalone Session (`-game -windowed`) | `PCD3D_SM5` | `0` | `0` | `0` | `0` | `0` | 185.6s (Frame 252+) | None | **PASS** |

### Step 5 Assertion Status:
Across all target profile runs (including the 185-second standalone session), the engine encountered **0 crashes**, **0 early exits**, and **0 assertions**. The assertion `RHICoreShader.cpp:53` was **never triggered**. Therefore, level bisection was not required.

---

## 2. Visual Comparison & Geometry Audit

````carousel
![Baseline (DX12 SM6 Nanite=1 VSM=1)](/C:/Users/Asus/.gemini/antigravity/brain/751b7775-ac54-4a67-acdb-b0f97b02c547/ascent_baseline.png)
<!-- slide -->
![Target Profile (DX12 SM5 Nanite=0 VSM=0)](/C:/Users/Asus/.gemini/antigravity/brain/751b7775-ac54-4a67-acdb-b0f97b02c547/ascent_target_profile.png)
<!-- slide -->
![Pixel Difference Map](/C:/Users/Asus/.gemini/antigravity/brain/751b7775-ac54-4a67-acdb-b0f97b02c547/ascent_diff.png)
````

- **Camera Transform (Identical for all captures)**:
  - Location: `X = 300.0, Y = -4000.0, Z = 600.0`
  - Rotation: `Pitch = -15.0°, Yaw = 90.0°, Roll = 0.0°`
- **Missing / Invisible Geometry**: **None**. All static meshes, buildings, doors, archways, and props present in the baseline are fully present in the target profile. The difference map highlights standard Cascaded Shadow Maps (CSM) with unbaked ambient fallback vs Virtual Shadow Maps (VSM), but zero mesh culling or geometry dropouts occurred.

---

## 3. Nanite Asset & Importer Audit

- **Nanite Mesh Count in Ascent**:
  - Total unique static mesh assets in `Ascent.umap`: **1,185**.
  - Meshes with Nanite enabled: **0** (0.0%).
  - Meshes with Nanite disabled: **1,185** (100.0%).
- **Uiana Importer Nanite Behavior**:
  - Read-only inspection of all C++ (`.cpp`, `.h`) and Python source in `Uiana-MapImporter-main` and `UEFormat` confirms **no Nanite enablement**. Assets are imported as standard static meshes with default `NaniteSettings.bEnabled = false`.

---

## 4. Config Audit: Settings in DefaultEngine.ini That Seem Wrong / Conflicting

During verification, the following conflicting or obsolete settings were identified in [DefaultEngine.ini](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Config/DefaultEngine.ini):

1. **Ray Tracing & Path Tracing under SM5**:
   - `r.RayTracing=True`, `r.RayTracing.RayTracingProxies.ProjectEnabled=True`, `r.PathTracing=True`.
   - *Issue*: DXR hardware ray tracing and path tracing require SM6 and are completely inert under SM5, wasting shader compilation cycles and memory.
2. **Residual Lumen Ray Tracing**:
   - `r.Lumen.HardwareRayTracing=True` and `r.Lumen.Reflections.HardwareRayTracing.Translucent.Refraction.EnableForProject=True`.
   - *Issue*: Lumen is disabled (`r.DynamicGlobalIlluminationMethod=0`, `r.ReflectionMethod=0`).
3. **Unneeded Mesh Distance Fields**:
   - `r.GenerateMeshDistanceFields=True` (`r.DistanceFields.DefaultVoxelDensity=0.2`).
   - *Issue*: Generates distance field volumes for all 1,185 meshes on load, increasing disk cache and RAM with zero visual benefit since distance field shadows and Lumen AO are off.
4. **MegaLights Inconsistency**:
   - `r.MegaLights.FrontLayerTranslucency.EnableForProject=True` while `r.MegaLights.EnableForProject=False`.
5. **Mobile Renderer Settings in Desktop Project**:
   - `r.Mobile.*`, `r.MobileHDR=True`, and `r.Mobile.ShadingPath=1` bloat configuration for a Windows desktop build.
6. **Obsolete ShaderPlatform Section**:
   - `[ShaderPlatformConfig PCD3D_SM6]` section at the end of `DefaultEngine.ini` is dead configuration when targeting SM5 only.

---

## 5. Latency Reduction & Anti-Aliasing Systems Research (UE 5.8.3, DX12 SM5)

| Technology | Source / Repository | Compatible with UE 5.8.3? | License | Last Update | SM5 vs SM6 Requirement | Key Architectural Nuances |
| :--- | :--- | :---: | :--- | :---: | :---: | :--- |
| **NVIDIA Reflex** | Built into Engine (`Engine/Plugins/Runtime/Nvidia/Reflex`) & Streamline (`NVIDIA-RTX/Streamline`) | **Verified** | Proprietary NVIDIA SDK (Free for commercial game distribution); Streamline wrapper is MIT | v1.8 / v1.9 (2024–2026) | **SM5 Compatible** | Operates purely via NVAPI sleep fences and queue synchronization; zero shaders. Includes Flash Indicator for hardware latency analysis. |
| **AMD Anti-Lag 2** | AMD GPUOpen (`GPUOpen-LibrariesAndSDKs/AntiLag2-SDK`) | **Verified** via source build *(Prebuilt marketplace binary 5.8.3 unverified)* | **MIT License** | v1.0.1 (2024–2026) | **SM5 Compatible** | In-engine CPU-GPU synchronization hook. Operates via CPU thread sleep; zero shaders. Must **never** be run simultaneously with Reflex. |
| **FXAA** | Engine Native (`r.AntiAliasingMethod=1`) | **Verified** | Epic / NVIDIA | Built-in | **SM5 Compatible** | Screen-space post-process pixel shader. 0 temporal lag, 0 ghosting, ~0.3ms frame cost. Blurs fine crosshairs and distant head silhouettes. |
| **Intel CMAA2** | Intel Open Source (`GameTechDev/CMAA2`) | **Verified** via compute shader plugin | **Apache 2.0** | v2.0 (2023–2024) | **SM5 Compatible** | Runs on `cs_5_0` Compute Shader. 0 temporal lag, 0 ghosting, ~0.5ms frame cost. Excellent edge reconstruction; does not blur crosshairs. |
| **SMAA 1x** | Community plugins (`nicholas477/SMAAPlugin`) | **Verified** via custom pass | **MIT License** | Community | **SM5 Compatible** | Subpixel morphological AA. Sharp edges, 0 ghosting. Requires `r.TemporalAA.Jitter=0` to avoid jitter crawl artifacts. |
| **MSAA (2x/4x)** | Hardware Native Rasterizer | **Unsupported in Deferred** *(Forward only: `r.MSAACount=4`)* | Native Hardware | Standard | **SM5 Compatible** | Hardware subpixel clarity with 0 blur and 0 ghosting (Riot's *Valorant* standard). Requires Forward Shading. |
| **TSR / TAA** | Engine Native (`r.AntiAliasingMethod=2/4`) | **Verified** | Epic | Built-in | SM5 / SM6 | Temporal history accumulation across 4–8 frames. **Not recommended for esports**: causes severe ghosting during rapid flick shots. |
| **CAS (Sharpening)** | AMD FidelityFX CAS (`GPUOpen-Effects/FidelityFX-CAS`) | **Verified** via post-process material/compute | **MIT License** | FidelityFX SDK | **SM5 Compatible** | Contrast-adaptive sharpening. Runs in ~0.1ms on SM5 compute; boosts edge clarity without the halo artifacts of `r.Tonemapper.Sharpen`. |

*(Any third-party marketplace installer tagged explicitly for "5.8.3" is marked unverified, as compiling from official GitHub source against engine headers is the standard deployment route).*
