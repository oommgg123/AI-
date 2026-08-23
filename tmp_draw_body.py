import io, subprocess, re, difflib

head = subprocess.run(['git','show','fcc210d:src/main.cpp'], capture_output=True, text=True).stdout
head = head.replace('\r\n','\n').replace('\r','\n')

def extract(src, name):
    lines = src.split('\n')
    for i, ln in enumerate(lines):
        if re.search(r'\b' + name + r'\s*\(', ln) and not ln.strip().startswith('//') and ln.strip().startswith('void DrawFrame'):
            j = i
            while j < len(lines) and '{' not in lines[j]:
                j += 1
            if j >= len(lines): continue
            depth = 0
            k = j
            while k < len(lines):
                depth += lines[k].count('{') - lines[k].count('}')
                if depth == 0 and k > j:
                    return lines[i:k+1]  # 返回行列表（含行号偏移）
    return None

def norm_lines(lines):
    out = []
    for ln in lines:
        s = ln.replace('\r','')
        s = re.sub(r'app\.(vk|ui|scene|gizmo|aa|undo)\.', 'app.', s)
        s = re.sub(r'app->(vk|ui|scene|gizmo|aa|undo)\.', 'app->', s)
        s = re.sub(r'^static\s+', '', s)
        s = re.sub(r'^inline\s+', '', s)
        idx = s.find('//')
        if idx >= 0: s = s[:idx]
        s = s.strip()
        if s: out.append(s)
    return out

hf = extract(head, 'DrawFrame')
cur = io.open('src/renderer.cpp', encoding='utf-8', newline='').read().split('\n')
# 当前 DrawFrame 行号
for i, ln in enumerate(cur):
    if ln.strip().startswith('void DrawFrame'):
        j = i
        while j < len(cur) and '{' not in cur[j]: j += 1
        depth = 0
        k = j
        while k < len(cur):
            depth += cur[k].count('{') - cur[k].count('}')
            if depth == 0 and k > j: break
            k += 1
        cf = cur[i:k+1]
        break

hn = norm_lines(hf)
cn = norm_lines(cf)
print(f"HEAD DrawFrame 代码行: {len(hn)}, 当前: {len(cn)}")
d = list(difflib.unified_diff(hn, cn, 'HEAD', 'CUR', lineterm=''))
removed = [x for x in d if x.startswith('-') and not x.startswith('---')]
print(f"HEAD 有当前无: {len(removed)} 行")
print("========== 遗漏候选 ==========")
for x in removed:
    print(x[:140])
