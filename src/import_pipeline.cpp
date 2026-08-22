// ============================================================================
//   专门的导入管线（用户 177 轮：导入导出相关全部集中于此）
//   模型导入（OBJ/STL/glTF·GLB/FBX）、OBJ 导出、贴图导入（普通+程序化）、
//   文件选择、导入工作线程、导入结果应用、场景装载与相机适配
// ============================================================================
#include "import_pipeline.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app.h"
#include "gdi_util.h"   // 共享 GDI 工具：RegisterWindowClass / DoubleBuffer（消除逐字复制的脚手架）
#include "ui_presets.h" // 统一控件预设体系：MC 窗口 GDI 颜色/进度条尺寸从 ui::g_theme 取值

// ---- main.cpp 提供的外部符号（保持单一定义）----
extern std::string g_error;
extern const char* g_stage;
extern void SetError(const char* msg);
extern void ShowErrorBox(const char* msg);
extern void VkbLog(const char* msg);
extern wchar_t g_startupObjPath[MAX_PATH];
extern bool g_useQuads;      // OBJ quad 渲染开关（main.cpp 定义）
extern bool g_showNormals;   // 法线渲染开关（main.cpp 定义）
extern bool g_swapZtoY;      // OBJ Z-up → Y-up（main.cpp 定义）
extern bool CreateVertexBuffer3D(App& app);
extern bool DecodePngWic(const unsigned char* data, size_t size,
                         std::vector<uint8_t>& rgba, int& width, int& height,
                         int targetW = 0, int targetH = 0);  // Round277：可选缩放（球按钮图标用）

// 导入进度（定义于此；main.cpp 进度条读取）
std::atomic<int> g_importProgress{-1};
std::atomic<int> g_mcProgress{-1};   // MC 导入专用（与模型导入分离）
std::atomic<int> g_importUploading{0};   // Round367：GPU 上传阶段标志

// ---- MC 窗口 UI 常量（从 ui::g_theme 预设取值，禁止再硬编码 RGB）----
const COLORREF kMcBg          = ui::g_theme.palette.bg;          // 窗口背景
const COLORREF kMcTextDim     = ui::g_theme.palette.textDim;     // 次要文字
const COLORREF kMcAccent      = ui::g_theme.palette.accent;      // 进度条填充
const COLORREF kMcAccentHover = ui::g_theme.palette.accentHover; // 蓝色强调文字（百分比/db 标题）
const COLORREF kMcTrack       = ui::g_theme.slider.track;        // 进度条轨道底色

namespace {

bool ReadFileToMemory(const wchar_t* path, std::vector<uint8_t>& data) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 0x7FFFFFFFLL) {
        CloseHandle(h);
        return false;
    }
    data.resize(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    const bool ok = ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) &&
                    got == data.size();
    CloseHandle(h);
    return ok;
}

// ------------------------- 我的世界（基岩版）NBT 解析（用户 188 轮迁入）-------------------------
// 基岩版 level.dat：8 字节小端整数头 + 小端 NBT
// 字符串长度 = 2 字节 uint16 LE；列表/数组元素个数 = 4 字节 int32 LE
// 版本字段：baseGameVersion(string) / lastOpenedWithVersion(int list)
// 校验范围：maj==1 && 12<=min<=21（1.12-1.21 经典编号），或 maj>=26（新版按年份编号，如 26.x）
struct NbtTag {
    enum class T { END=0, BYTE=1, SHORT=2, INT=3, LONG=4, FLOAT=5, DOUBLE=6,
                   BYTEARR=7, STR=8, LIST=9, COMPOUND=10, INTARR=11, LONGARR=12 };
    T type = T::END;
    int64_t iVal = 0;
    double dVal = 0;
    std::string sVal;
    std::vector<std::shared_ptr<NbtTag>> elems;
    std::map<std::string, std::shared_ptr<NbtTag>> fields;
    uint8_t elemType = 0;
};
using NbtPtr = std::shared_ptr<NbtTag>;

struct Reader {
    const uint8_t* p;
    const uint8_t* e;
    uint8_t u8() { return p < e ? *p++ : 0; }
    int16_t i16() { int16_t v=0; if (p+2<=e){ memcpy(&v,p,2); p+=2; } return v; }  // 小端
    int32_t i32() { int32_t v=0; if (p+4<=e){ memcpy(&v,p,4); p+=4; } return v; }
    int64_t i64() { int64_t v=0; if (p+8<=e){ memcpy(&v,p,8); p+=8; } return v; }
    float   f32() { float v=0;   if (p+4<=e){ memcpy(&v,p,4); p+=4; } return v; }
    double  f64() { double v=0;  if (p+8<=e){ memcpy(&v,p,8); p+=8; } return v; }
    uint32_t varint() {  // Minecraft 变长整数（LEB128，小端）
        uint32_t v=0; int s=0; uint8_t b;
        do { b=u8(); uint32_t bits=(uint32_t)(b&0x7f); if (s<32) v |= bits<<s; s+=7; }
        while (b&0x80 && s<35);
        return v;
    }
};

NbtPtr parseValue(uint8_t type, Reader& r) {
    NbtPtr v = std::make_shared<NbtTag>();
    v->type = (NbtTag::T)type;
    switch (type) {
        case 1: v->iVal = (int8_t)r.u8(); break;
        case 2: v->iVal = r.i16(); break;
        case 3: v->iVal = r.i32(); break;
        case 4: v->iVal = r.i64(); break;
        case 5: v->dVal = r.f32(); break;
        case 6: v->dVal = r.f64(); break;
        case 7: { uint32_t n=r.i32(); if (r.p+n<=r.e) r.p+=n; } break;  // ByteArray：4 字节长度
        case 8: { uint16_t n=r.i16(); if (r.p+n<=r.e){ v->sVal.assign((const char*)r.p,n); r.p+=n; } } break;  // String：2 字节 uint16 LE 长度
        case 9: { uint8_t et=r.u8(); uint32_t n=r.i32(); v->elemType=et;  // List：4 字节元素个数
                  for (uint32_t i=0;i<n;i++) v->elems.push_back(parseValue(et,r)); } break;
        case 10: { while (true){ uint8_t t=r.u8(); if (t==0) break;
                     uint16_t n=r.i16(); std::string name;  // 字段名：2 字节 uint16 LE 长度
                     if (r.p+n<=r.e){ name.assign((const char*)r.p,n); r.p+=n; }
                     v->fields[name]=parseValue(t,r); } } break;
        case 11: { uint32_t n=r.i32(); for (uint32_t i=0;i<n;i++) r.i32(); } break;  // IntArray：4 字节个数，跳过
        case 12: { uint32_t n=r.i32(); if (r.p+8*(size_t)n<=r.e) r.p+=8*(size_t)n; } break;  // LongArray：4 字节个数
        default: break;
    }
    return v;
}

NbtPtr parseLevelDat(const uint8_t* data, size_t len) {
    Reader r{ data, data+len };
    if (len < 8) return nullptr;
    r.i32(); r.i32();            // 8 字节头：存储工具版本 + NBT 大小
    uint8_t rootType = r.u8();
    if (rootType != 10) return nullptr;
    uint16_t nameLen = r.i16();   // 根名字长度（基岩版为 2 字节 uint16 LE，通常为 0）
    if (r.p+nameLen <= r.e) r.p += nameLen;
    return parseValue(10, r);
}

NbtTag* findTag(NbtTag* c, const char* name) {
    if (!c || c->type != NbtTag::T::COMPOUND) return nullptr;
    auto it = c->fields.find(name);
    return it == c->fields.end() ? nullptr : it->second.get();
}

bool parseVerStrMC(const std::string& s, int& maj, int& min, int& pat) {
    return sscanf(s.c_str(), "%d.%d.%d", &maj, &min, &pat) >= 2;
}

std::wstring s2wMC(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n<=0) return {};
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

bool findLevelDat(const std::wstring& dir, std::wstring& out) {
    WIN32_FIND_DATAW fd;
    std::wstring pat = dir + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if (wcscmp(fd.cFileName, L".")==0 || wcscmp(fd.cFileName, L"..")==0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (findLevelDat(full, out)) { found=true; break; }
        } else if (_wcsicmp(fd.cFileName, L"level.dat")==0) {
            out = full; found = true; break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

std::wstring getTempDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    return n ? std::wstring(buf, n) : std::wstring(L"C:\\Temp");
}

// 将 src 链接/复制到 dst：优先硬链接（零拷贝，.mcworld 多为数百 MB，避免整文件复制拖慢导入），
// 若跨卷或权限不足则回退到复制。
bool linkOrCopy(const std::wstring& src, const std::wstring& dst) {
    // 先确保目标父目录存在
    {
        std::wstring parent = dst;
        auto pos = parent.rfind(L'\\');
        if (pos != std::wstring::npos) {
            parent.resize(pos);
            CreateDirectoryW(parent.c_str(), nullptr);  // 已存在则忽略
        }
    }
    if (CreateHardLinkW(dst.c_str(), src.c_str(), nullptr)) return true;
    // 跨卷/权限不足时回退复制；TRUE=允许覆盖（避免残留文件导致重试失败）
    return CopyFileW(src.c_str(), dst.c_str(), TRUE) != 0;
}

// 解析 zip 中央目录，累加所有条目未压缩字节数（用于解压进度比例）。
// 只读 EOCD + 中央目录，不解压任何内容；解析失败返回 0（调用方退化为动画占位）。
uint64_t ZipTotalUncompressedSize(const std::wstring& zipPath) {
    FILE* f = _wfopen(zipPath.c_str(), L"rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    if (fsz < 22) { fclose(f); return 0; }
    // 从文件末尾最多 65557 字节内找 EOCD 签名 0x06054b50（取最后一个）
    const long tailStart = (fsz > 65557) ? fsz - 65557 : 0;
    const size_t tailLen = (size_t)(fsz - tailStart);
    std::vector<uint8_t> tail(tailLen);
    fseek(f, tailStart, SEEK_SET);
    if (fread(tail.data(), 1, tailLen, f) != tailLen) { fclose(f); return 0; }
    int eocd = -1;
    for (size_t i = tailLen - 22 + 1; i-- > 0;) {
        if (tail[i] == 0x50 && tail[i+1] == 0x4B && tail[i+2] == 0x05 && tail[i+3] == 0x06) { eocd = (int)i; break; }
    }
    if (eocd < 0) { fclose(f); return 0; }
    const uint16_t n = (uint16_t)(tail[eocd+10] | (tail[eocd+11] << 8));
    const uint32_t cdOff = (uint32_t)(tail[eocd+16] | (tail[eocd+17] << 8) |
                                      (tail[eocd+18] << 16) | (tail[eocd+19] << 24));
    fseek(f, cdOff, SEEK_SET);
    uint64_t total = 0;
    for (uint16_t k = 0; k < n; ++k) {
        uint8_t hdr[46];
        if (fread(hdr, 1, 46, f) != 46) break;
        if (!(hdr[0] == 0x50 && hdr[1] == 0x4B && hdr[2] == 0x01 && hdr[3] == 0x02)) break;  // 中央目录签名
        const uint32_t usize = (uint32_t)(hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24));
        const uint16_t nlen = (uint16_t)(hdr[28] | (hdr[29] << 8));
        const uint16_t elen = (uint16_t)(hdr[30] | (hdr[31] << 8));
        const uint16_t clen = (uint16_t)(hdr[32] | (hdr[33] << 8));
        total += usize;
        fseek(f, nlen + elen + clen, SEEK_CUR);
    }
    fclose(f);
    return total;
}

bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    if (!out.empty() && !ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr)) {
        CloseHandle(h); return false;
    }
    CloseHandle(h);
    return true;
}

// 真正校验一个 level.dat 文件
McWorldImporter::Validation validateWorld(const std::wstring& levelPath) {
    McWorldImporter::Validation res;
    std::vector<uint8_t> buf;
    if (!readFileBytes(levelPath, buf)) { res.reason = L"无法读取 level.dat 文件。"; return res; }
    NbtPtr root = parseLevelDat(buf.data(), buf.size());
    if (!root) {
        // 文件存在但 NBT 解析失败：极可能是网易版（中国版）加密存档，无法读取版本号
        res.valid = false;
        res.reason = L"该文件为网易版（中国版）加密存档，无法读取版本号。";
        return res;
    }

    int maj=-1, min=-1, pat=0;
    std::wstring verW;
    if (auto* t = findTag(root.get(), "baseGameVersion")) {
        if (parseVerStrMC(t->sVal, maj, min, pat)) verW = s2wMC(t->sVal);
    }
    if (maj < 0) {
        if (auto* t = findTag(root.get(), "lastOpenedWithVersion");
            t && !t->elems.empty() && t->elems.size() >= 4) {
            maj = (int)t->elems[1]->iVal;
            min = (int)t->elems[2]->iVal;
            pat = (int)t->elems[3]->iVal;
            verW = std::to_wstring(maj) + L"." + std::to_wstring(min) + L"." + std::to_wstring(pat);
        }
    }
    if (maj < 0) {
        // 无法读取确切版本号 → 报错（不再"尝试打开"，避免弹出 400×400 信息窗口冒充可加载）
        res.valid = false;
        res.versionText = L"未知版本";
        res.reason = L"无法读取此存档的确切版本号。"
                     L"可能是网易版（中国版）加密存档，或非标准基岩版格式。";
        return res;
    }
    if ((maj == 1 && min >= 12 && min <= 21) || maj >= 26) {
        res.valid = true;
        res.versionText = verW.empty() ? (std::to_wstring(maj)+L"."+std::to_wstring(min)+L"."+std::to_wstring(pat)) : verW;
        res.reason = L"基岩版世界，版本在支持的 1.12-1.21 / 26+ 范围内。";
    } else {
        res.valid = false;
        res.reason = L"世界版本 " + std::to_wstring(maj) + L"." + std::to_wstring(min) +
                     L"." + std::to_wstring(pat) + L" 不在支持的 1.12-1.21 / 26+ 基岩版范围内。";
    }
    return res;
}

}  // namespace

// ---------------- 公开 API ----------------

bool ImportModelFile(const wchar_t* path, SceneObject& out) {
    // 扩展名分发（用户 176 轮：新增 STL / glTF·GLB / FBX 格式）
    const std::wstring extW = [&] {
        const std::wstring p = path;
        const size_t dot = p.find_last_of(L'.');
        return (dot == std::wstring::npos) ? std::wstring{} : p.substr(dot + 1);
    }();
    if (extW == L"stl" || extW == L"STL") return ImportSTL(path, out);
    if (extW == L"gltf" || extW == L"glb" || extW == L"GLTF" || extW == L"GLB")
        return ImportGLTF(path, out);
    if (extW == L"fbx" || extW == L"FBX") return ImportFBX(path, out);
    return ParseOBJ(path, out);
}

bool ExportObj(const wchar_t* path, const SceneObject& obj) {
    std::vector<std::array<float, 3>> pts;
    if (!obj.solidVerts.empty()) {
        for (const auto& v : obj.solidVerts) pts.push_back({v.pos[0], v.pos[1], v.pos[2]});
    } else {
        for (const auto& v : obj.wireVerts) pts.push_back({v.pos[0], v.pos[1], v.pos[2]});
    }
    if (pts.empty()) return false;
    FILE* f = _wfopen(path, L"w");
    if (!f) return false;
    for (const auto& p : pts) fprintf(f, "v %.6f %.6f %.6f\n", p[0], p[1], p[2]);
    if (!obj.solidVerts.empty()) {
        const size_t triCount = obj.solidVerts.size() / 3;
        for (size_t i = 0; i < triCount; ++i) {
            fprintf(f, "f %zu %zu %zu\n", i * 3 + 1, i * 3 + 2, i * 3 + 3);
        }
    }
    fclose(f);
    return true;
}

// ---------------- 贴图导入（用户 177 轮：目前仅导入存储，渲染暂不采样）----------------

bool LoadTextureFromFile(const wchar_t* path, SceneObject& out) {
    std::vector<uint8_t> bytes;
    if (!ReadFileToMemory(path, bytes) || bytes.empty()) return false;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!DecodePngWic(bytes.data(), bytes.size(), rgba, w, h) || rgba.empty() || w <= 0 || h <= 0)
        return false;
    rgba.clear();   // Round369：贴图暂未接入渲染管线（无消费方），不长期持有省内存（原 40B/像素图集级占用）
    out.texWidth = w;
    out.texHeight = h;
    return true;
}

bool GenerateProceduralTexture(ProceduralTexture kind, int size, SceneObject& out) {
    if (size <= 0) size = 256;
    const int n = size;
    std::vector<uint8_t> rgba(static_cast<size_t>(n) * n * 4);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            switch (kind) {
                case ProceduralTexture::Checkerboard: {
                    const int cell = std::max(n / 8, 1);
                    const bool on = ((x / cell) + (y / cell)) & 1;
                    r = on ? 200 : 60;
                    g = on ? 200 : 60;
                    b = on ? 210 : 64;
                    break;
                }
                case ProceduralTexture::Gradient: {
                    const float t = static_cast<float>(x) / static_cast<float>(std::max(n - 1, 1));
                    r = static_cast<uint8_t>(255.0f * t + 0.5f);
                    g = static_cast<uint8_t>(120.0f + 120.0f * t + 0.5f);
                    b = static_cast<uint8_t>(255.0f * (1.0f - t) + 0.5f);
                    break;
                }
                case ProceduralTexture::Grid: {
                    const int step = std::max(n / 16, 1);
                    const bool line = (x % step == 0) || (y % step == 0);
                    r = line ? 220 : 40;
                    g = line ? 220 : 40;
                    b = line ? 230 : 42;
                    break;
                }
            }
            const size_t i = (static_cast<size_t>(y) * n + static_cast<size_t>(x)) * 4;
            rgba[i] = r; rgba[i + 1] = g; rgba[i + 2] = b; rgba[i + 3] = a;
        }
    }
    rgba.clear();   // Round369：贴图暂未接入渲染管线（无消费方），不长期持有省内存（原 40B/像素图集级占用）
    out.texWidth = n;
    out.texHeight = n;
    return true;
}

void LoadObjTexture(const wchar_t* objPath, const std::string& mtlFile, SceneObject& out) {
    out.texRgba.clear();
    out.texWidth = out.texHeight = 0;
    if (mtlFile.empty()) return;

    char objUtf8[MAX_PATH * 2] = {0};
    WideCharToMultiByte(CP_UTF8, 0, objPath, -1, objUtf8, sizeof(objUtf8), nullptr, nullptr);
    std::string dir(objUtf8);
    const size_t slash = dir.find_last_of('\\');
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);

    const std::string mtlPath = dir + mtlFile;
    wchar_t mtlW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, mtlPath.c_str(), -1, mtlW, MAX_PATH);
    std::vector<uint8_t> mtlBytes;
    if (!ReadFileToMemory(mtlW, mtlBytes)) return;
    const std::string mtlContent(reinterpret_cast<const char*>(mtlBytes.data()), mtlBytes.size());

    std::string texFile;
    const char* q = mtlContent.data();
    const char* const qEnd = q + mtlContent.size();
    while (q < qEnd) {
        if (std::strncmp(q, "map_Kd", 6) == 0 && (q + 6 < qEnd) &&
            (q[6] == ' ' || q[6] == '\t')) {
            q += 7;
            while (q < qEnd && (*q == ' ' || *q == '\t')) ++q;
            const char* s = q;
            while (q < qEnd && *q != '\n' && *q != '\r') ++q;
            const char* e = q;
            while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) --e;
            texFile.assign(s, e);
            break;
        }
        while (q < qEnd && *q != '\n') ++q;
        if (q < qEnd) ++q;
    }
    if (texFile.empty()) return;

    const std::string texPath = dir + texFile;
    wchar_t texW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, texPath.c_str(), -1, texW, MAX_PATH);
    std::vector<uint8_t> texBytes;
    if (!ReadFileToMemory(texW, texBytes)) return;
    std::vector<uint8_t> rgba;
    int tw = 0, th = 0;
    if (!DecodePngWic(texBytes.data(), texBytes.size(), rgba, tw, th) || rgba.empty()) return;
    rgba.clear();   // Round369：贴图暂未接入渲染管线（无消费方），不长期持有省内存（原 40B/像素图集级占用）
    out.texWidth = tw;
    out.texHeight = th;
    VkbLog(("[texture] 贴图已加载到内存: " + texFile + " (" + std::to_string(tw) + "x" +
            std::to_string(th) + ")").c_str());
}

// ---------------- 文件选择 ----------------

bool PickModelFile(HWND owner, wchar_t* outPath, size_t outCap) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"3D 模型 (*.obj;*.stl;*.gltf;*.glb;*.fbx)\0*.obj;*.stl;*.gltf;*.glb;*.fbx\0"
                      L"OBJ 模型 (*.obj)\0*.obj\0"
                      L"STL 模型 (*.stl)\0*.stl\0"
                      L"glTF / GLB (*.gltf;*.glb)\0*.gltf;*.glb\0"
                      L"FBX 模型 (*.fbx)\0*.fbx\0"
                      L"所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"选择 3D 模型";
    if (!GetOpenFileNameW(&ofn)) return false;
    const size_t len = std::wcslen(file);
    if (len == 0 || len >= outCap) return false;
    std::memcpy(outPath, file, (len + 1) * sizeof(wchar_t));
    return true;
}

bool PickTextureFile(HWND owner, wchar_t* outPath, size_t outCap) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"图片 (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp)\0"
                      L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0"
                      L"所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"选择贴图";
    if (!GetOpenFileNameW(&ofn)) return false;
    const size_t len = std::wcslen(file);
    if (len == 0 || len >= outCap) return false;
    std::memcpy(outPath, file, (len + 1) * sizeof(wchar_t));
    return true;
}

// Round270：启动加载示例模型时不自动选中（区分手动导入——手动导入仍自动选中新物体）
static bool g_isStartupImport = false;

// ---------------- 场景装载与相机适配 ----------------

void LoadSceneObjects(App& app) {
    g_isStartupImport = true;   // Round270：启动加载的示例模型不自动选中（默认无选中）
    std::wstring path;
    if (g_startupObjPath[0]) {
        path = g_startupObjPath;
    } else {
        wchar_t exePath[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
        {
            wchar_t* slash = nullptr;
            for (wchar_t* s = exePath; *s; ++s) if (*s == L'\\') slash = s;
            if (slash) *slash = 0;
        }
        char exePathUtf8[MAX_PATH * 2] = {0};
        WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exePathUtf8, sizeof(exePathUtf8), nullptr, nullptr);
        std::string fullPath = std::string(exePathUtf8) + "\\示例.obj";
        wchar_t objPath[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, objPath, MAX_PATH);
        path = objPath;
    }
    if (!path.empty()) LaunchImport(app, path.c_str());
}

// ---------------- 导入工作线程（CPU 解析；Vulkan 调用在主线程）----------------

struct ImportJob {
    HWND mainHwnd = nullptr;
    std::wstring path;
};
struct ImportResult {
    bool ok = false;
    SceneObject obj;
    std::wstring path;   // Round371：导入源路径（undo/redo 操作式——不拷贝大模型）
};

ImportResult g_importResult;
HANDLE g_importThread = nullptr;

DWORD WINAPI ImportWorker(LPVOID param) {
    ImportJob* job = reinterpret_cast<ImportJob*>(param);
    ImportResult r;
    // Round367：模型名 = 导入文件名（去扩展名）；启动默认示例模型叫"立方体"
    {
        std::wstring fname = job->path;
        const size_t slash = fname.find_last_of(L"\\/");
        if (slash != std::wstring::npos) fname = fname.substr(slash + 1);
        const size_t dot = fname.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) fname = fname.substr(0, dot);
        r.obj.name = g_isStartupImport ? L"立方体" : fname;
    }
    g_importProgress = 0;
    r.path = job->path;   // Round371：记录导入路径供 undo/redo
    try {
        r.ok = ImportModelFile(job->path.c_str(), r.obj);
    } catch (const std::bad_alloc&) {
        r.ok = false;
        g_error = "内存不足：模型过大，无法分配足够内存完成解析。";
    }
    if (!r.ok) r.obj = SceneObject{};
    g_importProgress = r.ok ? 100 : -1;
    g_importResult = std::move(r);
    const HWND h = job->mainHwnd;
    delete job;
    if (h && IsWindow(h)) PostMessageW(h, WM_APP, 0, 0);
    return 0;
}

void ApplyImportResult(App& app) {
    g_stage = "ApplyImportResult:开始";
    ImportResult r;
    std::swap(r, g_importResult);
    if (!r.ok) {
        MessageBoxW(app.hwnd, L"无法解析该模型文件（或文件已损坏/格式不支持）。", L"awa - 导入失败",
                    MB_ICONWARNING | MB_OK);
        return;
    }
    g_stage = "ApplyImportResult:vkDeviceWaitIdle";
    // Round375：设备若已丢失（驱动 TDR/超时）此裸调用会访问违规或挂死——先检后做，丢失则优雅退出导入
    {
        VkResult devRes = vkDeviceWaitIdle(app.device);
        if (devRes == VK_ERROR_DEVICE_LOST) {
            MessageBoxW(app.hwnd, L"显卡设备丢失，导入已取消。请重启软件或更新显卡驱动后重试。",
                        L"awa - 导入失败", MB_ICONWARNING | MB_OK);
            return;
        }
    }
    HCURSOR prevCursor = SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32650)));
    // Round249：追加而非清空——新导入物体不会删除已有物体（多物体场景）
    app.objects.push_back(std::move(r.obj));
    // Round270：启动加载的示例模型**不自动选中**（打开软件默认无选中）；手动导入仍自动选中新物体
    if (g_isStartupImport) {
        g_isStartupImport = false;
        app.selectedObject = -1;
    } else {
        app.selectedObject = static_cast<int>(app.objects.size()) - 1;
    }
    // Round250：记录撤销（Add）——启动导入无选中（selectedObject=-1）时不记录，
    // 否则 objects[-1] 越界访问（Round278 崩溃根因：启动即崩 vkDeviceWaitIdle / Debug 断言）
    if (app.selectedObject >= 0) {
        App::UndoEntry e;
        e.op = App::UndoOp::Add;
        e.index = app.selectedObject;
        // Round371：操作式撤销——导入不拷贝大模型（内存翻倍主因），记录路径，redo 时重新导入
        e.importPath = r.path;
        e.name = app.objects[app.selectedObject].name;
        PushUndo(app, e);
    }
    g_stage = "ApplyImportResult:CreateVertexBuffer3D";
    // Round367：GPU 顶点缓冲构建（大模型耗时）期间进度条保持显示（"正在上传渲染数据…"），
    // 真正完成才结束进度条——消除"进度条读完仍干等"的观感
    g_importUploading = 1;
    const bool vbOk = CreateVertexBuffer3D(app);
    g_importUploading = 0;
    g_importProgress = -1;
    if (!vbOk) {
        ShowErrorBox(g_error.c_str());
        g_error.clear();
    }
    // Round259：不再调用 FitCameraToObject（加载模型不动摄像机）、不再调整渲染距离
    SetCursor(prevCursor);
}

void WaitForImportThread() {
    if (g_importThread) {
        WaitForSingleObject(g_importThread, INFINITE);
        CloseHandle(g_importThread);
        g_importThread = nullptr;
    }
}

void LaunchImport(App& app, const wchar_t* path) {
    WaitForImportThread();
    app.importBarStartMs = GetTickCount64();
    ImportJob* job = new ImportJob{app.hwnd, std::wstring(path)};
    g_importThread = CreateThread(nullptr, 0, ImportWorker, job, 0, nullptr);
}

// ============================================================================
//   我的世界（基岩版）地图导入器（用户 188 轮：专用类封装，整入导入管线）
// ============================================================================

McWorldImporter& McWorldImporter::Instance() {
    static McWorldImporter s_instance;
    return s_instance;
}

// 白边正方体主题：纯白填充、直角、悬停描边黑色（本地覆盖版，不改全局 ui::g_theme.button）
// 注意：此元素走按钮渲染管线（DrawGdiButton）但**不是按钮**——纯视觉装饰，无点击行为。
static ButtonTheme MakeWhiteSquareTheme() {
    ButtonTheme t;
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
        t.normal[i] = white[i]; t.pressed[i] = white[i]; t.released[i] = white[i];
        t.hoverBorder[i] = white[i];   // 悬停时也是白色（保持一致）
    }
    t.animSpeed = 0.18f;
    return t;
}

// 取父目录（去掉末尾文件名），失败时返回原串
std::wstring parentDirOf(const std::wstring& file) {
    size_t slash = file.find_last_of(L"\\/");
    return slash == std::wstring::npos ? file : file.substr(0, slash);
}

// 递归统计目录：文件数 / 总字节数（LevelDB 的 db/ 通常较扁平，但递归更稳）
void StatDir(const std::wstring& dir, int& outCount, int64_t& outTotal) {
    outCount = 0; outTotal = 0;
    WIN32_FIND_DATAW fd;
    std::wstring pat = dir + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            int c2 = 0; int64_t t2 = 0;
            StatDir(full, c2, t2);
            outCount += c2; outTotal += t2;
        } else {
            ++outCount;
            LARGE_INTEGER fs; fs.HighPart = fd.nFileSizeHigh; fs.LowPart = fd.nFileSizeLow;
            outTotal += fs.QuadPart;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// 解压 .mcworld（zip）到 dst：异步跑 PowerShell（隐藏窗口），期间每 100ms 按
// 已解压字节数实时汇报进度到 g_mcProgress（映射到 10→45 区间，真实爬升）。
// 无法解析 zip 总大小时退化为慢速占位递增（避免"卡住不动"的假象）。
bool runExpandArchiveProgress(const std::wstring& zip, const std::wstring& dst) {
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Expand-Archive -Force -LiteralPath '";
    cmd += zip;
    cmd += L"' -DestinationPath '";
    cmd += dst;
    cmd += L"'\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;               // 不显示 PowerShell 窗口
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))   // 防止控制台弹窗
        return false;
    const uint64_t total  = ZipTotalUncompressedSize(zip);
    const uint64_t t0     = GetTickCount64();
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0) break;  // 进程退出
        if (GetTickCount64() - t0 > 120000) break;                          // 超时保护
        int cnt = 0; int64_t extracted = 0;
        StatDir(dst, cnt, extracted);
        int p;
        if (total > 0) {
            double r = (double)extracted / (double)total;
            if (r > 1.0) r = 1.0;
            p = 10 + (int)(35.0 * r);            // 10 → 45 按真实字节比例
        } else {
            p = 10 + (int)((GetTickCount64() - t0) / 1500);  // 占位：每 1.5s +1
        }
        if (p < 10) p = 10; else if (p > 44) p = 44;
        g_mcProgress = p;
    }
    DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    g_mcProgress = 45;   // 解压阶段收尾（避免进度条停滞）
    return ec == 0;
}

// 递归删除目录（仅用于清理 .mcworld 解压临时目录）
void RemoveDirRecursive(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    std::wstring pat = dir + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            RemoveDirRecursive(full);
        else
            DeleteFileW(full.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(dir.c_str());
}

// 字节数 → 人类可读（B / KB / MB）
std::wstring fmtBytes(int64_t b) {
    wchar_t buf[64];
    if (b < 1024) { swprintf(buf, 64, L"%lld B", (long long)b); }
    else if (b < 1024 * 1024) { swprintf(buf, 64, L"%.1f KB", (double)b / 1024.0); }
    else { swprintf(buf, 64, L"%.2f MB", (double)b / (1024.0 * 1024.0)); }
    return std::wstring(buf);
}

McWorldImporter::Validation McWorldImporter::Validate(const wchar_t* path) {
    Validation res;
    m_rootDir.clear(); m_levelDatPath.clear(); m_tempDir.clear();
    if (!path || !*path) { res.reason = L"路径为空。"; return res; }
    std::wstring p(path);

    if (_wcsicmp(p.c_str() + (p.size() >= 8 ? p.size() - 8 : 0), L".mcworld") == 0) {
        // .mcworld（zip）：硬链接/复制为 .zip 并解压到临时目录，再找 level.dat
        std::wstring tmp  = getTempDir() + L"\\awa_mcworld_" + std::to_wstring(GetTickCount64());
        // zip 放在 tmp 目录内，关闭窗口时随目录一并清理（避免临时 .zip 泄漏）
        std::wstring zip  = tmp + L"\\archive.zip";
        if (!linkOrCopy(p, zip)) {
            res.reason = L"无法准备 .mcworld 文件用于解压（硬链接/复制失败）。"; return res;
        }
        if (!runExpandArchiveProgress(zip, tmp)) {
            res.reason = L"无法解压 .mcworld 文件（请确认系统为 Windows 10+，且文件为有效的 zip 归档）。";
            return res;
        }
        std::wstring level;
        if (!findLevelDat(tmp, level)) { res.reason = L".mcworld 内未找到 level.dat。"; return res; }
        m_rootDir = tmp;            // 解压根目录含 db/
        m_levelDatPath = level;
        m_tempDir = tmp;            // 关闭窗口时清理
        return validateWorld(level);
    }
    if (_wcsicmp(p.c_str() + (p.size() >= 4 ? p.size() - 4 : 0), L".dat") == 0) {
        // level.dat 文件：直接校验
        m_rootDir = parentDirOf(p);
        m_levelDatPath = p;
        return validateWorld(p);
    }
    // 目录：找 level.dat
    std::wstring level = p;
    if (!level.empty() && level.back() != L'\\' && level.back() != L'/') level += L"\\";
    level += L"level.dat";
    if (GetFileAttributesW(level.c_str()) == INVALID_FILE_ATTRIBUTES) {
        res.reason = L"目录内未找到 level.dat，这不是有效的我的世界世界目录。";
        return res;
    }
    m_rootDir = p;
    m_levelDatPath = level;
    return validateWorld(level);
}

LRESULT CALLBACK McWorldImporter::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // WM_CREATE 在 CreateWindowExW 返回前同步触发，此时 GWLP_USERDATA 尚未设置，
    // 必须从 CREATESTRUCT.lpCreateParams 取出 this 并写入 GWLP_USERDATA。
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        McWorldImporter* self = reinterpret_cast<McWorldImporter*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            if (!self->m_font)
                self->m_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
            // 初始化 2D 地图控件（480×480 直角方形，白边由 WM_PAINT 用 borderOnly 画）
            self->m_viewSquareTheme = MakeWhiteSquareTheme();
            self->m_viewSquare.rect = {{0, 0}, {static_cast<uint32_t>(kW), static_cast<uint32_t>(kW)}};
            self->m_viewSquare.radius = 0.0f;   // 直角（方形，无圆角）
            self->m_viewSquare.machine.state = ButtonState::Normal;
            self->m_viewSquareSm.state = ButtonState::Normal;
            for (int i = 0; i < 4; ++i) {
                self->m_viewSquare.color[i]  = self->m_viewSquareTheme.normal[i];
                self->m_viewSquare.border[i] = self->m_viewSquareTheme.normal[i];
            }
        }
        return 0;
    }
    McWorldImporter* self = reinterpret_cast<McWorldImporter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        // 固定窗口尺寸（500×600 不可缩放）：最小=最大=初始尺寸
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            RECT rc{ 0, 0, kW, kH };
            AdjustWindowRect(&rc, GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE);
            const int w = rc.right - rc.left, h = rc.bottom - rc.top;
            mmi->ptMinTrackSize.x = w; mmi->ptMinTrackSize.y = h;
            mmi->ptMaxTrackSize.x = w; mmi->ptMaxTrackSize.y = h;
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT full; GetClientRect(hwnd, &full);
            const int cw = full.right - full.left, ch = full.bottom - full.top;
            // 双缓冲：惰性缓存 DC（尺寸变化时重建）
            if (!self->m_db.dc || self->m_db.w != cw || self->m_db.h != ch) {
                gdi::FreeDoubleBuffer(self->m_db);
                self->m_db = gdi::CreateDoubleBuffer(hdc, cw, ch);
            }
            HDC mem = self->m_db.dc;
            HBRUSH bg = CreateSolidBrush(kMcBg); FillRect(mem, &full, bg); DeleteObject(bg);
            if (self->m_font) SelectObject(mem, self->m_font);
            SetBkMode(mem, TRANSPARENT);

            if (self->m_loading) {
                // ---- loading 状态：窗口中央显示进度条 ----
                SetTextColor(mem, kMcTextDim);
                RECT msgRect{ 0, ch / 2 - 40, cw, ch / 2 - 10 };
                DrawTextW(mem, L"正在读取地图数据…", -1, &msgRect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                // 进度条背景
                const int barW = std::min(300, cw - 80), barH = ui::g_theme.slider.h;
                const int barX = (cw - barW) / 2, barY = ch / 2;
                RECT barBg{ barX, barY, barX + barW, barY + barH };
                HBRUSH barBgBr = CreateSolidBrush(kMcTrack);
                FillRect(mem, &barBg, barBgBr); DeleteObject(barBgBr);
                // 进度条填充（从 g_mcProgress 读取）
                int prog = 0;
                if (g_mcProgress >= 0 && g_mcProgress <= 100)
                    prog = g_mcProgress.load();
                if (prog > 0) {
                    const int fillW = barW * prog / 100;
                    RECT barFill{ barX, barY, barX + fillW, barY + barH };
                    HBRUSH barFgBr = CreateSolidBrush(kMcAccent);
                    FillRect(mem, &barFill, barFgBr); DeleteObject(barFgBr);
                }
                // 百分比文字
                SetTextColor(mem, kMcAccentHover);
                wchar_t pct[16];
                swprintf(pct, 16, L"%d%%", prog);
                RECT pctRect{ 0, barY + barH + 6, cw, barY + barH + 30 };
                DrawTextW(mem, pct, -1, &pctRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                // ---- 正常状态：2D 地图控件 = 480×480 方形地图 + 1px 白色直角白边 ----
                // 位置：水平居中、垂直偏上 40px
                const int sqSize = 480;
                const int sqX = (cw - sqSize) / 2;
                const int sqY = 40;
                // 1) 方形地图（地图窗口专属管线绘制，无圆形裁剪）
                if (self->m_mapPipe.valid()) {
                    self->m_mapPipe.Draw(mem, sqX, sqY);
                } else {
                    // 地图未就绪：深色方形占位
                    HBRUSH ph = CreateSolidBrush(RGB(0, 0, 0));
                    RECT pr{ sqX, sqY, sqX + sqSize, sqY + sqSize };
                    FillRect(mem, &pr, ph);
                    DeleteObject(ph);
                }
                // 2) 1px 白色直角边框（走按钮渲染管线 borderOnly，radius=0 直角）
                self->m_viewSquare.rect = {{sqX, sqY}, {static_cast<uint32_t>(sqSize), static_cast<uint32_t>(sqSize)}};
                self->m_viewSquare.radius = 0.0f;
                self->m_viewSquare.border[0] = 1.0f; self->m_viewSquare.border[1] = 1.0f;
                self->m_viewSquare.border[2] = 1.0f; self->m_viewSquare.border[3] = 1.0f;
                DrawGdiButton(mem, self->m_viewSquare, self->m_viewSquareTheme,
                              nullptr, 0, nullptr, RGB(255, 255, 255), /*borderOnly*/ true);
            }  // end else (正常状态)
            BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            // 必须先恢复 owner 再销毁窗口：否则系统销毁活动窗口时选择下一个
            // 激活窗口，会跳过仍被禁用的主窗口，把激活权交给后面的软件（突然蹦出来）
            if (self) {
                HWND owner = self->m_owner;
                if (owner && IsWindow(owner)) EnableWindow(owner, TRUE);
                DestroyWindow(hwnd);
                if (owner && IsWindow(owner)) SetForegroundWindow(owner);
            } else {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            // 激活恢复已由 WM_CLOSE / CloseWindow 完成（先 Enable 后 Destroy 的正确顺序），
            // 这里只做资源清理
            gdi::FreeDoubleBuffer(self->m_db);  // 释放缓存的双缓冲 DC
            if (!self->m_tempDir.empty()) {     // 清理 .mcworld 解压临时目录
                RemoveDirRecursive(self->m_tempDir);
                self->m_tempDir.clear();
            }
            self->m_owner = nullptr;
            self->m_wnd = nullptr;
            return 0;
        case WM_TIMER:
            // timer 3：loading 态进度条动画（持续重绘直到加载完成停表）
            if (wParam == 3 && self && !self->m_loading)
                KillTimer(hwnd, 3);   // 加载完毕，停止定时器
            else if (wParam == 3)
                InvalidateRect(hwnd, nullptr, FALSE);   // 刷新进度条
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// 阶段一：从世界根目录收集真实信息（世界名 / 种子 / 出生点 / db 统计）
// 数据源：levelname.txt（UTF-8 文本）、level.dat 的 NBT（levelSeed / SpawnX·Y·Z / LevelName）、db/ 文件夹
void McWorldImporter::CollectInfo(const std::wstring& rootDir) {
    m_worldName.clear(); m_seed.clear(); m_spawn.clear();
    m_dbCount = 0; m_dbBytes = 0;
    if (rootDir.empty()) return;

    // 世界名：优先 levelname.txt（UTF-8），否则 level.dat 的 LevelName
    std::wstring nl = rootDir + L"\\levelname.txt";
    if (GetFileAttributesW(nl.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::vector<uint8_t> b;
        if (readFileBytes(nl, b) && !b.empty()) {
            m_worldName = s2wMC(std::string((const char*)b.data(), b.size()));
            while (!m_worldName.empty() &&
                   (m_worldName.back() == L'\r' || m_worldName.back() == L'\n' || m_worldName.back() == L' '))
                m_worldName.pop_back();
        }
    }

    // level.dat：种子 / 出生点 / 回退世界名
    std::wstring lvl = m_levelDatPath.empty() ? std::wstring() : m_levelDatPath;
    if (lvl.empty()) findLevelDat(rootDir, lvl);
    if (!lvl.empty()) {
        std::vector<uint8_t> buf;
        if (readFileBytes(lvl, buf)) {
            NbtPtr root = parseLevelDat(buf.data(), buf.size());
            if (root) {
                if (m_worldName.empty()) {
                    if (auto* t = findTag(root.get(), "LevelName"))
                        m_worldName = s2wMC(t->sVal);
                }
                if (auto* t = findTag(root.get(), "levelSeed"))
                    m_seed = std::to_wstring(t->iVal);
                bool ok = false; int64_t sx = 0, sy = 0, sz = 0;
                if (auto* t = findTag(root.get(), "SpawnX")) { sx = t->iVal; ok = true; }
                if (auto* t = findTag(root.get(), "SpawnY")) sy = t->iVal;
                if (auto* t = findTag(root.get(), "SpawnZ")) sz = t->iVal;
                if (ok) {
                    m_spawn = L"(" + std::to_wstring(sx) + L", " + std::to_wstring(sy) +
                              L", " + std::to_wstring(sz) + L")";
                }
            }
        }
    }

    // db/ 统计（真实地图数据所在：LevelDB 数据库）
    std::wstring db = rootDir + L"\\db";
    if (GetFileAttributesW(db.c_str()) != INVALID_FILE_ATTRIBUTES)
        StatDir(db, m_dbCount, m_dbBytes);
}

HWND McWorldImporter::OpenWindow(HWND owner, const wchar_t* versionText) {
    if (m_wnd && IsWindow(m_wnd)) { SetForegroundWindow(m_wnd); return m_wnd; }
    m_ver = versionText ? versionText : L"";
    if (!m_loading)
        CollectInfo(m_rootDir);   // 阶段一：窗口打开前收集真实信息（loading 时稍后调用）
    HICON hAppIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));  // 复用主程序图标
    if (!m_registered) {
        // 复用共享 GDI 窗口类注册（消除逐字复制的 WNDCLASSEXW 脚手架）
        gdi::RegisterWindowClass(L"awaMcWorldWindow", WndProc, kMcBg, hAppIcon, hAppIcon);
        m_registered = true;
    }
    // 不可缩放：去掉 THICKFRAME(拉伸边框)/最小化/最大化；仅保留标题栏+关闭按钮
    constexpr DWORD kMcStyle = WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
    RECT rc{ 0, 0, kW, kH };
    AdjustWindowRect(&rc, kMcStyle, FALSE);
    HWND w = CreateWindowExW(0, L"awaMcWorldWindow", L"awa - 我的世界地图",
                             kMcStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             owner, nullptr, GetModuleHandleW(nullptr), this);
    if (!w) return nullptr;
    SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_wnd = w;
    m_owner = owner;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(w, SW_SHOW);
    UpdateWindow(w);
    // loading 态：启动 30ms 定时器驱动进度条重绘
    if (m_loading)
        SetTimer(w, 3, 30, nullptr);
    return w;
}

void McWorldImporter::CloseWindow() {
    if (m_wnd && IsWindow(m_wnd)) {
        // 先恢复 owner 再销毁（同 WM_CLOSE 顺序）：避免激活权跳到后面的软件
        if (m_owner && IsWindow(m_owner)) EnableWindow(m_owner, TRUE);
        HWND owner = m_owner;
        DestroyWindow(m_wnd);
        if (owner && IsWindow(owner)) SetForegroundWindow(owner);
    }
    m_wnd = nullptr;
    // 状态机复位
    m_viewSquare.machine.state = ButtonState::Normal;
    m_viewSquareSm.state = ButtonState::Normal;
}

// 后台线程：验证(.mcworld解压+level.dat解析) + 收集真实信息 + 后台构建 3D 场景
DWORD WINAPI McWorldImporter::_LoadThread(LPVOID param) {
    McWorldImporter* self = reinterpret_cast<McWorldImporter*>(param);
    g_mcProgress = 0;     // 明确归零（窗口首帧已因 BeginLoad 前置归零）
    g_mcProgress = 10;    // 开始验证
    Validation res = self->Validate(self->m_loadPath.c_str());
    if (res.valid) {
        g_mcProgress = 45;
        self->UpdateVersion(res.versionText.c_str());
        self->LoadInfo();     // 收集世界名/种子/出生点/db 统计
        g_mcProgress = 70;
        // 后台线程构建 3D 场景（CPU 密集，移出主线程避免进度条卡顿）
        self->m_builtReady = false;
        if (McBuildDemoWorld(self->m_builtObj, &self->m_builtGrid, &self->m_builtAtlas)) {
            g_mcProgress = 96;
            // 俯视 2D 地图（方形 480×480，地图管线渲染，未来数据变化可再次 Build 动态更新）
            self->m_mapPipe.Build(self->m_builtGrid, self->m_builtAtlas, 480, 480);
            self->m_builtReady = true;
        }
    }
    self->m_lastValidation = res;
    // 验证成功且场景构建完成才发 wParam=1（否则主线程走失败报错路径）
    if (HWND owner = self->m_owner; owner && IsWindow(owner))
        PostMessageW(owner, WM_APP + 2, (res.valid && self->m_builtReady) ? 1 : 0, 0);
    return 0;
}

// 异步加载入口：先开窗(loading进度条，从0%开始) → 后台线程干活 → 主线程 WM_APP+2 收尾
void McWorldImporter::BeginLoad(HWND owner, const std::wstring& path) {
    if (m_wnd && IsWindow(m_wnd)) { SetForegroundWindow(m_wnd); return; }
    m_loadPath = path;
    m_loading = true;
    m_lastValidation = Validation{};
    g_mcProgress = 0;     // 关键：窗口首帧前归零，修复"进度条默认从100%开始"
    OpenWindow(owner, L"");   // 开窗即 loading 态：中央显示进度条
    if (m_wnd) {
        HANDLE th = CreateThread(nullptr, 0, _LoadThread, this, 0, nullptr);
        if (th) CloseHandle(th);   // 分离线程，后台运行
    }
}
