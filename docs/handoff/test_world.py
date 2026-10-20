import unreal

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor_subsystem.get_editor_world()
unreal.log(f">>> Modern get_editor_world: {world}")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal.log(f">>> LevelEditorSubsystem: {level_subsystem}")
