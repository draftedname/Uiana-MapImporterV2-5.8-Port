import unreal
import json

sample_meshes = ["WoodBox_0", "Bench_0", "Shell_0_AtkSpawnIslandFoundationA", "BoatShop_0_ArchwayRailing"]
mesh_results = {}
for name in sample_meshes:
    path = f"/Game/ValorantContent/Meshes/{name}.{name}"
    mesh = unreal.load_asset(path)
    if mesh:
        mesh_results[name] = {
            "found": True,
            "light_map_coordinate_index": mesh.get_editor_property("light_map_coordinate_index"),
            "light_map_resolution": mesh.get_editor_property("light_map_resolution")
        }
    else:
        mesh_results[name] = {"found": False}

sample_mats = ["Wood_M2_SlattedWoodDark_MI", "Statue_0_M0_Lion_MI", "Brick_M13_ArcSmallTilesGrass_MI"]
mat_results = {}
for name in sample_mats:
    path = f"/Game/ValorantContent/Materials/{name}.{name}"
    mat = unreal.load_asset(path)
    if mat:
        tex_params = unreal.MaterialEditingLibrary.get_texture_parameter_names(mat)
        textures = {}
        for p in tex_params:
            tex_val = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, p)
            if tex_val:
                textures[str(p)] = tex_val.get_name()
        mat_results[name] = {
            "found": True,
            "texture_count": len(textures),
            "textures": textures
        }
    else:
        mat_results[name] = {"found": False}

report = {
    "meshes": mesh_results,
    "materials": mat_results
}

with open(r"C:\Users\Asus\Desktop\Valo Exports\verify_lighting_fix.json", "w") as f:
    json.dump(report, f, indent=2)

print("Report written successfully to verify_lighting_fix.json")
