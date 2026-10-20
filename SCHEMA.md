# Uiana MaterialRules Schema Documentation

This document specifies the format, field semantics, and extension points for `Config/MaterialRules.json` in the Uiana map-importer plugin.

## 1. Overview and Design Philosophy

Prior to this refactor, material parent mappings, slot overrides, parameter aliases, and fallback rules were hardcoded directly in C++ (`BPFL.cpp` and `UianaImporter.cpp`). This caused code bloat, required recompilation for any map-specific material adjustment, and made accommodating new Valorant maps slow and error-prone.

The data-driven material rules engine externalizes these behaviors into `Config/MaterialRules.json`. The design principles are:
- **Hot-Reloadable**: The JSON is reloaded at the start of every import (via `FUianaConfigManager::Get().LoadRules()`), allowing rapid iterative tuning without recompiling C++.
- **Fail-Fast**: If `MaterialRules.json` is missing or malformed, contains unknown keys, duplicate rule names/priorities, a misspelled enum value, or any target master material that does not exist, the import aborts immediately with an actionable error dialog and log entry naming the offending rule/index.
- **Flexible and Extensible**: Optional fields have safe defaults, so adding a field is additive for authors. Unknown keys are rejected on load (see [Load-Time Validation](#load-time-validation)); extending the schema therefore means adding the field to the C++ struct and to the whitelist in `UianaConfigManager.cpp` first, which prevents silent typos from disabling a rule.
- **Priority-Driven**: Rules have explicit integer priorities (lower number = higher priority), ensuring deterministic evaluation order.

---

## 2. Root Structure

A valid `MaterialRules.json` contains a single JSON object with the following top-level keys:

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "SchemaVersion": 1,
  "Flags": { ... },
  "ClampVectorParameters": [ ... ],
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
| `ParameterAliases` | Map[String, String] | Yes | Mapping from incoming raw parameter names to canonical master parameter names. |
| `SecondaryParameterRoutes` | Array[Object] | Yes | Conditional dual-parameter binding routes. |
| `DccPlaceholderPrefixes` | Array[String] | Yes | Prefixes representing default DCC exporter material slots. |
| `ParentRules` | Array[Object] | Yes | Ordered rules for resolving master parent materials. |
| `SlotOverrides` | Array[Object] | Yes | Ordered rules for resolving static mesh material slots. |
| `VariationWords` | Array[String] | Yes | Substrings stripped from material names during variant resolution. |

### Load-Time Validation

Every import reloads the file (`FUianaConfigManager::LoadRules`) and aborts before touching any asset when a check fails. Log lines use `LogUianaConfig: Error` and include the rule name or array index.

| Check | Behavior |
|---|---|
| JSON syntax | Malformed JSON aborts with the parser error. |
| Unknown keys | Any key not in the documented schema is rejected, at the root, in `Flags`, and inside every `ParentRules`, `SlotOverrides`, and `SecondaryParameterRoutes` element. Catches misspellings such as `Priorityy` or `Pattern`. |
| Enum spelling | `MatchTarget` must be `MaterialName`, `ParentName`, or `PackagePath`; `MatchType` must be `Exact`, `Prefix`, `Contains`, or `Regex`; route `Type` must be `Texture` or `Vector`. Values are case-sensitive. |
| Rule names | `ParentRules[].RuleName` is required and unique; non-empty `SlotOverrides[].RuleName` values must be unique. |
| Priorities | `Priority` must be unique within `ParentRules` and within `SlotOverrides`, so evaluation order is deterministic rather than depending on the sort implementation. |
| Master materials | A static `TargetParent` under `/Uiana/Materials/` (no `{MaterialName}` placeholder) must load via `StaticLoadObject`. |
| Aliases | `ParameterAliases` keys must already be lowercase (they are matched against lowercased parameter names) and must not map to an empty target. |

Regex notes: `MatchType: "Regex"` uses ICU syntax (`FRegexPattern`) and searches anywhere in the match-target string, exactly like `Contains`. `bCaseSensitive: false` compiles the pattern with `ERegexPatternFlags::CaseInsensitive`. Compiled patterns are cached per load; the cache is cleared on reload.

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
| `bEnableVariationWordStripping` | Boolean | `true` | Enables automatic stripping of variation words (e.g. `_DirtBlend`, `_Grass`) during candidate fallback matching. |

---

## 4. Parameter Aliases and Routing

### 4.1 ParameterAliases
Valorant material instances use various naming conventions for common texture parameters depending on artist or release version. `ParameterAliases` maps incoming parameter names (case-insensitive) to Uiana master material parameter names:

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
Some master materials (such as `BaseOpacity_RGB_Env_MAT`) require binding the same source texture or vector to an additional parameter (e.g. `RGBA` or `color mult`).

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
List of lowercase parameter names that should have RGB values clamped to `[0.0, 1.0]` when `bEnableDiffuseVectorClamping` is true:
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

---

## 5. ParentRules

`ParentRules` define how a material instance resolves its parent master material. Rules are evaluated in ascending order of `Priority`. The first matching rule assigns `TargetParent`.

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
| `Priority` | Integer | Evaluation priority (lower number evaluated first; e.g. 10 before 50). Must be unique within `ParentRules`. |
| `MatchTarget` | String | Attribute to match against: `"MaterialName"`, `"ParentName"`, or `"PackagePath"`. |
| `MatchType` | String | Match algorithm: `"Exact"`, `"Prefix"`, `"Contains"`, or `"Regex"`. |
| `bCaseSensitive` | Boolean | Whether string comparisons are case-sensitive. |
| `Patterns` | Array[String] | List of patterns. If any pattern matches, the condition is satisfied. |
| `ExcludePatterns` | Array[String] | List of exclusion patterns. If any matches, the rule is skipped. |
| `TargetParent` | String | Full asset path to the target parent material (e.g. `/Uiana/Materials/BaseEnv_MAT_V4.BaseEnv_MAT_V4`). |

---

## 6. SlotOverrides

`SlotOverrides` resolve material assignments on static mesh slots during mesh import or actor instantiation. They handle mesh-specific overrides, blend variant substitutions, and typo corrections.

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
| `Priority` | Integer | Evaluation order (ascending). Must be unique within `SlotOverrides`. |
| `MeshName` | String | Target mesh name (e.g. `"MidFloor_1"`), or empty for any mesh. |
| `SlotIndex` | Integer | Target slot index (e.g. `0`), or `-1` for any slot. |
| `Patterns` | Array[String] | Substrings that must be present in the incoming material name. |
| `ConditionWords` | Array[String] | Additional trigger words (e.g. `"Blend"`, `"Grass"`). |
| `Candidates` | Array[String] | Ordered list of candidate material names to check for existence on disk. |
| `TargetMaterial` | String | Fixed material name to assign if this rule matches. |

---

## 7. VariationWords and DCC Placeholders

### 7.1 VariationWords
When resolving material variations that were not directly matched by explicit slot overrides, words in `VariationWords` are stripped iteratively from the material name to locate an existing base material asset:

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
Prefixes used to detect dummy material names assigned by Maya/3ds Max/Blender during DCC export:
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

To customize behavior for new maps without recompilation:
1. Open `Uiana/Config/MaterialRules.json`.
2. Add your rule with an appropriate priority and match criteria.
3. If referencing a new master material in `TargetParent`, ensure the asset exists under `/Uiana/Materials/` or `/Game/ValorantContent/Materials/`.
4. Trigger the map import. The configuration manager validates the JSON on load and reports rule match counts to the output log under `LogUianaConfig`.
