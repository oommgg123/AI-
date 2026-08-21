#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# 生成版本头与版本资源（用户 179 轮：版本号单一来源，不硬编码）
# 用法: gen_version.py <VERSION> <RCVERSION> <h_in> <h_out> <rc_in> <rc_out>
#   VERSION   = "1.178"      （显示版本）
#   RCVERSION = "1,178,0,0"  （VERSIONINFO 四段）
import io
import sys


def replace_all(text, pairs):
    for k, v in pairs:
        text = text.replace(k, v)
    return text


def main():
    if len(sys.argv) < 7:
        print("usage: gen_version.py VERSION RCVERSION h_in h_out rc_in rc_out", file=sys.stderr)
        return 1
    version = sys.argv[1]
    rc = sys.argv[2]
    h_in, h_out = sys.argv[3], sys.argv[4]
    rc_in, rc_out = sys.argv[5], sys.argv[6]
    pairs = [("@@VERSION@@", version), ("@@RCVERSION@@", rc)]

    with open(h_in, "r", encoding="utf-8") as f:
        text = f.read()
    with open(h_out, "w", encoding="utf-8", newline="\n") as f:
        f.write(replace_all(text, pairs))

    with open(rc_in, "r", encoding="gbk") as f:
        text = f.read()
    with open(rc_out, "w", encoding="gbk", newline="\r\n") as f:
        f.write(replace_all(text, pairs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
