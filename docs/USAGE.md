# Uiana Usage Guide

This document provides full instructions to extract, configure, and import Valorant maps into Unreal Engine 5 with the **Uiana Map Importer** plugin.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Prerequisites and System Requirements](#2-prerequisites-and-system-requirements)
3. [Installation and Project Setup](#3-installation-and-project-setup)
4. [Map Asset Extraction](#4-map-asset-extraction)
5. [Importing Maps in Unreal Engine](#5-importing-maps-in-unreal-engine)
6. [Data-Driven Material Rules Engine](#6-data-driven-material-rules-engine)
7. [Cubemaps and Panorama Setup Guide](#7-cubemaps-and-panorama-setup-guide)
8. [Lighting, Atmosphere, and UV Validation](#8-lighting-atmosphere-and-uv-validation)
9. [Shading Models and Performance](#9-shading-models-and-performance)
10. [Troubleshooting and FAQs](#10-troubleshooting-and-faqs)

---

## 1. Overview

**Uiana** automates the workflow to import Valorant map assets into Unreal Engine 5. The plugin replaces manual asset export, texture assignment, material setup, and mesh placement. Uiana provides these capabilities:

- **Geometry Reconstruction**: Imports meshes with the [UEFormat](https://github.com/h4lfheart/UEFormat) (`.uemodel`) parser module.
- **Hierarchical Instancing (HISM)**: Combines repetitive map props into `AHismActor` instances for high runtime performance.
- **Material Instantiation**: Reconstructs Material Instances from extracted JSON data. It maps them to Unreal Master Materials with a rules engine that supports hot reload.
- **Lighting & Atmosphere**: Spawns directional lights, point lights, spot lights, rect lights, an `ASkyLight`, and an `ASkyAtmosphere` component.
- **Sublevel Organization**: Preserves the original level hierarchy, streaming sublevels, and actor transformations.

---

## 2. Prerequisites and System Requirements

Before you configure Uiana, make sure that your system meets these requirements:

| Component | Minimum Requirement | Recommended |
|---|---|---|
| **Unreal Engine** | UE 5.0 | UE 5.3 to UE 5.8 |
| **Operating System** | Windows 10 (64-bit) | Windows 11 (64-bit) |
| **.NET Runtime** | .NET 6.0 Desktop Runtime | .NET 8.0 SDK / Runtime |
| **Visual Studio** | Visual Studio 2019 (v16.11+) | Visual Studio 2022 with C++ Game Workload |
| **Storage** | 30 GB free space | SSD with 60+ GB free space (for raw dumps and assets) |
| **Game Files** | Valid Valorant game installation | Live or archived PAK files with matching AES keys |

---

## 3. Installation and Project Setup

### 3.1. Clone Repository with Submodules

Uiana uses `UEFormat` to deserialize `.uemodel` mesh files. Always clone the repository with `--recurse-submodules`:

```bash
git clone --recurse-submodules https://github.com/djhaled/Uiana-MapImporter.git
```

If you cloned the repository without submodules, initialize them:

```bash
git submodule update --init --recursive
```

### 3.2. Install into Unreal Engine Project

1. Go to the directory of your target Unreal Engine project.
2. If a `Plugins` directory does not exist, create a `Plugins` directory in the project root:
   ```
   YourProject/
   ├── Config/
   ├── Content/
   ├── Plugins/
   │   ├── UEFormat/      <- Copy or install UEFormat here
   │   └── Uiana/         <- Copy or install Uiana here
   ├── Source/
   └── YourProject.uproject
   ```
3. Install the two plugins `Uiana` and `UEFormat` in `YourProject/Plugins/`.
4. Right-click the `YourProject.uproject` file. Select **Generate Visual Studio project files**.
5. Open `YourProject.sln` in Visual Studio. Build the project in the **Development Editor** configuration.

---

## 4. Map Asset Extraction

Uiana processes map assets unpacked from game PAK archives into structured JSON metadata, `.uemodel` geometry, and `.png` or `.tga` textures.

### 4.1. Extraction Tools

You can extract assets with either of these tools:
- **`cue4extractor`**: Tool based on [CUE4Parse](https://github.com/FabianFG/CUE4Parse), designed for automated batch extraction of Valorant levels.
- **[FModel](https://fmodel.app/)**: Visual explorer and exporter that supports CUE4Parse and UEFormat.

### 4.2. Output Folder Structure

Make sure that your extracted map folder uses this structure:

```
Valo Exports/
└── maps/
    └── <map_name>/                  (for example: ascent, bind, haven, split)
        ├── Art/
        ├── Lights/
        ├── Materials/               (JSON property dumps)
        ├── Meshes/                  (.uemodel static meshes)
        ├── Textures/                (.png / .tga / .dds)
        └── <MapName>_Data.json      (Actor transforms, lights, and sublevel metadata)
```

---

## 5. Importing Maps in Unreal Engine

After you compile and enable the plugins, start Unreal Editor.

### 5.1. Open the Uiana Import Window

1. On the Unreal Editor toolbar, select the **Uiana** button.
2. The configuration dialog of the Uiana Importer will appear.

### 5.2. Configure Importer Settings

| Setting | Description |
|---|---|
| **Pak Path** | Path to your Valorant `Paks` directory (used if runtime asset resolution is active). |
| **Export Destination** | Path to the folder that contains your extracted map data (`Valo Exports/maps/<map_name>`). |
| **Content Destination** | Project Content Browser destination (for example: `/Game/ValorantContent/`). |
| **Map Selection** | Select the target map from the dropdown menu (for example: `Ascent`, `Bind`, `Haven`, `Split`, `Sunset`, `Abyss`). |

### 5.3. Import Options

- **Import Meshes**: Uses `UEFormat` to parse and register `.uemodel` assets.
- **Import Materials**: Reads material JSON dumps, creates `UMaterialInstanceConstant` assets, and connects texture parameters.
- **Spawn Actors**: Places static mesh actors and groups instanced geometry into `AHismActor` instances.
- **Spawn Lights**: Reconstructs Directional, Point, Spot, and Rect lights.
- **Spawn Sky & Atmosphere**: Adds an `ASkyLight` (intensity 2.0) and `ASkyAtmosphere` in the `Lights` level folder.

Select **Import Map** to start the import. The Editor status bar shows the progress. The Output Log (`LogUiana`) records detailed messages.

---

## 6. Data-Driven Material Rules Engine

Uiana includes a data-driven material resolution engine. The configuration file `Uiana/Config/MaterialRules.json` controls this engine.

### 6.1. Why Data-Driven?

In earlier versions, C++ source code contained all material mapping rules, slot overrides, parameter aliases, and fallback behaviors. If a developer added support for new maps or modified a material slot, the developer had to recompile the plugin.

With `MaterialRules.json`:
- **Hot-Reloadable**: The JSON file reloads at the start of each import. You can modify rules and immediately import again without an editor restart or a code rebuild.
- **Fail-Fast Validation**: Syntax errors, unknown fields, duplicate rule identifiers, or missing master material references stop the import immediately. The plugin shows an error dialog and writes a log entry. This action prevents corrupted content.
- **Deterministic**: Explicit priorities determine which rule applies when multiple patterns match.

### 6.2. Modifying Rules

To customize material assignments or add support for custom shaders:
1. Open `Plugins/Uiana/Config/MaterialRules.json`.
2. Add or update rules in `ParentRules`, `SlotOverrides`, or `ParameterAliases`.
3. Save the file. Start the import in the editor.

> [!NOTE]
> For full technical documentation, field definitions, and schema validation rules, see [SCHEMA.md](file:///SCHEMA.md).

---

## 7. Cubemaps and Panorama Setup Guide

Valorant stores cubemaps in separate face textures (`Side_0` through `Side_5`). To convert these textures into equirectangular panoramic HDRI images for the `SkyLight` or sky spheres of Unreal Engine, obey this procedure:

### 7.1. Face Mapping Order

When you export game files through `cue4extractor` or FModel, the six cubemap sides map to these cubemap axes:

| Exported File | Cubemap Face Axis | Description |
|---|---|---|
| `Side_0` | **NegX** | -X Face |
| `Side_1` | **PosX** | +X Face |
| `Side_2` | **PosZ** | +Z Face |
| `Side_3` | **NegZ** | -Z Face |
| `Side_4` | **PosY** | +Y Face |
| `Side_5` | **NegY** | -Y Face |

### 7.2. Face Orientation and Rotations

Arrange and rotate the faces according to this layout:

```
          [Side 4: PosY]
            (Rot: 0°)
[Side 0: NegX] [Side 1: PosX] [Side 2: PosZ] [Side 3: NegZ]
  (Rot: 0°)      (Rot: -90°)    (Rot: 180°)    (Rot: 90°)
          [Side 5: NegY]
            (Rot: 0°)
```

### 7.3. Converting to Equirectangular Panorama

1. Open the [GLSL Cubemap to Panorama Converter](https://danilw.github.io/GLSL-howto/cubemap_to_panorama_js/cubemap_to_panorama.html) in your browser.
2. Upload the textures `Side_0` through `Side_5` to their matching slots. Apply the rotation values shown in the layout.
3. Export the combined equirectangular panorama image (`.hdr` or `.png`).
4. Import the panorama into `/Game/ValorantContent/Cubemaps/` or `Plugins/Uiana/Content/Cubemaps/` in your Unreal Engine project.
5. In Unreal Engine, set `Mip Gen Settings` to `NoMipmaps`. Set the texture group to `Skybox`.
6. Assign the texture to your `SkyLight` (Source Type: `SLS_SpecifiedCubemap`) or to the material of the sky sphere.

---

## 8. Lighting, Atmosphere, and UV Validation

### 8.1. Automatic Sky Components

Uiana automatically creates atmospheric lighting during map import:
- **`ASkyAtmosphere`**: Simulates aerial perspective, Rayleigh scattering, and sky tint in the level.
- **`ASkyLight`**: Uses an initial intensity of `2.0` and a `Sky Distance Threshold` of `24000.0` to balance outdoor ambience and indoor shadow bounce.

### 8.2. Lightmap UV Coordinate Index Validation

When you import external game geometry into Unreal Engine, you can encounter this viewport error:
```
"Invalid Lightmap Settings - Object has multiple UV sets but Lightmap Coordinate Index is invalid"
```

Uiana automatically prevents this error:
- During mesh import, `UEFModelFactory` and `UianaImporter` examine the available UV channels (`NumUVs`) of the static mesh.
- If `NumUVs > 1`, the importer sets `LightMapCoordinateIndex` to `1` (channel 1 for baked lightmaps, channel 0 for diffuse).
- If only one UV set exists (`NumUVs <= 1`), the importer sets `LightMapCoordinateIndex` to `0`.
- This automated check keeps viewports free of warnings if you bake static lighting or use dynamic Lumen.

---

## 9. Shading Models and Performance

### 9.1. Deferred Shading (Recommended)

The developers validated and optimized Uiana for **Deferred Shading** (`r.ForwardShading=0`):
- Full support for multi-pass materials, decals, complex lighting, and HISM batching.
- Zero shader pipeline crashes during continuous soak tests.

### 9.2. Forward Shading Note

> [!WARNING]
> If you run Valorant maps with Forward Shading (`r.ForwardShading=1`) in Unreal Engine, driver or RHI errors can occur in `RHICoreShader.cpp` during BasePass rendering. Vertex-blend materials and multi-layered materials cause these errors. You must use Deferred Shading (`r.ForwardShading=0`).

---

## 10. Troubleshooting and FAQs

### Q: Unreal displays "Missing UEFormat Plugin" error on import.
**A:** Uiana requires `UEFormat` to deserialize `.uemodel` static mesh files. Make sure that you clone and compile the `UEFormat` submodule in the `Plugins/` directory of your project.

### Q: Imported materials show as grey, black, or default checkerboard.
**A:**
1. Make sure that the path of the extracted `Textures` directory matches the path in your importer settings.
2. Examine the Output Log for warnings with the `LogUianaConfig` tag.
3. If Valorant updated a shader parameter name (for example, from `Diffuse` to `BaseColor`), open `Config/MaterialRules.json`. Add the parameter alias in `ParameterAliases`.

### Q: Why do props look dark indoors?
**A:** Valorant uses baked lightmaps and custom environment cubemaps. Adjust the intensity of the imported `SkyLight`, or assign a map-specific HDRI cubemap (see [Section 7](#7-cubemaps-and-panorama-setup-guide)).

### Q: The editor crashes or freezes during large map imports.
**A:** Large maps (such as Haven, Split, or Breeze) contain tens of thousands of asset references. Make sure that your computer has at least 16 GB of available RAM. You can import assets in two steps. On the first pass, disable "Spawn Actors" to import all static meshes and textures. On the second pass, enable "Spawn Actors" to assemble the level.
