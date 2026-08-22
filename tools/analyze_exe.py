import struct, os, glob, sys

out = []
def log(*a):
    out.append(" ".join(str(x) for x in a))

exe = r"C:\Users\Administrator\WorkBuddy\2026-08-16-23-13-11\vulkan-blank\build\vulkan-blank-optimized.exe"
total = os.path.getsize(exe)
with open(exe,'rb') as f:
    data = f.read()

assert data[:2] == b'MZ'
e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
assert data[e_lfanew:e_lfanew+4] == b'PE\x00\x00'
nsec = struct.unpack_from('<H', data, e_lfanew+4+2)[0]
opt_hdr_size = struct.unpack_from('<H', data, e_lfanew+4+16)[0]
sec_table_off = e_lfanew + 4 + 20 + opt_hdr_size

secs = []
for i in range(nsec):
    off = sec_table_off + i*40
    name = data[off:off+8].split(b'\x00')[0].decode('latin1','replace')
    vsize, vaddr, rsize, roff = struct.unpack_from('<IIII', data, off+8)
    secs.append((name, rsize, vsize))

log("EXE_TOTAL_BYTES %d" % total)
log("EXE_TOTAL_KB %.1f" % (total/1024))
sum_file = 0
for name, rsize, vsize in secs:
    if rsize == 0: continue
    sum_file += rsize
    log("SECTION %s FILE_B %d VIRT_B %d PCT %.1f" % (name, rsize, vsize, 100.0*rsize/total))
log("SECTION_FILE_TOTAL_B %d" % sum_file)

inc_total = 0
for p in sorted(glob.glob(r"C:\Users\Administrator\WorkBuddy\2026-08-16-23-13-11\vulkan-blank\src\*.inc")):
    sz = os.path.getsize(p)
    inc_total += sz
    log("INC %d %s" % (sz, os.path.basename(p)))
log("INC_TOTAL_B %d" % inc_total)

for d in [r"C:\Users\Administrator\WorkBuddy\2026-08-16-23-13-11\vulkan-blank\src"]:
    for pat in ["*.cpp","*.h","*.vert","*.frag"]:
        files = glob.glob(os.path.join(d,pat)) + glob.glob(os.path.join(d,"shaders",pat))
        if files:
            s = sum(os.path.getsize(x) for x in files)
            log("SRC %s n=%d bytes=%d" % (pat, len(files), s))

txt = "\n".join(out) + "\n"
with open(r"C:\Users\Administrator\WorkBuddy\2026-08-16-23-13-11\vulkan-blank\tools\exe_report.txt","w",encoding="ascii",errors="replace") as f:
    f.write(txt)
sys.stderr.write("WROTE %d lines\n" % len(out))
