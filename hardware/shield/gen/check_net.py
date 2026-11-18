"""Compare the schematic netlist with spec.py. Exit 1 on any difference."""
import sys, sexpdata
from sexpdata import Symbol as S
sys.path.insert(0, sys.path[0])
import spec
net = sexpdata.loads(open(sys.argv[1]).read())
got = {}
for sec in net:
    if isinstance(sec, list) and sec[0] == S("nets"):
        for n in sec[1:]:
            name = next(x[1] for x in n if isinstance(x, list) and x[0] == S("name"))
            nodes = {(str(x[1][1]), str(x[2][1])) for x in n if isinstance(x, list) and x[0] == S("node")}
            for r, p in nodes:
                got[(r, p)] = str(name).lstrip("/")
want = {}
for k, v in spec.ZIO_NETS.items():
    r, p = k.split("-"); want[(r, p)] = v
for r, (_, _, _, pn, _, _) in spec.PARTS.items():
    for p, v in pn.items(): want[(r, str(p))] = v
bad = []
for k, v in want.items():
    if got.get(k) != v: bad.append(f"{k}: want {v} got {got.get(k)}")
for k, v in got.items():
    if k not in want and not v.startswith("unconnected"): bad.append(f"{k}: unexpected net {v}")
# nets with a single node (typo guard)
from collections import Counter
c = Counter(want.values())
single = [n for n, cnt in c.items() if cnt < 2]
print(f"pins checked: {len(want)}, nets: {len(c)}, single-pin nets: {single}")
print("\n".join(bad) if bad else "netlist matches spec")
sys.exit(1 if bad else 0)
