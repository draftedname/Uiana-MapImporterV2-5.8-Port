import unreal
import json

output_log = r"c:\Users\Asus\Desktop\Uiana-MapImporter-main\test_import_results.json"
results = {
    "success": False,
    "supergrid_check": {},
    "zero_actors": [],
    "waterfalls": [],
    "switches_and_plates": [],
    "coffee_material": "",
    "post_process": {},
    "sun_light": {}
}

map_path = "/Game/ValorantContent/Meshes/Ascent"
unreal.log(f"Loading level {map_path}...")
unreal.EditorLoadingAndSavingUtils.load_map(map_path)

subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing_actors = subsys.get_all_level_actors()
unreal.log(f"Clearing {len(existing_actors)} old actors from map before re-import...")
for a in existing_actors:
    subsys.destroy_actor(a)

map_name = "Ascent"
paks_folder = r"C:\Riot Games\VALORANT\live\ShooterGame\Content\Paks"
export_folder = r"C:\Users\Asus\Desktop\Valo Exports"

unreal.log("Running UBPFL.import_valorant_map on Ascent into active world...")
import_ok = unreal.BPFL.import_valorant_map(
    map_name,
    paks_folder,
    export_folder,
    True, # bImportMeshes
    True, # bImportMaterials
    True, # bImportLights
    True  # bImportDecals
)
unreal.log(f"import_valorant_map returned: {import_ok}")
results["import_returned"] = import_ok

# Inspect actors in the freshly imported active world
actors = subsys.get_all_level_actors()
results["total_actors"] = len(actors)
unreal.log(f"Total newly imported actors in Ascent: {len(actors)}")

# 1. Check SuperGrid meshes
supergrid_total = 0
supergrid_hidden = 0
supergrid_no_shadow = 0
supergrid_folder = 0

for a in actors:
    label = a.get_actor_label()
    if label.lower().startswith("supergrid_"):
        supergrid_total += 1
        folder = a.get_folder_path()
        if "supergrid" in str(folder).lower():
            supergrid_folder += 1
            
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        if smc:
            if not smc.get_editor_property("visible"):
                supergrid_hidden += 1
            if not smc.get_editor_property("cast_shadow"):
                supergrid_no_shadow += 1

results["supergrid_check"] = {
    "total": supergrid_total,
    "hidden": supergrid_hidden,
    "no_shadow": supergrid_no_shadow,
    "in_supergrid_folder": supergrid_folder
}
unreal.log(f"SuperGrid check: {results['supergrid_check']}")

# 2. Check actors at (0, 0, 0)
zero_count = 0
for a in actors:
    loc = a.get_actor_location()
    if abs(loc.x) < 1.0 and abs(loc.y) < 1.0 and abs(loc.z) < 1.0:
        zero_count += 1
        results["zero_actors"].append((a.get_actor_label(), a.get_class().get_name()))

unreal.log(f"Actors at (0,0,0): {zero_count} (was 128 previously)")

# 3. Check Waterfalls
for a in actors:
    label = a.get_actor_label()
    if "waterfall_start" in label.lower() or "waterfall_tail" in label.lower():
        loc = a.get_actor_location()
        results["waterfalls"].append({
            "label": label,
            "loc": [loc.x, loc.y, loc.z],
            "is_zero": abs(loc.x) < 1.0 and abs(loc.y) < 1.0 and abs(loc.z) < 1.0
        })

# 4. Check Switches and Wall Plates
for a in actors:
    label = a.get_actor_label()
    if "switch" in label.lower() or "wallplate" in label.lower() or "destructible" in label.lower():
        loc = a.get_actor_location()
        results["switches_and_plates"].append({
            "label": label,
            "loc": [loc.x, loc.y, loc.z],
            "is_zero": abs(loc.x) < 1.0 and abs(loc.y) < 1.0 and abs(loc.z) < 1.0
        })

# 5. Check Decal_0_Coffee_14 Material
for a in actors:
    label = a.get_actor_label()
    if "coffee" in label.lower():
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        if smc:
            mat = smc.get_material(0)
            results["coffee_material"] = mat.get_name() if mat else "None"

# 6. Check PostProcessVolume
ppvs = [a for a in actors if a.get_class().get_name() == "PostProcessVolume"]
results["post_process_count"] = len(ppvs)
if ppvs:
    ppv = ppvs[0]
    settings = ppv.get_editor_property("settings")
    unbound = getattr(ppv, "unbound", True)
    try:
        unbound = ppv.get_editor_property("unbound")
    except Exception:
        pass
    results["post_process"] = {
        "label": ppv.get_actor_label(),
        "unbound": unbound,
        "auto_exposure_bias": settings.get_editor_property("auto_exposure_bias"),
        "color_saturation": str(settings.get_editor_property("color_saturation")),
        "white_temp": settings.get_editor_property("white_temp")
    }

# 7. Check Sun DirectionalLight
suns = [a for a in actors if "sun" in a.get_actor_label().lower() or a.get_class().get_name() == "DirectionalLight"]
if suns:
    sun = suns[0]
    dlc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    if dlc:
        results["sun_light"] = {
            "label": sun.get_actor_label(),
            "intensity": dlc.get_editor_property("intensity")
        }

# Save level with new setup
unreal.EditorLoadingAndSavingUtils.save_current_level()

results["success"] = (
    results["supergrid_check"]["total"] > 0 and
    results["supergrid_check"]["hidden"] == results["supergrid_check"]["total"] and
    all(not w["is_zero"] for w in results["waterfalls"]) and
    all(not s["is_zero"] for s in results["switches_and_plates"]) and
    results["coffee_material"] != "None" and
    results["coffee_material"] != ""
)

with open(output_log, "w") as f:
    json.dump(results, f, indent=2)

unreal.log(f"Test finished! Success: {results['success']}")
