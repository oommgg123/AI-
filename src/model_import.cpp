// ============================================================================
//   3D 模型导入实现：STL（二进制/ASCII）、glTF/GLB、FBX（ASCII/二进制 7500+）
//   用户 176 轮要求。无第三方库：glTF 用手写轻量 JSON 解析器。
// ============================================================================
#include "model_import.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <atomic>

// main.cpp 提供的全局开关（OBJ 解析使用；与 import_pipeline.cpp 声明一致）
extern std::atomic<int> g_importProgress;
extern bool g_swapZtoY;   // OBJ Z-up → Y-up（用户 167 轮）
extern bool g_useQuads;   // OBJ quad 渲染（用户 177 轮）

namespace {

// ---------------- 通用工具 ----------------

std::vector<uint8_t> ReadWholeFile(const wchar_t* path) {
    std::vector<uint8_t> data;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart <= 0x7FFFFFFFLL) {
        data.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) ||
            got != data.size()) {
            data.clear();
        }
    }
    CloseHandle(h);
    return data;
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// base64 解码（glTF data URI）
std::vector<uint8_t> Base64Decode(const std::string& in) {
    static const std::array<int8_t, 256> T = [] {
        std::array<int8_t, 256> t{};
        t.fill(-1);
        const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) t[static_cast<uint8_t>(A[i])] = static_cast<int8_t>(i);
        return t;
    }();
    std::vector<uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    int val = 0, bits = 0;
    for (char ch : in) {
        if (ch == '=') break;
        const int v = T[static_cast<uint8_t>(ch)];
        if (v < 0) continue;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------- 轻量 JSON 解析器（递归下降，保序对象） ----------------

struct JsonValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* Find(const char* key) const {
        for (const auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

bool JsonSkipWs(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p < end;
}

bool JsonParseValue(const char*& p, const char* end, JsonValue& out);

bool JsonParseString(const char*& p, const char* end, std::string& out) {
    if (p >= end || *p != '"') return false;
    ++p;
    out.clear();
    while (p < end) {
        const char c = *p++;
        if (c == '"') return true;
        if (c == '\\') {
            if (p >= end) return false;
            const char e = *p++;
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '/': out += '/'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case 'u': {
                    if (p + 4 > end) return false;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = p[i];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                    }
                    p += 4;
                    // 仅支持 BMP 码点（模型文件名很少用代理对；UTF-8 编码 BMP）
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        } else {
            out += c;
        }
    }
    return false;
}

bool JsonParseValue(const char*& p, const char* end, JsonValue& out) {
    if (!JsonSkipWs(p, end)) return false;
    const char c = *p;
    if (c == '{') {
        ++p;
        out.type = JsonValue::Obj;
        if (!JsonSkipWs(p, end)) return false;
        if (*p == '}') { ++p; return true; }
        for (;;) {
            if (!JsonSkipWs(p, end) || *p != '"') return false;
            std::string key;
            if (!JsonParseString(p, end, key)) return false;
            if (!JsonSkipWs(p, end) || *p != ':') return false;
            ++p;
            JsonValue v;
            if (!JsonParseValue(p, end, v)) return false;
            out.obj.emplace_back(std::move(key), std::move(v));
            if (!JsonSkipWs(p, end)) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return false;
        }
    }
    if (c == '[') {
        ++p;
        out.type = JsonValue::Arr;
        if (!JsonSkipWs(p, end)) return false;
        if (*p == ']') { ++p; return true; }
        for (;;) {
            JsonValue v;
            if (!JsonParseValue(p, end, v)) return false;
            out.arr.push_back(std::move(v));
            if (!JsonSkipWs(p, end)) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return true; }
            return false;
        }
    }
    if (c == '"') {
        out.type = JsonValue::Str;
        return JsonParseString(p, end, out.str);
    }
    if (c == 't' && end - p >= 4 && std::memcmp(p, "true", 4) == 0) { p += 4; out.type = JsonValue::Bool; out.b = true; return true; }
    if (c == 'f' && end - p >= 5 && std::memcmp(p, "false", 5) == 0) { p += 5; out.type = JsonValue::Bool; out.b = false; return true; }
    if (c == 'n' && end - p >= 4 && std::memcmp(p, "null", 4) == 0) { p += 4; out.type = JsonValue::Null; return true; }
    // 数字
    const char* start = p;
    while (p < end && (std::strchr("0123456789+-.eE", *p))) ++p;
    if (p == start) return false;
    out.type = JsonValue::Num;
    out.num = std::strtod(start, nullptr);
    return true;
}

bool JsonParse(const std::string& s, JsonValue& out) {
    const char* p = s.data();
    const char* end = s.data() + s.size();
    return JsonParseValue(p, end, out);
}

// ---------------- 共享：三角网格 → SceneObject ----------------
// pos：世界坐标（会被中心化+贴地归一化）；tris：每 3 个顶点索引一组；
// fileNrm：可选，每顶点文件法线（glTF 用；长度须 == pos.size()）

bool BuildSceneObject(std::vector<std::array<float, 3>>& pos,
                      const std::vector<int32_t>& tris,
                      const std::vector<std::array<float, 3>>* fileNrm,
                      const std::vector<std::vector<int32_t>>* faces,
                      SceneObject& out) {
    if (pos.empty() || tris.size() < 3) return false;
    // 归一化：XZ 中心化 + 最低点贴网格（y→0）
    float minX = pos[0][0], maxX = pos[0][0], minY = pos[0][1], maxY = pos[0][1];
    float minZ = pos[0][2], maxZ = pos[0][2];
    for (const auto& p : pos) {
        minX = std::min(minX, p[0]); maxX = std::max(maxX, p[0]);
        minY = std::min(minY, p[1]); maxY = std::max(maxY, p[1]);
        minZ = std::min(minZ, p[2]); maxZ = std::max(maxZ, p[2]);
    }
    const float cx = (minX + maxX) * 0.5f, cz = (minZ + maxZ) * 0.5f;
    for (auto& p : pos) { p[0] -= cx; p[1] -= minY; p[2] -= cz; }
    const int npos = static_cast<int>(pos.size());

    // 线框：去重边。优先原始面（faces）环形连边→四边形只画 4 边、不含三角对角线；
    // 无 faces（STL/glTF 天然三角面）回退从三角化 tris 提边（三角边属正常形态）。
    std::unordered_set<uint64_t> edgeSet;
    edgeSet.reserve(tris.size() * 2);
    const auto addEdge = [&](int p, int q) {
        if (p == q) return;
        const uint64_t key = (p < q) ? ((static_cast<uint64_t>(p)) << 32) | static_cast<uint32_t>(q)
                                     : ((static_cast<uint64_t>(q)) << 32) | static_cast<uint32_t>(p);
        if (edgeSet.insert(key).second) {
            out.wireIndices.push_back(static_cast<uint32_t>(p));
            out.wireIndices.push_back(static_cast<uint32_t>(q));
        }
    };
    if (faces && !faces->empty()) {
        for (const auto& f : *faces) {
            const int m = static_cast<int>(f.size());
            for (int k = 0; k < m; ++k) {
                const int p = f[k], q = f[(k + 1) % m];
                if (p < 0 || q < 0 || p >= npos || q >= npos) continue;
                addEdge(p, q);   // 仅环形边：四边形不含内部 0-2 对角线
            }
        }
    } else {
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const int a = tris[i], b = tris[i + 1], c = tris[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= npos || b >= npos || c >= npos) continue;
            addEdge(a, b); addEdge(b, c); addEdge(c, a);
        }
    }
    out.wireVerts.reserve(static_cast<size_t>(npos));
    for (int i = 0; i < npos; ++i) {
        VertexSolid v{};
        v.pos[0] = pos[i][0]; v.pos[1] = pos[i][1]; v.pos[2] = pos[i][2];
        out.wireVerts.push_back(v);
    }

    // 实体：面法线（或文件法线）逐顶点去重
    out.solidVerts.reserve(static_cast<size_t>(npos));
    out.solidIndices.reserve(tris.size());
    std::unordered_map<uint64_t, uint32_t> vmap;
    vmap.reserve(static_cast<size_t>(npos) * 2);
    const float gray[4] = {0.72f, 0.72f, 0.74f, 1.0f};
    const auto emitVertex = [&](int vi, float nx, float ny, float nz) -> uint32_t {
        const uint8_t qnx = static_cast<uint8_t>(static_cast<int8_t>(nx * 127.0f));
        const uint8_t qny = static_cast<uint8_t>(static_cast<int8_t>(ny * 127.0f));
        const uint8_t qnz = static_cast<uint8_t>(static_cast<int8_t>(nz * 127.0f));
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(vi)) << 32) |
                             (static_cast<uint64_t>(qnx) << 16) |
                             (static_cast<uint64_t>(qny) << 8) | qnz;
        auto it = vmap.find(key);
        if (it != vmap.end()) return it->second;
        const uint32_t idx = static_cast<uint32_t>(out.solidVerts.size());
        VertexSolid v{};
        v.pos[0] = pos[vi][0]; v.pos[1] = pos[vi][1]; v.pos[2] = pos[vi][2];
        v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
        std::memcpy(v.color, gray, sizeof(gray));
        out.solidVerts.push_back(v);
        vmap.emplace(key, idx);
        return idx;
    };
    const bool hasFileNrm = (fileNrm && fileNrm->size() == pos.size());
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const int a = tris[i], b = tris[i + 1], c = tris[i + 2];
        if (a < 0 || b < 0 || c < 0 || a >= npos || b >= npos || c >= npos) continue;
        const auto& pa = pos[a]; const auto& pb = pos[b]; const auto& pc = pos[c];
        if (hasFileNrm) {
            out.solidIndices.push_back(emitVertex(a, (*fileNrm)[a][0], (*fileNrm)[a][1], (*fileNrm)[a][2]));
            out.solidIndices.push_back(emitVertex(b, (*fileNrm)[b][0], (*fileNrm)[b][1], (*fileNrm)[b][2]));
            out.solidIndices.push_back(emitVertex(c, (*fileNrm)[c][0], (*fileNrm)[c][1], (*fileNrm)[c][2]));
        } else {
            float ux = pb[0]-pa[0], uy = pb[1]-pa[1], uz = pb[2]-pa[2];
            float vx = pc[0]-pa[0], vy = pc[1]-pa[1], vz = pc[2]-pa[2];
            float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
            const float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (nl > 1e-9f) { nx /= nl; ny /= nl; nz /= nl; } else { nx = 0; ny = 1; nz = 0; }
            out.solidIndices.push_back(emitVertex(a, nx, ny, nz));
            out.solidIndices.push_back(emitVertex(b, nx, ny, nz));
            out.solidIndices.push_back(emitVertex(c, nx, ny, nz));
        }
    }
    out.solidIndices.shrink_to_fit();
    return !out.solidIndices.empty();
}

// ---------------- STL ----------------

bool ParseSTL(const wchar_t* path, SceneObject& out) {
    const std::vector<uint8_t> data = ReadWholeFile(path);
    if (data.empty()) return false;
    std::vector<std::array<float, 3>> pos;
    std::vector<int32_t> tris;
    const std::string_view sv(reinterpret_cast<const char*>(data.data()), data.size());
    // 判定 ASCII：以 "solid" 开头且内容含 "facet" 文本 → 文本格式
    const bool ascii = sv.size() >= 32 && sv.substr(0, 5) == "solid" && sv.find("facet") != std::string_view::npos;

    if (ascii) {
        size_t p = 0;
        while ((p = sv.find("vertex", p)) != std::string_view::npos) {
            p += 6;
            // 读 3 个数字（支持科学计数法）
            float x = 0, y = 0, z = 0;
            int matched = 0;
            for (int k = 0; k < 3; ++k) {
                while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t' || sv[p] == '\r' || sv[p] == '\n')) ++p;
                char* endp = nullptr;
                const double v = std::strtod(sv.data() + p, &endp);
                if (endp == sv.data() + p) break;
                if (k == 0) x = static_cast<float>(v);
                else if (k == 1) y = static_cast<float>(v);
                else z = static_cast<float>(v);
                p = static_cast<size_t>(endp - sv.data());
                ++matched;
            }
            if (matched == 3) pos.push_back({x, y, z});
        }
        // 每 3 个顶点一个面
        for (size_t i = 0; i + 2 < pos.size(); i += 3) {
            tris.push_back(static_cast<int32_t>(i));
            tris.push_back(static_cast<int32_t>(i + 1));
            tris.push_back(static_cast<int32_t>(i + 2));
        }
    } else {
        // 二进制：80B 头 + uint32 面数 + 每面 50B（12 float + 2B attr）
        if (data.size() < 84) return false;
        uint32_t faceCount = 0;
        std::memcpy(&faceCount, data.data() + 80, 4);
        if (faceCount > 20000000) return false;  // 防异常
        if (data.size() < 84ull + static_cast<uint64_t>(faceCount) * 50) return false;
        pos.reserve(static_cast<size_t>(faceCount) * 3);
        tris.reserve(static_cast<size_t>(faceCount) * 3);
        for (uint32_t f = 0; f < faceCount; ++f) {
            const size_t off = 84ull + static_cast<uint64_t>(f) * 50 + 12;  // 跳过法线 12B
            for (int k = 0; k < 3; ++k) {
                float v[3];
                std::memcpy(v, data.data() + off + static_cast<size_t>(k) * 12, 12);
                pos.push_back({v[0], v[1], v[2]});
            }
            const int32_t base = static_cast<int32_t>(f) * 3;
            tris.push_back(base); tris.push_back(base + 1); tris.push_back(base + 2);
        }
    }
    if (pos.size() < 3) return false;
    return BuildSceneObject(pos, tris, nullptr, nullptr, out);
}

// ---------------- glTF / GLB ----------------

// 从 glTF accessor 读取数据到浮点数组
struct GltfMesh {
    std::vector<std::array<float, 3>> pos;
    std::vector<std::array<float, 3>> nrm;  // 可选，与 pos 同长
    std::vector<int32_t> tris;
};

bool GltfReadAccessor(const JsonValue& accessors, const JsonValue& bufferViews,
                      const std::vector<uint8_t>& bin, size_t accessorIndex,
                      std::vector<float>& out, int& outComp, size_t& outCount) {
    if (accessorIndex >= accessors.arr.size()) return false;
    const JsonValue& acc = accessors.arr[accessorIndex];
    const JsonValue* pComp = acc.Find("componentType");
    const JsonValue* pCount = acc.Find("count");
    const JsonValue* pType = acc.Find("type");
    const JsonValue* pBv = acc.Find("bufferView");
    const JsonValue* pByteOff = acc.Find("byteOffset");
    if (!pComp || !pCount || !pType || !pBv) return false;
    const int comp = static_cast<int>(pComp->num);
    const size_t count = static_cast<size_t>(pCount->num);
    outComp = comp;
    outCount = count;
    const size_t byteOff = pByteOff ? static_cast<size_t>(pByteOff->num) : 0;
    const size_t bvIdx = static_cast<size_t>(pBv->num);
    if (bvIdx >= bufferViews.arr.size()) return false;
    const JsonValue& bv = bufferViews.arr[bvIdx];
    const JsonValue* pBuf = bv.Find("buffer");
    const JsonValue* pBvOff = bv.Find("byteOffset");
    const JsonValue* pBvLen = bv.Find("byteLength");
    const JsonValue* pStride = bv.Find("byteStride");
    if (!pBuf || !pBvLen) return false;
    const size_t bufOff = pBvOff ? static_cast<size_t>(pBvOff->num) : 0;
    const size_t bufLen = static_cast<size_t>(pBvLen->num);
    const size_t stride = pStride ? static_cast<size_t>(pStride->num) : 0;
    if (static_cast<size_t>(pBuf->num) != 0 || bufOff + bufLen > bin.size()) return false;
    // 类型组件数
    int comps = 0;
    if (std::string(pType->str) == "SCALAR") comps = 1;
    else if (std::string(pType->str) == "VEC2") comps = 2;
    else if (std::string(pType->str) == "VEC3") comps = 3;
    else if (std::string(pType->str) == "VEC4") comps = 4;
    else if (std::string(pType->str) == "MAT4") comps = 16;
    if (comps <= 0) return false;
    out.clear();
    out.reserve(count * static_cast<size_t>(comps));
    const uint8_t* src = bin.data() + bufOff + byteOff;
    const size_t elemSize = stride ? stride : (comp == 5126 ? 4 : comp == 5123 ? 2 : comp == 5125 ? 4 : comp == 5121 ? 1 : 0);
    if (elemSize == 0) return false;
    if (comp == 5126) {  // float
        for (size_t i = 0; i < count; ++i) {
            const float* f = reinterpret_cast<const float*>(src + i * elemSize);
            for (int k = 0; k < comps; ++k) out.push_back(f[k]);
        }
    } else if (comp == 5123) {  // ushort
        for (size_t i = 0; i < count; ++i) {
            const uint16_t* u = reinterpret_cast<const uint16_t*>(src + i * elemSize);
            for (int k = 0; k < comps; ++k) out.push_back(static_cast<float>(u[k]));
        }
    } else if (comp == 5125) {  // uint
        for (size_t i = 0; i < count; ++i) {
            const uint32_t* u = reinterpret_cast<const uint32_t*>(src + i * elemSize);
            for (int k = 0; k < comps; ++k) out.push_back(static_cast<float>(u[k]));
        }
    } else if (comp == 5121) {  // ubyte
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* u = src + i * elemSize;
            for (int k = 0; k < comps; ++k) out.push_back(static_cast<float>(u[k]));
        }
    } else {
        return false;
    }
    return true;
}

bool ParseGLTF(const wchar_t* path, SceneObject& out) {
    const std::vector<uint8_t> data = ReadWholeFile(path);
    if (data.empty()) return false;
    const std::string fname = WideToUtf8(path);
    const size_t dot = fname.find_last_of('.');
    const std::string ext = (dot == std::string::npos) ? "" : fname.substr(dot);

    std::string jsonText;
    std::vector<uint8_t> bin;
    std::string binUri;  // 外部 .bin 相对路径（.gltf 时用）

    if (ext == ".glb") {
        // GLB 容器：12B 头 + chunk（JSON + BIN）
        if (data.size() < 20 || std::memcmp(data.data(), "glTF", 4) != 0) return false;
        size_t p = 12;
        bool gotJson = false;
        while (p + 8 <= data.size()) {
            uint32_t clen = 0, ctype = 0;
            std::memcpy(&clen, data.data() + p, 4);
            std::memcpy(&ctype, data.data() + p + 4, 4);
            p += 8;
            if (p + clen > data.size()) break;
            if (ctype == 0x4E4F534A) {  // "JSON"
                jsonText.assign(reinterpret_cast<const char*>(data.data() + p), clen);
                gotJson = true;
            } else if (ctype == 0x004E4942) {  // "BIN\0"
                bin.assign(data.begin() + static_cast<ptrdiff_t>(p),
                           data.begin() + static_cast<ptrdiff_t>(p + clen));
            }
            p += clen;
        }
        if (!gotJson) return false;
    } else {
        // .gltf：JSON 文本；buffer 可能是 data URI 或外部 .bin
        jsonText.assign(reinterpret_cast<const char*>(data.data()), data.size());
    }

    JsonValue root;
    if (!JsonParse(jsonText, root) || root.type != JsonValue::Obj) return false;

    // 解析 buffers → 取 buffer 0 内容
    if (const JsonValue* bufs = root.Find("buffers"); bufs && !bufs->arr.empty()) {
        const JsonValue& b0 = bufs->arr[0];
        if (const JsonValue* uri = b0.Find("uri")) {
            const std::string& u = uri->str;
            if (u.rfind("data:", 0) == 0) {
                const size_t comma = u.find(',');
                if (comma != std::string::npos) bin = Base64Decode(u.substr(comma + 1));
            } else if (bin.empty()) {
                binUri = u;
            }
        }
    }
    if (binUri.empty() && bin.empty()) return false;
    if (!binUri.empty() && bin.empty()) {
        // 相对路径 .bin
        std::wstring binPath = path;
        const size_t slash = binPath.find_last_of(L"/\\");
        binPath = (slash == std::wstring::npos) ? L"" : binPath.substr(0, slash + 1);
        std::string uriA = binUri;
        std::replace(uriA.begin(), uriA.end(), '/', '\\');
        binPath += std::wstring(uriA.begin(), uriA.end());
        bin = ReadWholeFile(binPath.c_str());
        if (bin.empty()) {
            // 尝试 UTF-8 → 宽字符
            binPath = path;
            binPath = (slash == std::wstring::npos) ? L"" : binPath.substr(0, slash + 1);
            const int wlen = MultiByteToWideChar(CP_UTF8, 0, binUri.c_str(), -1, nullptr, 0);
            std::wstring wuri(static_cast<size_t>(wlen > 0 ? wlen - 1 : 0), L'\0');
            if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, binUri.c_str(), -1, wuri.data(), wlen);
            bin = ReadWholeFile((binPath + wuri).c_str());
        }
        if (bin.empty()) return false;
    }

    const JsonValue* accessors = root.Find("accessors");
    const JsonValue* bufferViews = root.Find("bufferViews");
    const JsonValue* meshes = root.Find("meshes");
    if (!accessors || !bufferViews || !meshes || meshes->arr.empty()) return false;

    // 取第一个 mesh 的第一个 primitive
    bool found = false;
    for (const auto& m : meshes->arr) {
        const JsonValue* prims = m.Find("primitives");
        if (!prims || prims->arr.empty()) continue;
        const JsonValue& prim = prims->arr[0];
        const JsonValue* attrs = prim.Find("attributes");
        if (!attrs) continue;
        std::vector<float> pdata, ndata, idata;
        int pcomp = 0, icomp = 0;
        size_t pcount = 0, icount = 0;
        bool ok = true;
        if (const JsonValue* posA = attrs->Find("POSITION"))
            ok = ok && GltfReadAccessor(*accessors, *bufferViews, bin,
                                        static_cast<size_t>(posA->num), pdata, pcomp, pcount);
        if (const JsonValue* idxA = prim.Find("indices"))
            ok = ok && GltfReadAccessor(*accessors, *bufferViews, bin,
                                        static_cast<size_t>(idxA->num), idata, icomp, icount);
        if (!ok || pdata.empty() || pcount < 3) continue;
        // 顶点
        std::vector<std::array<float, 3>> pos;
        pos.reserve(pcount);
        for (size_t i = 0; i < pcount; ++i) {
            const size_t k = i * 3;
            if (k + 2 >= pdata.size()) break;
            pos.push_back({pdata[k], pdata[k + 1], pdata[k + 2]});
        }
        // 索引
        std::vector<int32_t> tris;
        if (!idata.empty()) {
            tris.reserve(icount);
            for (size_t i = 0; i < icount; ++i) tris.push_back(static_cast<int32_t>(idata[i]));
        } else {
            for (size_t i = 0; i < pcount; ++i) tris.push_back(static_cast<int32_t>(i));
        }
        // 可选法线
        std::vector<std::array<float, 3>> nrm;
        if (const JsonValue* nrmA = attrs->Find("NORMAL")) {
            std::vector<float> nd;
            int ncomp = 0;
            size_t ncount = 0;
            if (GltfReadAccessor(*accessors, *bufferViews, bin,
                                 static_cast<size_t>(nrmA->num), nd, ncomp, ncount) && ncount == pcount) {
                nrm.reserve(ncount);
                for (size_t i = 0; i < ncount; ++i) {
                    const size_t k = i * 3;
                    if (k + 2 >= nd.size()) break;
                    nrm.push_back({nd[k], nd[k + 1], nd[k + 2]});
                }
            }
        }
        if (BuildSceneObject(pos, tris, nrm.empty() ? nullptr : &nrm, nullptr, out)) {
            found = true;
            if (const JsonValue* name = m.Find("name")) out.name.assign(name->str.begin(), name->str.end());
            break;
        }
    }
    return found;
}

// ---------------- FBX ----------------

// 二进制 FBX 数组属性（未压缩）：直接引用文件 buffer 原始数据
struct FbxArray {
    char type = 0;            // 'd'/'f'/'i'/'l'/'b'
    uint32_t count = 0;
    const uint8_t* data = nullptr;  // 指向文件 buffer（base + 偏移）
};

struct FbxNode {
    std::string name;
    std::vector<std::string> props;  // 标量属性（S/I/F/D/L/C/Y）
    std::vector<FbxArray> arrays;    // 数组属性（仅未压缩；压缩数组跳过）
    std::vector<FbxNode> children;
};

// 递归读取二进制 FBX 节点（FBX 7500+ 节点树格式）
bool FbxReadNode(const uint8_t* base, const uint8_t* bend, size_t& p, FbxNode& out, int depth) {
    if (depth > 16 || p + 13 > static_cast<size_t>(bend - base)) return false;
    const uint8_t* cur = base + p;
    uint32_t endOffset = 0, numProps = 0, propListLen = 0;
    std::memcpy(&endOffset, cur, 4);
    std::memcpy(&numProps, cur + 4, 4);
    std::memcpy(&propListLen, cur + 8, 4);
    const uint8_t nameLen = cur[12];
    const size_t headEnd = p + 13 + nameLen;
    if (endOffset > static_cast<uint32_t>(bend - base) || headEnd > endOffset) return false;
    out.name.assign(reinterpret_cast<const char*>(cur + 13), nameLen);
    size_t q = headEnd;
    const size_t propAreaEnd = headEnd + propListLen;
    // 解析属性
    while (q + 1 <= propAreaEnd && out.props.size() + out.arrays.size() < numProps) {
        const char tc = static_cast<char>(base[q]);
        ++q;
        auto readFixed = [&](size_t n) -> std::string {
            if (q + n > propAreaEnd) return {};
            std::string v(reinterpret_cast<const char*>(base + q), n);
            q += n;
            return v;
        };
        if (tc == 'S' || tc == 'R') {
            if (q + 4 > propAreaEnd) break;
            uint32_t len = 0;
            std::memcpy(&len, base + q, 4);
            q += 4;
            if (q + len > propAreaEnd) break;
            out.props.emplace_back(reinterpret_cast<const char*>(base + q), len);
            q += len;
        } else if (tc == 'Y') { out.props.push_back(readFixed(2)); }
        else if (tc == 'C') { out.props.push_back(readFixed(1)); }
        else if (tc == 'I') { out.props.push_back(readFixed(4)); }
        else if (tc == 'F') { out.props.push_back(readFixed(4)); }
        else if (tc == 'D') { out.props.push_back(readFixed(8)); }
        else if (tc == 'L') { out.props.push_back(readFixed(8)); }
        else if (tc == 'f' || tc == 'd' || tc == 'l' || tc == 'i' || tc == 'b') {
            if (q + 12 > propAreaEnd) break;
            uint32_t alen = 0, enc = 0, clen = 0;
            std::memcpy(&alen, base + q, 4);
            std::memcpy(&enc, base + q + 4, 4);
            std::memcpy(&clen, base + q + 8, 4);
            q += 12;
            const size_t elemSize = (tc == 'd' || tc == 'l') ? 8 : (tc == 'f' || tc == 'i') ? 4 : 1;
            const size_t rawLen = static_cast<size_t>(alen) * elemSize;
            if (enc == 0) {
                if (q + rawLen > propAreaEnd) break;
                // 完整引用原始数据（几何重建需要全部元素）
                FbxArray a;
                a.type = tc;
                a.count = alen;
                a.data = base + q;
                out.arrays.push_back(a);
                q += rawLen;
            } else {
                // 压缩数组（encoding=1）：跳过
                q += static_cast<size_t>(clen);
                if (q > propAreaEnd) break;
                out.arrays.push_back({tc, alen, nullptr});
            }
        } else {
            break;
        }
    }
    p = endOffset;
    // 子节点
    size_t child = p;
    while (child < endOffset) {
        FbxNode c;
        if (!FbxReadNode(base, bend, child, c, depth + 1)) break;
        out.children.push_back(std::move(c));
    }
    return true;
}

// 从 FbxArray 读取元素（double/int 数组 → float / int32）
void FbxArrayToFloats(const FbxArray& a, std::vector<float>& outF) {
    outF.clear();
    if (!a.data || a.count == 0) return;
    if (a.type == 'd') {
        outF.reserve(a.count);
        for (uint32_t i = 0; i < a.count; ++i) {
            double v; std::memcpy(&v, a.data + static_cast<size_t>(i) * 8, 8);
            outF.push_back(static_cast<float>(v));
        }
    } else if (a.type == 'f') {
        outF.reserve(a.count);
        for (uint32_t i = 0; i < a.count; ++i) {
            float v; std::memcpy(&v, a.data + static_cast<size_t>(i) * 4, 4);
            outF.push_back(v);
        }
    } else if (a.type == 'l') {
        outF.reserve(a.count);
        for (uint32_t i = 0; i < a.count; ++i) {
            int64_t v; std::memcpy(&v, a.data + static_cast<size_t>(i) * 8, 8);
            outF.push_back(static_cast<float>(v));
        }
    } else if (a.type == 'i') {
        outF.reserve(a.count);
        for (uint32_t i = 0; i < a.count; ++i) {
            int32_t v; std::memcpy(&v, a.data + static_cast<size_t>(i) * 4, 4);
            outF.push_back(static_cast<float>(v));
        }
    }
}

void FbxArrayToInts(const FbxArray& a, std::vector<int32_t>& outI) {
    outI.clear();
    if (!a.data || a.count == 0) return;
    outI.reserve(a.count);
    if (a.type == 'i') {
        for (uint32_t i = 0; i < a.count; ++i) {
            int32_t v; std::memcpy(&v, a.data + static_cast<size_t>(i) * 4, 4);
            outI.push_back(v);
        }
    } else if (a.type == 'l') {
        for (uint32_t i = 0; i < a.count; ++i) {
            int64_t v; std::memcpy(&v, a.data + static_cast<size_t>(i) * 8, 8);
            outI.push_back(static_cast<int32_t>(v));
        }
    }
}

bool ParseFBX(const wchar_t* path, SceneObject& out) {
    const std::vector<uint8_t> data = ReadWholeFile(path);
    if (data.empty()) return false;
    // 仅支持二进制 FBX（7500+ 节点树）；ASCII FBX 暂不支持（返回失败走"无法解析"提示）
    if (data.size() <= 27 || std::memcmp(data.data(), "Kaydara FBX Binary", 18) != 0) return false;
    uint32_t fbxVersion = 0;
    std::memcpy(&fbxVersion, data.data() + 23, 4);
    if (fbxVersion < 7000) return false;

    const uint8_t* base = data.data();
    const uint8_t* bend = data.data() + data.size();
    std::vector<FbxNode> roots;
    size_t p = 27;
    while (p + 13 <= data.size()) {
        FbxNode n;
        if (!FbxReadNode(base, bend, p, n, 0)) break;
        roots.push_back(std::move(n));
    }

    // 收集 Geometry 节点（Type 属性含 "Mesh"）
    for (const auto& r : roots) {
        for (const auto& n : r.children) {
            if (n.name != "Geometry") continue;
            bool isMesh = false;
            for (const auto& prop : n.props)
                if (prop.find("Mesh") != std::string::npos) { isMesh = true; break; }
            if (!isMesh) continue;
            std::vector<float> verts;
            std::vector<int32_t> polyIdx;
            for (const auto& c : n.children) {
                if (c.name == "Vertices" && !c.arrays.empty()) {
                    FbxArrayToFloats(c.arrays[0], verts);
                } else if (c.name == "PolygonVertexIndex" && !c.arrays.empty()) {
                    FbxArrayToInts(c.arrays[0], polyIdx);
                }
            }
            if (verts.size() < 9 || polyIdx.empty()) continue;
            // 顶点 → 数组
            std::vector<std::array<float, 3>> pos;
            pos.reserve(verts.size() / 3);
            for (size_t i = 0; i + 2 < verts.size(); i += 3)
                pos.push_back({verts[i], verts[i + 1], verts[i + 2]});
            // 多边形索引（负值 = -idx-1 表示多边形结束）→ 三角化 fan
            std::vector<int32_t> tris;
            std::vector<std::vector<int32_t>> faces;   // Round353：保留原始多边形（用于四边线框）
            const size_t nPos = pos.size();
            size_t i = 0;
            while (i < polyIdx.size()) {
                std::vector<int32_t> poly;
                while (i < polyIdx.size()) {
                    const int32_t v = polyIdx[i++];
                    if (v < 0) { poly.push_back(-v - 1); break; }
                    poly.push_back(v);
                }
                if (poly.size() >= 3) {
                    for (size_t k = 1; k + 1 < poly.size(); ++k) {
                        const int a = poly[0], b = poly[k], c2 = poly[k + 1];
                        if (a < 0 || b < 0 || c2 < 0 ||
                            a >= static_cast<int>(nPos) || b >= static_cast<int>(nPos) || c2 >= static_cast<int>(nPos)) continue;
                        tris.push_back(a); tris.push_back(b); tris.push_back(c2);
                    }
                    faces.push_back(poly);   // 原始多边形（含四边形）→ 线框只画环形边
                }
            }
            if (tris.empty()) continue;
            if (BuildSceneObject(pos, tris, nullptr, &faces, out)) {
                out.name = L"FBX 模型";
                return true;
            }
        }
    }
    return false;
}

}  // namespace

// ---------------- 公开入口 ----------------

bool ImportSTL(const wchar_t* path, SceneObject& out) {
    return ParseSTL(path, out);
}

bool ImportGLTF(const wchar_t* path, SceneObject& out) {
    return ParseGLTF(path, out);
}

bool ImportFBX(const wchar_t* path, SceneObject& out) {
    return ParseFBX(path, out);
}

// ---------------- OBJ 解析（自 main.cpp 迁入，用户 177 轮）----------------

bool ParseOBJ(const wchar_t* path, SceneObject& out) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(hFile, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 0x7FFFFFFFLL) {
        CloseHandle(hFile);
        return false;
    }
    std::string content(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD got = 0;
    if (!ReadFile(hFile, content.data(), static_cast<DWORD>(content.size()), &got, nullptr) ||
        got != content.size()) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    std::vector<std::array<float, 3>> pos;
    std::vector<int> faceIdx;
    std::vector<int> faceStart;
    std::vector<std::array<float, 3>> fileVn;  // 文件顶点法线（vn 行）
    pos.reserve(1000000);
    faceIdx.reserve(4000000);
    faceStart.reserve(1000000);
    std::string mtlFile;

    const char* p = content.data();
    const char* const end = p + content.size();
    while (p < end) {
        // 进度：按解析进度 10%→70%（每 64KB 更新一次，避免每行 atomic 开销）
        if (g_importProgress >= 0 && ((p - content.data()) & 0xFFFF) == 0) {
            const int prog = 10 + static_cast<int>(static_cast<float>(p - content.data()) /
                                                  static_cast<float>(content.size()) * 60.0f);
            if (prog > g_importProgress) g_importProgress = prog;
        }
        const char c = *p;
        if (c == 'm' && p + 1 < end && p[1] == 't' && p + 6 < end &&
            std::strncmp(p, "mtllib", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            if (mtlFile.empty()) {
                p += 7;
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                const char* s = p;
                while (p < end && *p != '\n' && *p != '\r' && *p != ' ' && *p != '\t') ++p;
                mtlFile.assign(s, p);
            }
        } else if (c == 'v' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            char* ep = nullptr;
            const float x = std::strtof(p, &ep); p = ep;
            const float y = std::strtof(p, &ep); p = ep;
            const float z = std::strtof(p, &ep); p = ep;
            pos.push_back({x, y, z});
        } else if (c == 'v' && p + 2 < end && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            // 文件顶点法线（用户 166 轮法线渲染接口）
            p += 3;
            char* ep = nullptr;
            const float x = std::strtof(p, &ep); p = ep;
            const float y = std::strtof(p, &ep); p = ep;
            const float z = std::strtof(p, &ep); p = ep;
            fileVn.push_back({x, y, z});
        } else if (c == 'f' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
            p += 2;
            faceStart.push_back(static_cast<int>(faceIdx.size()));
            while (p < end && *p != '\n' && *p != '\r') {
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                if (p >= end || *p == '\n' || *p == '\r') break;
                char* ep = nullptr;
                const long vi = std::strtol(p, &ep, 10);
                if (ep == p) break;  // 解析失败，结束该面
                faceIdx.push_back(vi > 0 ? static_cast<int>(vi - 1)
                                         : static_cast<int>(pos.size()) + static_cast<int>(vi));
                p = ep;
                while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') ++p;
            }
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
    if (pos.empty() || faceStart.empty()) return false;

    {
        // Z→Y 旋转换算（用户 167 轮）：g_swapZtoY=true 时把 OBJ Z-up 转为 Y-up（+90° 绕 X 轴）
        if (g_swapZtoY) {
            for (auto& pt : pos) {
                const float ny = pt[2];
                const float nz = -pt[1];
                pt[1] = ny;
                pt[2] = nz;
            }
            for (auto& n : fileVn) {
                const float ny = n[2];
                const float nz = -n[1];
                n[1] = ny;
                n[2] = nz;
            }
        }
        out.vn = std::move(fileVn);
        out.hasFileNormals = !out.vn.empty();
        float minX = pos[0][0], maxX = pos[0][0];
        float minY = pos[0][1], maxY = pos[0][1];
        float minZ = pos[0][2], maxZ = pos[0][2];
        for (const auto& pt : pos) {
            if (pt[0] < minX) minX = pt[0];
            if (pt[0] > maxX) maxX = pt[0];
            if (pt[1] < minY) minY = pt[1];
            if (pt[1] > maxY) maxY = pt[1];
            if (pt[2] < minZ) minZ = pt[2];
            if (pt[2] > maxZ) maxZ = pt[2];
        }
        const float cx = (minX + maxX) * 0.5f;
        const float bottomY = minY;  // 渲染 y（pos[1]）最低 → 0（贴网格）
        const float cz = (minZ + maxZ) * 0.5f;
        for (auto& pt : pos) {
            pt[0] -= cx;
            pt[1] -= bottomY;
            pt[2] -= cz;
        }
    }

    if (g_importProgress >= 0) g_importProgress = 72;
    std::unordered_set<uint64_t> edgeSet;
    edgeSet.reserve(faceIdx.size());
    const float totalFaces = static_cast<float>(faceStart.size());
    for (size_t fi = 0; fi < faceStart.size(); ++fi) {
        if (g_importProgress >= 0 && (fi & 0xFF) == 0) {
            const int prog = 72 + static_cast<int>(static_cast<float>(fi) / totalFaces * 8.0f);
            if (prog > g_importProgress) g_importProgress = prog;
        }
        const int s = faceStart[fi];
        const int e = (fi + 1 < faceStart.size()) ? faceStart[fi + 1] : static_cast<int>(faceIdx.size());
        if (e - s < 3) continue;
        for (int i = s; i < e; ++i) {
            int a = faceIdx[i];
            int b = faceIdx[(i + 1 < e) ? (i + 1) : s];
            if (a == b) continue;
            if (a > b) std::swap(a, b);
            edgeSet.insert((static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
                           static_cast<uint32_t>(b));
        }
    }
    if (g_importProgress >= 0) g_importProgress = 80;
    const float wireCol[4] = {0.95f, 0.95f, 0.98f, 1.0f};
    const int npos = static_cast<int>(pos.size());
    out.wireVerts.clear();
    out.wireVerts.reserve(npos);
    for (int i = 0; i < npos; ++i) {
        VertexSolid v{};
        v.pos[0] = pos[i][0]; v.pos[1] = pos[i][1]; v.pos[2] = pos[i][2];
        v.color[0] = wireCol[0]; v.color[1] = wireCol[1]; v.color[2] = wireCol[2]; v.color[3] = 1.0f;
        out.wireVerts.push_back(v);
    }
    out.wireIndices.clear();
    out.wireIndices.reserve(edgeSet.size() * 2);
    for (const uint64_t key : edgeSet) {
        const int a = static_cast<int>(key >> 32);
        const int b = static_cast<int>(key & 0xFFFFFFFFu);
        if (a >= npos || b >= npos) continue;
        out.wireIndices.push_back(static_cast<uint32_t>(a));
        out.wireIndices.push_back(static_cast<uint32_t>(b));
    }

    // 索引总数：quad（4 边面，默认）用 6 索引（2 三角形），三角/多边形 fan 用 3×(nv-2)
    size_t indexCount = 0;
    for (size_t fi = 0; fi < faceStart.size(); ++fi) {
        const int s = faceStart[fi];
        const int e = (fi + 1 < faceStart.size()) ? faceStart[fi + 1] : static_cast<int>(faceIdx.size());
        const int nv = e - s;
        if (nv < 3) continue;
        indexCount += (g_useQuads && nv == 4) ? 6u : static_cast<size_t>(nv - 2) * 3u;
    }
    out.solidVerts.clear();
    out.solidVerts.reserve(npos);
    out.solidIndices.clear();
    out.solidIndices.reserve(indexCount);
    const float gray[4] = {0.72f, 0.72f, 0.74f, 1.0f};
    std::unordered_map<uint64_t, uint32_t> vmap;
    vmap.reserve(static_cast<size_t>(npos) * 2);
    auto emitVertex = [&](int vi, float nx, float ny, float nz) -> uint32_t {
        const uint8_t qnx = static_cast<uint8_t>(static_cast<int8_t>(nx * 127.0f));
        const uint8_t qny = static_cast<uint8_t>(static_cast<int8_t>(ny * 127.0f));
        const uint8_t qnz = static_cast<uint8_t>(static_cast<int8_t>(nz * 127.0f));
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(vi)) << 32) |
                             (static_cast<uint64_t>(qnx) << 16) |
                             (static_cast<uint64_t>(qny) << 8) |
                             static_cast<uint64_t>(qnz);
        auto it = vmap.find(key);
        if (it == vmap.end()) {
            const uint32_t idx = static_cast<uint32_t>(out.solidVerts.size());
            VertexSolid v{};
            v.pos[0] = pos[vi][0]; v.pos[1] = pos[vi][1]; v.pos[2] = pos[vi][2];
            v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
            v.color[0] = gray[0]; v.color[1] = gray[1]; v.color[2] = gray[2]; v.color[3] = 1.0f;
            out.solidVerts.push_back(v);
            vmap.emplace(key, idx);
            return idx;
        }
        return it->second;
    };
    for (size_t fi = 0; fi < faceStart.size(); ++fi) {
        if (g_importProgress >= 0 && (fi & 0xFF) == 0) {
            const int prog = 80 + static_cast<int>(static_cast<float>(fi) / totalFaces * 12.0f);
            if (prog > g_importProgress) g_importProgress = prog;
        }
        const int s = faceStart[fi];
        const int e = (fi + 1 < faceStart.size()) ? faceStart[fi + 1] : static_cast<int>(faceIdx.size());
        const int nv = e - s;
        if (nv < 3) continue;
        if (g_useQuads && nv == 4) {
            // 四边形面渲染（Newell 统一面法线，4 顶点 + 6 索引）
            const int q[4] = {faceIdx[s], faceIdx[s + 1], faceIdx[s + 2], faceIdx[s + 3]};
            bool ok = true;
            for (int k = 0; k < 4; ++k) if (q[k] < 0 || q[k] >= npos) { ok = false; break; }
            if (!ok) continue;
            float nx = 0.0f, ny = 0.0f, nz = 0.0f;
            for (int k = 0; k < 4; ++k) {
                const auto& a = pos[q[k]];
                const auto& b = pos[q[(k + 1) % 4]];
                nx += (a[1] - b[1]) * (a[2] + b[2]);
                ny += (a[2] - b[2]) * (a[0] + b[0]);
                nz += (a[0] - b[0]) * (a[1] + b[1]);
            }
            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl > 1e-9f) { nx /= nl; ny /= nl; nz /= nl; } else { nx = 0; ny = 1; nz = 0; }
            const uint32_t idx[4] = {
                emitVertex(q[0], nx, ny, nz), emitVertex(q[1], nx, ny, nz),
                emitVertex(q[2], nx, ny, nz), emitVertex(q[3], nx, ny, nz)};
            out.solidIndices.push_back(idx[0]);
            out.solidIndices.push_back(idx[1]);
            out.solidIndices.push_back(idx[2]);
            out.solidIndices.push_back(idx[0]);
            out.solidIndices.push_back(idx[2]);
            out.solidIndices.push_back(idx[3]);
        } else {
            // 三角形/多边形 fan
            for (int i = s + 1; i + 1 < e; ++i) {
                const int ia = faceIdx[s], ib = faceIdx[i], ic = faceIdx[i + 1];
                if (ia >= npos || ib >= npos || ic >= npos) continue;
                const auto& pa = pos[ia]; const auto& pb = pos[ib]; const auto& pc = pos[ic];
                float ux = pb[0]-pa[0], uy = pb[1]-pa[1], uz = pb[2]-pa[2];
                float vx = pc[0]-pa[0], vy = pc[1]-pa[1], vz = pc[2]-pa[2];
                float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
                const float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
                if (nl > 1e-9f) { nx /= nl; ny /= nl; nz /= nl; } else { nx = 0; ny = 1; nz = 0; }
                out.solidIndices.push_back(emitVertex(ia, nx, ny, nz));
                out.solidIndices.push_back(emitVertex(ib, nx, ny, nz));
                out.solidIndices.push_back(emitVertex(ic, nx, ny, nz));
            }
        }
    }
    // 贴图：当前渲染管线无纹理采样，贴图解码纯属浪费 → 跳过（LoadObjTexture 接口保留）
    if (g_importProgress >= 0) g_importProgress = 100;
    return !out.solidVerts.empty() || !out.wireVerts.empty();
}

// 计算物体 AABB（遍历 solidVerts；平移 tx/ty/tz 不计入，拾取时再加）
void ComputeObjectBounds(SceneObject& o) {
    o.boundsMin[0] = o.boundsMin[1] = o.boundsMin[2] =  1e30f;
    o.boundsMax[0] = o.boundsMax[1] = o.boundsMax[2] = -1e30f;
    for (const auto& v : o.solidVerts) {
        for (int i = 0; i < 3; ++i) {
            if (v.pos[i] < o.boundsMin[i]) o.boundsMin[i] = v.pos[i];
            if (v.pos[i] > o.boundsMax[i]) o.boundsMax[i] = v.pos[i];
        }
    }
}
