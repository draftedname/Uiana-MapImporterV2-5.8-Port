import unreal
import json

cls = unreal.load_class(None, "/Script/Uiana.UianaDataSettings")
print("Found class via load_class:", cls)

result = {}
if cls:
    settings = unreal.new_object(cls)
    map_names = settings.get_map_names()
    print("=== DYNAMIC MAP NAMES IN UE 5.8 ===")
    print(f"Total maps discovered: {len(map_names)}")
    for m in map_names:
        print(f"  - {m}")

    result = {
        "success": True,
        "count": len(map_names),
        "maps": [str(m) for m in map_names],
        "default_map": str(settings.get_editor_property("map"))
    }

    # Test setting map property dynamically
    settings.set_editor_property("map", "Abyss")
    result["test_set_abyss"] = str(settings.get_editor_property("map"))
else:
    result = {"success": False, "error": "Class UianaDataSettings not found"}

with open(r"C:\Users\Asus\Desktop\Valo Exports\test_dynamic_map_report.json", "w") as f:
    json.dump(result, f, indent=2)

print("Report saved to test_dynamic_map_report.json")
