import os, glob, sys
out=[]
def log(*a): out.append(" ".join(str(x) for x in a))

base = r"C:\Users\Administrator\WorkBuddy\2026-08-16-23-13-11\vulkan-blank\build"
objs = glob.glob(os.path.join(base, "**", "*.obj"), recursive=True)
# group by source basename
from collections import defaultdict
by = defaultdict(int)
for p in objs:
    sz = os.path.getsize(p)
    bn = os.path.basename(p)
    # strip .obj
    src = bn[:-4] if bn.endswith(".obj") else bn
    by[src] += sz

tot = sum(by.values())
log("OBJ_TOTAL_B %d" % tot)
# show entries, sort desc
for k,v in sorted(by.items(), key=lambda x:-x[1]):
    log("OBJ %s %d %.1f" % (k, v, 100.0*v/tot))

txt="\n".join(out)+"\n"
with open(r"C:\Users\Administrator\WorkBuddy\2026-08-16-23-13-11\vulkan-blank\tools\obj_report.txt","w",encoding="ascii",errors="replace") as f:
    f.write(txt)
sys.stderr.write("OK %d objs\n" % len(objs))
