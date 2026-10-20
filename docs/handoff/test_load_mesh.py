import unreal
mesh = unreal.load_object(None, "/Game/ValorantContent/Meshes/ModernTech_10_PanelSquareLarge.ModernTech_10_PanelSquareLarge")
unreal.log_warning(f"LOADED_MESH_RESULT: {mesh}")
if mesh:
    unreal.log_warning(f"SUCCESS: Loaded {mesh.get_name()} ({type(mesh)})")
else:
    unreal.log_warning("FAILED to load mesh")
