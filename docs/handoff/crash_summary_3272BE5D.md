# Crash Log Summary: `UECC-Windows-3272BE5D4189EDC0C7764C81940C04F3_0000`

**Session Context:**
- **Crash GUID:** `UECC-Windows-3272BE5D4189EDC0C7764C81940C04F3_0000`
- **Engine Version:** Unreal Engine 5.8.3 Development (Win64)
- **RHI / Shading:** DirectX 11 (`r.ForwardShading=True`)
- **Process:** `UnrealEditor.exe` (PID: `19184`)
- **System:** Windows 11 (26H2), AMD Ryzen 5 5600X
- **Time Before Crash:** ~155 seconds into editor session

---

## 1. Primary Fatal Error

**File:** `CrashContext.runtime-xml` / `ValoPreaim.log`

```text
Assertion failed: false [File:D:\build\++UE5\Sync\Engine\Source\Runtime\RHICore\Private\RHICoreShader.cpp] [Line: 53] 
Shader attempted to bind uniform buffer 'FOpaqueBasePassUniformParameters' at slot [Name: SceneTextures, Slot: 13] with hash '439736432', but the shader expected 'TranslucentBasePass' with hash '2077857058'.
```

---

## 2. Render Hardware Breadcrumbs

**File:** `Breadcrumbs_RHIThread_0.txt`

```text
Breadcrumbs 'RHIThread'
 - ParallelDraw (Index: 1, Num: 4)
 - BasePass
 - Scene
 - RenderGraphExecute - /ViewFamilies
 - SceneRender - ViewFamilies
 - Frame 1192
```

---

## 3. Crashed Thread Call Stack

**Thread:** `RHIThread` (Thread ID: `16568`, `IsCrashed: true`)

```text
UnrealEditor-RHICore!UE::RHICore::ValidateStaticUniformBuffer() [RHICoreShader.cpp:53]
UnrealEditor-D3D11RHI + 29837
UnrealEditor-D3D11RHI + 6fb53
UnrealEditor-RHI + 93af0
UnrealEditor-RHI + 91c18
UnrealEditor-RHI + b80c8
UnrealEditor-RHI + 92934
UnrealEditor-RHI + 94db2
UnrealEditor-Core + f5e1e
UnrealEditor-Core + e8bfe
UnrealEditor-Core + e8efe
UnrealEditor-RenderCore + 1aa7ba
UnrealEditor-Core + 8205b8
UnrealEditor-Core + 819ee5
KERNEL32 + 2dda7
ntdll + 7ee4c
```

---

## 4. Engine Log Timeline (`ValoPreaim.log` Tail)

```text
[2026.10.20-07.57.08:946][189] AssetCheck: /Game/ValorantContent/Meshes/MidFloor_1 Validating asset
[2026.10.20-07.57.08:968][189] AssetCheck: /Game/ValorantContent/Meshes/ABuilding_1_MiddleSteps Validating asset
[2026.10.20-07.57.08:978][189] AssetCheck: /Game/ValorantContent/Meshes/AtkBridge_1 Validating asset
[2026.10.20-07.57.08:993][189] LogContentValidation: Validated asset counts for 9 validators: 0 errors
[2026.10.20-07.57.10:377][193] LogWindows: Error: appError called: Assertion failed: false [File:D:\build\++UE5\Sync\Engine\Source\Runtime\RHICore\Private\RHICoreShader.cpp] [Line: 53] 
Shader attempted to bind uniform buffer 'FOpaqueBasePassUniformParameters' at slot [Name: SceneTextures, Slot: 13] with hash '439736432', but the shader expected 'TranslucentBasePass' with hash '2077857058'.
[2026.10.20-07.57.10:377][193] LogWindows: Windows GetLastError: The operation completed successfully. (0)
```

---

## 5. Technical Diagnostic

1. **Failure Point:** Viewport Base Pass parallel draw dispatch (`ParallelDraw`, Frame 1192).
2. **Root Cause:** A draw call in the DX11 **Opaque Base Pass** was invoked using a pixel shader compiled for `TranslucentBasePass`. Slot 13 (`SceneTextures`) uniform parameter layout hash mismatched what the compiled shader expected (`439736432` vs `2077857058`), failing runtime RHI static validation.
