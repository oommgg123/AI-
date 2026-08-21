#!/usr/bin/env python3
"""将 SPIR-V 二进制转为 C 头文件（静态字节数组），用于嵌入可执行文件。

用法: spv_to_inc.py <input.spv> <output.inc> <symbol_name>
"""
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("用法: spv_to_inc.py <input.spv> <output.inc> <symbol_name>", file=sys.stderr)
        return 1

    _, spv_path, inc_path, name = sys.argv
    with open(spv_path, "rb") as f:
        data = f.read()

    lines = []
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

    with open(inc_path, "w", encoding="utf-8") as f:
        f.write("// 由 spv_to_inc.py 自动生成，请勿手动编辑\n")
        f.write(f"static const unsigned char {name}[] = {{\n")
        f.write("\n".join(lines))
        f.write("\n};\n")
        f.write(f"static const size_t {name}Size = {len(data)};\n")

    print(f"{spv_path} -> {inc_path} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
