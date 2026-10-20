import unreal
unreal.log(">>> PYTHON LOG TEST: BPFL exists = " + str(hasattr(unreal, "BPFL")))
if hasattr(unreal, "BPFL"):
    unreal.log(">>> BPFL has import_valorant_map = " + str(hasattr(unreal.BPFL, "import_valorant_map")))
