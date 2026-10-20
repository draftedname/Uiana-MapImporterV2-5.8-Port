# Crash Analysis: DX12 Forward vs. DX12 Deferred Shading

Comparison and forensic investigation of the two crash dumps:
- **Crash 1 (08:36:18Z)**: [`UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000) (DX12 Forward Shading)
- **Crash 2 (08:40:12Z)**: [`UECC-Windows-8B5E21F9423E49C3B0C8B7A0CED62017_0000`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-8B5E21F9423E49C3B0C8B7A0CED62017_0000) (DX12 Deferred Shading)

Associated logs directory: [`Saved/Logs/`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Logs/)

---

## 1. Verified Facts

The following points are verified directly from engine crash logs, XML descriptors, breadcrumbs, and minidump analysis:

### Fatal Assertion
Both sessions crash on the exact same assertion inside Unreal Engine's RHI core:
```text
Assertion failed: false [File:D:\build\++UE5\Sync\Engine\Source\Runtime\RHICore\Private\RHICoreShader.cpp] [Line: 53] 
Shader attempted to bind uniform buffer 'FOpaqueBasePassUniformParameters' at slot [Name: SceneTextures, Slot: 13] with hash '439736432', but the shader expected 'TranslucentBasePass' with hash '2077857058'.
```

### Callstack and Render Pipeline Location
The callstack is identical across both crashes. The failure occurs on a render thread worker task during parallel BasePass command list dispatch:
```text
[Callstack] UnrealEditor-D3D12RHI.dll!UE::RHICore::SetShaderUniformBuffers()
[Callstack] UnrealEditor-D3D12RHI.dll!FD3D12StateCache::ApplyState<FD3D12GraphicsCommandListReference>()
[Callstack] UnrealEditor-D3D12RHI.dll!FD3D12CommandContext::RHIDrawIndexedPrimitive()
[Callstack] UnrealEditor-Renderer.dll!DrawIndexedMesh()
[Callstack] UnrealEditor-Renderer.dll!TParallelMeshDrawCommandPass<...>::DispatchDraw()
[Callstack] UnrealEditor-Renderer.dll!UE::Tasks::Private::TTaskCommon<...>::DoExecute()
```
Breadcrumbs in both crash directories confirm the active pass was:
`Frame -> SceneRender - ViewFamilies -> RenderGraphExecute - /ViewFamilies -> Scene -> BasePass -> ParallelDraw`

### Crash 1 Details: DX12 Forward Shading
- **Directory**: [`UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000)
- **Log File**: [`ValoPreaim_2.log`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000/ValoPreaim_2.log)
- **Engine Version**: `5.8.3-58210709+++UE5+Release-5.8`
- **Session Duration**: `223s` (Frame 2304 / Tick 305)
- **Breadcrumbs**: 5 parallel workers (`ParallelDraw (Index: 1..5, Num: 7)`)
- **Pre-crash Events**:
  - Distance field generation finished for `DefPathABuilding_0` and `DefPathABuilding_1_Roof`.
  - `LogD3D12RHI: Waited for PSO creation for 100.000000ms` (x3), then `200.000000ms` (x3).

### Crash 2 Details: DX12 Deferred Shading
- **Directory**: [`UECC-Windows-8B5E21F9423E49C3B0C8B7A0CED62017_0000`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-8B5E21F9423E49C3B0C8B7A0CED62017_0000)
- **Log File**: [`ValoPreaim_2.log`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-8B5E21F9423E49C3B0C8B7A0CED62017_0000/ValoPreaim_2.log)
- **Engine Version**: `5.8.3-58210709+++UE5+Release-5.8`
- **Session Duration**: `94s` (Frame 1739 / Tick 740)
- **Breadcrumbs**: 4 parallel workers (`ParallelDraw (Index: 0..3, Num: 4)`)

### Clarification on Uniform Buffer Layout Hashes
The hash values `439736432` and `2077857058` reported in the assertion are **not asset IDs, mesh IDs, or material hashes**. They are engine-level compile-time type signatures representing:
- `439736432`: The type layout hash for struct `FOpaqueBasePassUniformParameters`.
- `2077857058`: The type layout hash for struct `TranslucentBasePass`.
These hashes are identical across every Unreal Engine 5.8 instance encountering this assertion and provide no information regarding which asset is at fault.

### Clarification on Slate Context (`UserActivityHint`)
In both crash descriptors ([`CrashContext.runtime-xml` (Crash 1)](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000/CrashContext.runtime-xml), [`CrashContext.runtime-xml` (Crash 2)](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-8B5E21F9423E49C3B0C8B7A0CED62017_0000/CrashContext.runtime-xml)), the `UserActivityHint` is:
```xml
<UserActivityHint>Layout="Uiana" Label="Valorant Map Importer" Content=SBorder</UserActivityHint>
```
This field solely records the Slate widget/tab that had user input focus or was active in the editor layout when the crash occurred. It does **not** indicate that plugin C++ or UI code threw an exception or crashed; the crash occurred exclusively inside the renderer core on a background rendering worker thread while drawing the 3D scene.

### Forensic Findings in Crash Folders and Saved Logs

1. **Crash Diagnostic Files**:
   - [`CrashReportClient.ini`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000/CrashReportClient.ini): Contains only standard crash reporter configuration flags (`bHideLogFilesOption=false`, `CrashConfigPurgeDays=2`, etc.). Contains no asset or debug information.
   - `UIRTCContext.txt`: **Nothing found** (file does not exist in either crash directory or in `Saved/Logs/`).
   - `Diagnostics.txt`: **Nothing found** (file does not exist in either crash directory or in `Saved/Logs/`).
   - [`UEMinidump.dmp`](file:///C:/Users/Asus/Desktop/ValoPreaim%20UE%205.8/ValoPreaim/Saved/Crashes/UECC-Windows-B0B5ECDF43E9BD6F7E79DBBCE782E92E_0000/UEMinidump.dmp): Both dumps (Crash 1: 2.33 MB, Crash 2: 2.83 MB) record the exception on the worker thread calling `RHICore::SetShaderUniformBuffers`. String scans extract package and asset paths present in general engine memory pages, but do not record the specific asset pointer or name being processed in the active draw call.

2. **Map and Level Paths Loaded**:
   - Initial editor map load: `Cmd: MAP LOAD FILE="../../../Engine/Content/Maps/Templates/OpenWorld.umap" TEMPLATE=1 SHOWPROGRESS=1 FEATURELEVEL=4` (standard blank template map).
   - In Crash 2, before import: `World = /Temp/Untitled_1.Untitled_1` uninitialized, switching to `/Engine/Maps/Templates/Untitled`.
   - Map being imported: Ascent (`maps/ascent`), extracted via:
     `cue4extractor.exe --game-directory "C:/Riot Games/VALORANT/live/ShooterGame/Content/Paks" --file-list "C:/Users/Asus/Desktop/Valo Exports/maps/ascent/_assets_textures_missing.txt"`
   - No separate `.umap` level file was loaded or saved; assets were imported directly into the active editor world session.

3. **Shader and Material Compilation Warnings/Errors**:
   - **Shader compile warnings or errors**: **Nothing found** (0 shader compile warnings or errors logged in either session).
   - **Material missing object warnings**: 618 warnings of type `LogUObjectGlobals: Warning: Failed to find object 'MaterialInterface /Game/ValorantContent/Materials/...'` and `/Uiana/Materials/...` (instances referencing parent materials not yet extracted, falling back to the engine default material).
   - **CoreRedirects errors**: 8 errors `LogCoreRedirects: Error: AddRedirect(AddKnownMissing) failed to add redirect from /Game/ValorantContent/Materials/Material #3` (and `#4`, `#5`).

4. **Shaders Compiling at Crash Time**:
   - **Nothing found**. Exactly 8 `LogShaderCompilers` log entries exist in each session, all occurring during initial editor boot (local worker initialization and unchanged SM6/SM5 autogen header checks). Zero background shader compilations were queued or active when either crash occurred.

5. **Last Assets Touched, Loaded, or Serialized**:
   - During the import, 1,138 asset packages were serialized and saved into `/Game/ValorantContent/Materials/` and `/Game/ValorantContent/Meshes/`.
   - The final mesh packages saved before validation were:
     1. `/Game/ValorantContent/Meshes/Tree_0_AshenC`
     2. `/Game/ValorantContent/Meshes/SwitchRoom_0_SwitchBox`
     3. `/Game/ValorantContent/Meshes/SwitchRoom_0_SwitchboxB`
   - Immediately following package saving, data validation validated 990 assets, ending with:
     - `/Game/ValorantContent/Meshes/Tree_0_AshenC Validating asset`
     - `/Game/ValorantContent/Meshes/SwitchRoom_0_SwitchBox Validating asset`
     - `/Game/ValorantContent/Meshes/SwitchRoom_0_SwitchboxB Validating asset`
   - In Crash 1, distance field builds for `DefPathABuilding_0` and `DefPathABuilding_1_Roof` executed directly before PSO creation and crash.

---

## 2. What the Assertion Means

In Unreal Engine's rendering architecture, rendering passes are bound to specific uniform buffer structures:
- **Opaque BasePass**: Renders opaque and masked geometry. It binds `FOpaqueBasePassUniformParameters` to slot 13 (`SceneTextures`).
- **TranslucentBasePass**: Renders translucent geometry. It binds `TranslucentBasePass` uniform parameters to slot 13.

When a shader is compiled, the engine embeds a type signature hash for every uniform buffer the shader expects. When a draw call executes, `UE::RHICore::SetShaderUniformBuffers()` validates that the uniform buffer currently bound to the slot matches the layout hash expected by the shader.

The assertion:
```text
Shader attempted to bind uniform buffer 'FOpaqueBasePassUniformParameters' at slot [Name: SceneTextures, Slot: 13] with hash '439736432', but the shader expected 'TranslucentBasePass' with hash '2077857058'.
```
means that **a shader compiled specifically for translucent rendering (`TranslucentBasePass`) was submitted to the opaque rendering pipeline (`BasePass`)**, which provided `FOpaqueBasePassUniformParameters`. When the D3D12 state cache attempted to bind the uniform buffer, the runtime layout validation detected the structural incompatibility and asserted to prevent GPU corruption.

---

## 3. What Has Been Ruled Out

The following factors have been tested and ruled out as the root cause:

1. **Rendering Hardware Interface (RHI)**:
   - Ruled out between DirectX 11 and DirectX 12. Both APIs trigger the identical uniform buffer validation failure.
2. **Shading Path (Forward vs. Deferred)**:
   - Ruled out between Forward Shading and Deferred Shading. Crash 1 (`B0B5ECDF`) occurred with Forward Shading enabled (`r.ForwardShading=1`); Crash 2 (`8B5E21F9`) and a subsequent run (`1AE046F0`) occurred with Deferred Shading (`r.ForwardShading=0`). Both crash at `RHICoreShader.cpp:53`.
3. **Stale Render State / Caching**:
   - Ruled out by performing a clean restart of the editor with viewport realtime rendering disabled before reopening the level. As soon as the viewport draws the imported scene, the crash occurs.
4. **Slate / Plugin UI Code**:
   - The plugin UI had Slate focus, but no plugin code is on the callstack. The crash is entirely within core engine renderer draw command dispatch.

Because the crash occurs deterministically regardless of RHI, shading path, or session cleanliness, **the bug is not an RHI driver issue or transient render state bug; it is an asset material or draw command configuration defect in the imported scene.**

---

## 4. Still Unknown: Which Actor, Mesh, and Material Triggers It

Despite thorough examination of logs and dumps, **the exact actor, mesh, and material triggering the assertion remains unknown**:
- The crash occurs inside `TParallelMeshDrawCommandPass::DispatchDraw()`, which processes pre-generated `FMeshDrawCommand` arrays on worker threads.
- In optimized Release/Development engine binaries, `FMeshDrawCommand` instances in the command list do not carry human-readable string names for their source `AActor`, `UStaticMeshComponent`, or `UMaterialInterface`.
- The engine log only records high-level phase completions (asset validation, distance field builds, PSO wait times) and does not log individual draw command submissions.
- String inspection of the minidump captures general memory allocations from recent operations (such as the 1,138 imported packages), but does not reveal which specific draw command was active in `FD3D12CommandContext::RHIDrawIndexedPrimitive`.

---

## 5. Hypotheses

The following hypotheses explain how a translucent shader could end up in the opaque BasePass queue:

1. **Blend Mode Mismatch on an Imported Material Instance**:
   - If a material instance was imported with its blend mode set to Translucent (`BLEND_Translucent` or `BLEND_Additive`), but its parent material or mesh component flags caused the mesh processor to categorize it as opaque (or vice-versa), the mesh draw command would be queued into the opaque `BasePass` while retaining the translucent shader pipeline.
2. **Missing Base Material Fallback Inconsistency**:
   - 618 material instances failed to find their parent materials (`Failed to find object 'MaterialInterface...'`) and fell back to the default material. If a material instance had parameter overrides compiled against a translucent permutation while the fallback base material is opaque, a pipeline state descriptor mismatch can occur.
3. **Specialized Mesh / Shader Type (Decals, Cloth, Foliage, Sky)**:
   - Several imported assets have specialized shading requirements:
     - Sky / Vista cards (`Vista_1_CloudA`, `M_Sky_Ascent_Daytime`, `M_Sky_Flowmap_Test`)
     - Cloth motion materials (`BaseEnv_ClothMotion_MAT`)
     - Foliage / Alpha assets (`Tree_0_AshenC`, `BaseEnvVertexAlpha_MAT`)
     - Decals / Projected markers (`Decal_0_VenetianSunMask`, `Mat_bomb_marking_A`)
   - If one of these materials has two-sided, vertex-alpha, or unlit settings that confuse the mesh pass processor, it may be bucketed into the wrong pass.
4. **Multi-Section Static Mesh with Mixed Blend Modes**:
   - A mesh with multiple material sections (e.g. Section 0 opaque, Section 1 translucent) where section batching or Nanite/LOD conversion incorrectly assigned the translucent section's draw command to the opaque pass.

---

## 6. Recommended Isolation Procedure

To identify the specific culprit without symbol-level debugger attachment:

1. **Binary Visibility Toggle in Outliner**:
   - With realtime off, hide half the imported actors in the Outliner. Enable realtime briefly. If it crashes, the culprit is in the visible half; if not, in the hidden half.
2. **Audit Materials by Blend Mode**:
   - Search the 1,138 assets in `/Game/ValorantContent/Materials/` for any material or instance where `BlendMode != BLEND_Opaque` (specifically Translucent, Masked, or AlphaComposite) and inspect their parent assignments.
3. **Inspect the Last Processed Assets**:
   - Test isolating the final assets saved before the crash: `Tree_0_AshenC`, `SwitchRoom_0_SwitchBox`, `SwitchRoom_0_SwitchboxB`, and the distance-field meshes `DefPathABuilding_0`, `DefPathABuilding_1_Roof`.
