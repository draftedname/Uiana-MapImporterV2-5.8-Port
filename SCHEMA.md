# Uiana MaterialRules Schema Documentation

This document specifies the format, field semantics, and extension points for `Config/MaterialRules.json` in the Uiana map-importer plugin.

## 1. Overview and Design Philosophy

Before this refactor, C++ source code (`BPFL.cpp` and `UianaImporter.cpp`) contained material parent mappings, slot overrides, parameter aliases, and fallback rules. This design required a C++ recompile for each map-specific material change. The procedure to add new Valorant maps was slow and caused errors.

The data-driven material rules engine externalizes these behaviors into `Config/MaterialRules.json`. The design principles are:
- **Hot-Reloadable**: The JSON file reloads at the start of each import (through `FUianaConfigManager::Get().LoadRules()`). This lets authors tune rules quickly without a C++ recompile.
- **Fail-Fast**: The import aborts immediately if `MaterialRules.json` is missing or invalid. The system also stops if the file contains unknown keys, duplicate rule names, duplicate priorities, misspelled enum values, or missing master materials. The system displays an error dialog and writes a log entry with the rule name or index.
- **Flexible and Extensible**: Optional fields use default values. The loader rejects unknown keys (see [Load-Time Validation](#load-time-validation)). To extend the schema, you must add the new field to the C++ struct and to the allowed list in `UianaConfigManager.cpp`. This requirement prevents spelling errors from disabling rules.
- **Priority-Driven**: Rules have explicit integer priorities (lower number = higher priority). This configuration guarantees a deterministic evaluation sequence.

---

## 2. Root Structure

A valid `MaterialRules.json` contains a single JSON object with the following top-level keys:

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "SchemaVersion": 1,
  "Flags": { ... },
  "ClampVectorParameters": [ ... ],
  "IgnoredParameters": [ ... ],
  "ParameterAliases": { ... },
  "SecondaryParameterRoutes": [ ... ],
  "DccPlaceholderPrefixes": [ ... ],
  "ParentRules": [ ... ],
  "SlotOverrides": [ ... ],
  "VariationWords": [ ... ]
}
```

### Root Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `$schema` | String | No | URI reference for JSON validation schemas. |
| `SchemaVersion` | Integer | Yes | Version of the schema structure. Currently `1`. |
| `Flags` | Object | Yes | Global behavioral feature flags. |
| `ClampVectorParameters` | Array[String] | Yes | Lowercase names of vector parameters clamped to `[0.0, 1.0]`. |
| `IgnoredParameters` | Array[String] | Yes | Lowercase names of parameters deliberately ignored (the importer skips binding and texture import for low-LOD streaming variants and debug pins). |
| `ParameterAliases` | Map[String, String] | Yes | Mapping from incoming raw parameter names to canonical master parameter names. |
| `SecondaryParameterRoutes` | Array[Object] | Yes | Conditional routes for dual-parameter bindings. |
| `DccPlaceholderPrefixes` | Array[String] | Yes | Prefixes that represent default DCC exporter material slots. |
| `ParentRules` | Array[Object] | Yes | Ordered rules to resolve master parent materials. |
| `SlotOverrides` | Array[Object] | Yes | Ordered rules to resolve static mesh material slots. |
| `VariationWords` | Array[String] | Yes | Substrings removed from material names during variant resolution. |

### Load-Time Validation

Each import reloads the configuration file (`FUianaConfigManager::LoadRules`). If a check fails, the import aborts before it modifies an asset. Log messages use `LogUianaConfig: Error` and include the rule name or array index.

| Check | Behavior |
|---|---|
| JSON syntax | Malformed JSON aborts with the parser error message. |
| Unknown keys | Any key not in the documented schema is rejected at the root, in `Flags`, and inside every rule element. This check catches spelling errors such as `Priorityy` or `Pattern`. |
| Enum spelling | `MatchTarget` must be `MaterialName`, `ParentName`, or `PackagePath`. `MatchType` must be `Exact`, `Prefix`, `Contains`, or `Regex`. Route `Type` must be `Texture` or `Vector`. Values are case-sensitive. |
| Rule names | `ParentRules[].RuleName` is required and must be unique. Non-empty `SlotOverrides[].RuleName` values must be unique. |
| Priorities | `Priority` must be unique in `ParentRules` and in `SlotOverrides`. This rule guarantees a deterministic evaluation sequence. |
| Master materials | A static `TargetParent` in `/Uiana/Materials/` (without `{MaterialName}` placeholder) must load through `StaticLoadObject`. |
| Aliases | `ParameterAliases` keys must already be lowercase. They are matched against lowercased parameter names. Keys must not map to an empty target. |

Regex notes: `MatchType: "Regex"` uses ICU syntax (`FRegexPattern`). It searches anywhere in the match-target string, exactly like `Contains`. The flag `bCaseSensitive: false` compiles the pattern with `ERegexPatternFlags::CaseInsensitive`. The system caches compiled patterns during load. The system clears the cache on reload.

---

## 3. Flags

The `Flags` object enables or disables specific pipeline behaviors:

```json
"Flags": {
  "bEnableHismMixedSlotGuard": false,
  "bEnableDiffuseVectorClamping": true,
  "bEnableReferencedTextureFallback": true,
  "bEnableVariationWordStripping": true
}
```

| Flag | Type | Default | Purpose |
|---|---|---|---|
| `bEnableHismMixedSlotGuard` | Boolean | `false` | Enables legacy guard that enforces opaque materials on HISM actors with mixed blend modes. |
| `bEnableDiffuseVectorClamping` | Boolean | `true` | Enables clamping of diffuse/tint vector multipliers to `[0.0, 1.0]` to prevent PBR blowout. |
| `bEnableReferencedTextureFallback` | Boolean | `true` | Enables fallback to `ReferencedTextures` / `CachedExpressionData` when no texture parameters are bound. |
| `bEnableVariationWordStripping` | Boolean | `true` | Enables automatic removal of variation words (for example: `_DirtBlend`, `_Grass`) during candidate fallback matching. |

---

## 4. Parameter Aliases and Routing

### 4.1 ParameterAliases
Valorant material instances use different naming conventions for texture parameters based on artist or release version. `ParameterAliases` maps incoming parameter names (case-insensitive) to Uiana master material parameter names:

```json
"ParameterAliases": {
  "materialtexture": "diffuse",
  "diffusetex": "diffuse",
  "diffuse a": "diffuse",
  "albedo": "diffuse",
  "texture rgb": "diffuse",
  "albedo a texture": "diffuse",
  "texture": "diffuse",
  "additonalnormal": "normal",
  "texture a normal": "normal",
  "normal a": "normal",
  "normal a texture": "normal",
  "normal_1": "normal",
  "mra a": "mra",
  "mra_texture": "mra"
}
```

### 4.2 SecondaryParameterRoutes
Some master materials (such as `BaseOpacity_RGB_Env_MAT`) require binding the source texture or vector to a secondary parameter (for example: `RGBA` or `color mult`).

```json
{
  "ParentCondition": "BaseOpacity_RGB_Env_MAT",
  "SourceParamAliases": ["diffuse", "materialtexture", "diffusetex", "albedo"],
  "TargetParam": "RGBA",
  "Type": "Texture"
}
```

| Field | Type | Description |
|---|---|---|
| `ParentCondition` | String | Substring that must match the resolved parent material name (or empty for unconditional). |
| `SourceParamAliases` | Array[String] | Array of parameter names that trigger this route. |
| `TargetParam` | String | Name of the target parameter in the material instance. |
| `Type` | String | `"Texture"` or `"Vector"`. |

### 4.3 ClampVectorParameters
List of lowercase parameter names that must have RGB values clamped to `[0.0, 1.0]` when `bEnableDiffuseVectorClamping` is true:
```json
"ClampVectorParameters": [
  "diffusecolor",
  "diffuse color",
  "tint",
  "color mult",
  "layer a tint",
  "layer b tint"
]
```

### 4.4 IgnoredParameters
List of lowercase parameter names that the importer and scanner deliberately ignore. These parameters include low-LOD streaming textures (`diffuse b low`, `diffuse low`, `texture b low`) that would overwrite base layers. They also include engine, debug, and LUT parameters (`weightmap`, `preintegratedskin`, `debuggrid`, `scenetexture`):
```json
"IgnoredParameters": [
  "diffuse b low",
  "diffuse low",
  "texture b low",
  "texture low",
  "weightmap",
  "preintegratedskin",
  "debuggrid",
  "debuggridtexture",
  "debugnormal",
  "debugdiffuse",
  "debugmra",
  "debugemissive",
  "editorgridmaterialtexture",
  "editorbrushmaterialtexture",
  "gridtexture",
  "defaultnormal",
  "defaultdiffuse",
  "defaultmra",
  "previewmaterialtexture",
  "thumbnailtexture",
  "scenetexture",
  "postprocessinput0",
  "postprocessinput1",
  "postprocessinput2",
  "postprocessinput3",
  "postprocessinput4"
]
```

---

## 5. ParentRules

The array `ParentRules` defines how a material instance resolves its parent master material. The importer evaluates rules in ascending order of `Priority`. The first matching rule assigns `TargetParent`.

### Example Rule

```json
{
  "RuleName": "FoliageMasked",
  "Priority": 30,
  "MatchTarget": "MaterialName",
  "MatchType": "Contains",
  "bCaseSensitive": false,
  "Patterns": [
    "/Foliage/",
    "Foliage_",
    "VegetationLeaves",
    "BeechGreenFoliage",
    "Arborvitae",
    "Conifer",
    "Ivy_",
    "Flowers",
    "PlantAlpha"
  ],
  "ExcludePatterns": [
    "Trunk",
    "Bark",
    "Planter"
  ],
  "TargetParent": "/Uiana/Materials/BaseEnv_MAT_V4_Foliage.BaseEnv_MAT_V4_Foliage"
}
```

### Field Definitions

| Field | Type | Description |
|---|---|---|
| `RuleName` | String | Unique identifier for telemetry and error reporting. |
| `Priority` | Integer | Evaluation priority (a lower number is evaluated first, for example: 10 before 50). Must be unique in `ParentRules`. |
| `MatchTarget` | String | Attribute to match against: `"MaterialName"`, `"ParentName"`, or `"PackagePath"`. |
| `MatchType` | String | Match algorithm: `"Exact"`, `"Prefix"`, `"Contains"`, or `"Regex"`. |
| `bCaseSensitive` | Boolean | Whether string comparisons are case-sensitive. |
| `Patterns` | Array[String] | List of patterns. If any pattern matches, the condition is satisfied. |
| `ExcludePatterns` | Array[String] | List of exclusion patterns. If any matches, the rule is skipped. |
| `TargetParent` | String | Full asset path to the target parent material (for example: `/Uiana/Materials/BaseEnv_MAT_V4.BaseEnv_MAT_V4`). |

---

## 6. SlotOverrides

The array `SlotOverrides` defines rules to resolve material assignments on static mesh slots during mesh import or actor spawning. These rules handle mesh-specific overrides, blend variant substitutions, and spelling corrections.

### Example Rules

```json
{
  "RuleName": "MidFloorSlot0",
  "Priority": 10,
  "MeshName": "MidFloor_1",
  "SlotIndex": 0,
  "Patterns": [],
  "ConditionWords": [],
  "Candidates": [],
  "TargetMaterial": "Brick_M6_HerringBoneBroken_MI"
},
{
  "RuleName": "StoneSquareTilesBlend",
  "Priority": 20,
  "MeshName": "",
  "SlotIndex": -1,
  "Patterns": ["StoneTile", "SquareTile"],
  "ConditionWords": ["Blend", "Grass", "Moss", "Mud", "Dirt"],
  "Candidates": [],
  "TargetMaterial": "Stone_M1_SquareTiles_MI"
},
{
  "RuleName": "HerringBoneVariations",
  "Priority": 30,
  "MeshName": "",
  "SlotIndex": -1,
  "Patterns": ["HerringBone"],
  "ConditionWords": ["Grass", "Water", "Mud"],
  "Candidates": [
    "Brick_M6_HerringBoneBroken_MI",
    "Brick_M6_HerringBone_MI"
  ],
  "TargetMaterial": ""
}
```

### Field Definitions

| Field | Type | Description |
|---|---|---|
| `RuleName` | String | Unique identifier for telemetry and logging. |
| `Priority` | Integer | Evaluation sequence (ascending). Must be unique in `SlotOverrides`. |
| `MeshName` | String | Target mesh name (for example: `"MidFloor_1"`), or empty for all meshes. |
| `SlotIndex` | Integer | Target slot index (for example: `0`), or `-1` for all slots. |
| `Patterns` | Array[String] | Substrings that must be in the incoming material name. |
| `ConditionWords` | Array[String] | More trigger words (for example: `"Blend"`, `"Grass"`). |
| `Candidates` | Array[String] | Ordered list of candidate material names to find on disk. |
| `TargetMaterial` | String | Fixed material name to assign if this rule matches. |

---

## 7. VariationWords and DCC Placeholders

### 7.1 VariationWords
When the importer resolves material variations without an explicit slot override, it removes words in `VariationWords` from the material name. This procedure finds an existing base material asset:

```json
"VariationWords": [
  "_DarkBlend", "DarkBlend_", "DarkBlend",
  "_DirtBlend", "DirtBlend_", "DirtBlend",
  "_BareBlend", "BareBlend_", "BareBlend",
  "_GrassBlend", "GrassBlend_", "GrassBlend",
  "_MossBlend", "MossBlend_", "MossBlend",
  "_MudBlend", "MudBlend_", "MudBlend",
  "_Blend", "Blend_", "Blend",
  "_Grass", "Grass_", "Grass",
  "_Dirt", "Dirt_", "Dirt",
  "_Sand", "Sand_", "Sand",
  "_Moss", "Moss_", "Moss",
  "_Mud", "Mud_", "Mud",
  "_Water", "Water_", "Water",
  "_Gravel", "Gravel_", "Gravel"
]
```

### 7.2 DccPlaceholderPrefixes
Prefixes used to find placeholder material names assigned by Maya, 3ds Max, or Blender during DCC export:
```json
"DccPlaceholderPrefixes": [
  "lambert",
  "phong",
  "blinn",
  "MaterialSlot_",
  "WorldGridMaterial",
  "DefaultMaterial"
]
```

---

## 8. Modifying and Extending Rules

To customize behavior for new maps without a C++ recompile:
1. Open `Uiana/Config/MaterialRules.json`.
2. Add your rule with the correct priority and match criteria.
3. Make sure that all new master materials in `TargetParent` exist in `/Uiana/Materials/` or `/Game/ValorantContent/Materials/`.
4. Start the map import. The configuration manager validates the JSON file on load. It writes rule match counts to the Output Log in the `LogUianaConfig` category.
