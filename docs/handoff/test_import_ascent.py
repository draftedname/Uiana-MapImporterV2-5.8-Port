import unreal
import time
import os
import json

def run_test():
    unreal.log("==================================================")
    unreal.log("Starting Headless Ascent Import Test via Python...")
    unreal.log("==================================================")

    editor_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    world = editor_sub.get_editor_world()
    unreal.log(f"[Ascent Test] Initial editor world: {world.get_path_name() if world else 'None'}")

    paks = r"C:\Riot Games\VALORANT\live\ShooterGame\Content\Paks"
    export_dir = r"C:\Users\Asus\Desktop\Valo Exports"

    t0 = time.time()
    unreal.log("[Ascent Test] Calling unreal.BPFL.import_valorant_map...")
    success = unreal.BPFL.import_valorant_map(
        map_name="Ascent",
        paks_folder=paks,
        export_folder=export_dir,
        import_meshes=True,
        import_materials=True,
        import_lights=True,
        import_decals=True
    )
    t1 = time.time()
    unreal.log(f"[Ascent Test] import_valorant_map returned: {success} in {t1 - t0:.2f}s")

    # Re-fetch editor world
    world = editor_sub.get_editor_world()
    all_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    unreal.log(f"[Ascent Test] Total actors in world: {len(all_actors)}")

    actor_breakdown = {}
    mesh_actors_with_mesh = 0
    mesh_actors_without_mesh = 0
    hism_actors_with_mesh = 0
    hism_actors_without_mesh = 0
    total_hism_instances = 0
    mesh_names_found = set()
    mesh_names_missing = set()
    materials_found = set()
    lights_count = 0
    decals_count = 0

    for actor in all_actors:
        cls_name = actor.get_class().get_name()
        actor_breakdown[cls_name] = actor_breakdown.get(cls_name, 0) + 1

        if isinstance(actor, unreal.StaticMeshActor):
            comp = actor.static_mesh_component
            if comp:
                sm = comp.static_mesh
                if sm:
                    mesh_actors_with_mesh += 1
                    mesh_names_found.add(sm.get_name())
                    mat_count = comp.get_num_materials()
                    for mi in range(mat_count):
                        mat = comp.get_material(mi)
                        if mat:
                            materials_found.add(mat.get_name())
                else:
                    mesh_actors_without_mesh += 1
                    mesh_names_missing.add(actor.get_actor_label())
        elif "Hism" in cls_name or actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
            comp = actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
            if comp:
                sm = comp.static_mesh
                inst_count = comp.get_instance_count()
                total_hism_instances += inst_count
                if sm:
                    hism_actors_with_mesh += 1
                    mesh_names_found.add(sm.get_name())
                    mat_count = comp.get_num_materials()
                    for mi in range(mat_count):
                        mat = comp.get_material(mi)
                        if mat:
                            materials_found.add(mat.get_name())
                else:
                    hism_actors_without_mesh += 1
        elif isinstance(actor, (unreal.PointLight, unreal.SpotLight, unreal.DirectionalLight, unreal.RectLight, unreal.SkyLight)):
            lights_count += 1
        elif isinstance(actor, unreal.DecalActor):
            decals_count += 1

    report = {
        "success": bool(success),
        "elapsed_seconds": round(t1 - t0, 2),
        "total_actors": len(all_actors),
        "actor_breakdown": actor_breakdown,
        "mesh_actors_with_mesh": mesh_actors_with_mesh,
        "mesh_actors_without_mesh": mesh_actors_without_mesh,
        "hism_actors_with_mesh": hism_actors_with_mesh,
        "hism_actors_without_mesh": hism_actors_without_mesh,
        "total_hism_instances": total_hism_instances,
        "total_mesh_actors_in_level": mesh_actors_with_mesh + hism_actors_with_mesh,
        "unique_mesh_assets_loaded": len(mesh_names_found),
        "unique_materials_loaded": len(materials_found),
        "sample_meshes": list(mesh_names_found)[:30],
        "sample_materials": list(materials_found)[:30],
        "lights_count": lights_count,
        "decals_count": decals_count,
    }

    report_path = os.path.join(export_dir, "import_report_ascent.json")
    with open(report_path, "w") as f:
        json.dump(report, f, indent=4)

    unreal.log(f"[Ascent Test] Report written to: {report_path}")
    unreal.log(f"[Ascent Test] Summary:")
    unreal.log(f"  - Total Actors: {len(all_actors)}")
    unreal.log(f"  - StaticMeshActors with Mesh: {mesh_actors_with_mesh}")
    unreal.log(f"  - HismActors with Mesh: {hism_actors_with_mesh}")
    unreal.log(f"  - Total HISM Instances spawned: {total_hism_instances}")
    unreal.log(f"  - Total Mesh Actors in level: {mesh_actors_with_mesh + hism_actors_with_mesh}")
    unreal.log(f"  - Unique Meshes loaded: {len(mesh_names_found)}")
    unreal.log(f"  - Unique Materials loaded: {len(materials_found)}")
    unreal.log(f"  - Lights: {lights_count}")
    unreal.log(f"  - Decals: {decals_count}")
    unreal.log("==================================================")

if __name__ == "__main__":
    run_test()
