# minimal re-implementation of OpenDeck's required-field rules (src-tauri/src/plugins/manifest.rs + shared.rs, main @ 2026-09-17)
import json, sys
m=json.load(open(sys.argv[1])); errs=[]
for k in ("Name","Author","Version","Icon","Actions","OS"):
    if k not in m: errs.append("manifest."+k)
for i,a in enumerate(m["Actions"]):
    for k in ("Name","UUID","Icon","States"):
        if k not in a: errs.append(f"action[{i}].{k}")
    if "Encoder" in a:
        for k in ("Icon","StackColor","background","layout"):
            if k not in a["Encoder"]: errs.append(f"action[{i}].Encoder.{k}")
for o in m["OS"]:
    if "Platform" not in o: errs.append("OS.Platform")
print("manifest ok" if not errs else "MISSING: "+", ".join(errs)); sys.exit(1 if errs else 0)
