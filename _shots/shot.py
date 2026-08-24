import os
from mss import mss
from mss.tools import to_png
out = r"C:/Users/Administrator/WorkBuddy/2026-08-16-23-13-11/vulkan-blank/_shots/shot_main.png"
os.makedirs(os.path.dirname(out), exist_ok=True)
with mss() as sct:
    img = sct.grab(sct.monitors[1])
    to_png(img.rgb, img.size, level=6, output=out)
print("saved OK", os.path.getsize(out), "bytes")
