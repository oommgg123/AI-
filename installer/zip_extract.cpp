#define UNICODE
#define _UNICODE
#include "zip_extract.h"
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <cstdint>

static uint16_t rd16(const BYTE* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const BYTE* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// UTF-8 -> wstring（ZIP 含中文条目名时需转换）
static std::wstring Utf8ToW(const char* s, size_t n) {
    if (n == 0) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
    if (wlen <= 0) return L"";
    std::wstring w; w.resize((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, s, (int)n, &w[0], wlen);
    return w;
}

static bool InflateRaw(const BYTE* src, uint32_t srcLen,
                       std::vector<BYTE>& out, uint32_t hint) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    // -MAX_WBITS => 原始 deflate（无 zlib 头），符合 ZIP 存储
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
    strm.next_in = (Bytef*)src;
    strm.avail_in = srcLen;
    if (hint) out.reserve((size_t)hint);
    const int CHUNK = 65536;
    BYTE buf[CHUNK];
    int ret = Z_OK;
    do {
        strm.next_out = buf;
        strm.avail_out = CHUNK;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return false;
        }
        size_t have = CHUNK - strm.avail_out;
        out.insert(out.end(), buf, buf + have);
    } while (ret != Z_STREAM_END);
    inflateEnd(&strm);
    return true;
}

static bool WriteFileAll(const std::wstring& path, const BYTE* data, size_t len) {
    DWORD w = 0;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    BOOL ok = WriteFile(h, data, (DWORD)len, &w, NULL);
    CloseHandle(h);
    return ok && w == (DWORD)len;
}

// 依据条目名逐级创建目录
static void EnsureParentDirs(const std::wstring& filePath) {
    size_t pos = filePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return;
    std::wstring dir = filePath.substr(0, pos);
    // 逐步创建（支持多层）
    std::wstring cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur += dir[i];
        if (dir[i] == L'\\' || dir[i] == L'/') {
            if (cur.size() > 1) CreateDirectoryW(cur.c_str(), NULL);
        }
    }
    CreateDirectoryW(dir.c_str(), NULL);
}

bool ExtractBundle(const BYTE* zip, DWORD zipSize, const std::wstring& outDir,
                   std::function<void(int, int, const std::wstring&)> cb,
                   std::wstring* outExeName, int* outErrors, __int64* outBytes) {
    if (outErrors) *outErrors = 0;
    if (outBytes) *outBytes = 0;
    if (outExeName) outExeName->clear();

    if (!zip || zipSize < 22) return false;

    // 1) 定位 EOCD (PK\x05\x06)
    uint32_t eocd = 0;
    DWORD minPos = (zipSize > 0xFFFF + 22) ? zipSize - (0xFFFF + 22) : 0;
    bool found = false;
    for (uint32_t i = zipSize; i > minPos;) {
        --i;
        if (zip[i] == 0x50 && i + 3 < zipSize &&
            zip[i + 1] == 0x4B && zip[i + 2] == 0x05 && zip[i + 3] == 0x06) {
            eocd = i; found = true; break;
        }
    }
    if (!found) return false;

    uint32_t cdOffset = rd32(zip + eocd + 16);
    uint32_t cdSize   = rd32(zip + eocd + 12);
    uint32_t numEnt   = rd16(zip + eocd + 10);
    if (cdOffset + cdSize > zipSize) return false;

    struct Ent {
        std::wstring name;
        uint16_t method;
        uint32_t comp;
        uint32_t uncomp;
        uint32_t localOff;
    };
    std::vector<Ent> ents;
    ents.reserve(numEnt);

    // 2) 遍历中央目录
    uint32_t p = cdOffset;
    for (uint32_t k = 0; k < numEnt; ++k) {
        if (p + 46 > zipSize) break;
        if (!(zip[p] == 0x50 && zip[p + 1] == 0x4B && zip[p + 2] == 0x01 && zip[p + 3] == 0x02))
            break;
        uint16_t method   = rd16(zip + p + 10);
        uint32_t comp     = rd32(zip + p + 20);
        uint32_t uncomp   = rd32(zip + p + 24);
        uint16_t nameLen  = rd16(zip + p + 28);
        uint16_t extraLen = rd16(zip + p + 30);
        uint16_t commLen  = rd16(zip + p + 32);
        uint32_t localOff = rd32(zip + p + 42);
        if (p + 46 + nameLen > zipSize) break;
        std::wstring name = Utf8ToW((const char*)(zip + p + 46), nameLen);
        ents.push_back({name, method, comp, uncomp, localOff});
        p += 46 + nameLen + extraLen + commLen;
    }
    if (ents.empty()) return false;

    // 3) 逐条目解压
    int total = (int)ents.size();
    int done = 0;
    for (const Ent& e : ents) {
        // 跳过目录条目
        bool isDir = (!e.name.empty() && (e.name.back() == L'/' || e.name.back() == L'\\'));
        std::wstring baseName = e.name;
        size_t sl = baseName.find_last_of(L"\\/");
        if (sl != std::wstring::npos) baseName = baseName.substr(sl + 1);

        if (cb) cb(done, total, baseName.empty() ? e.name : baseName);
        ++done;

        if (isDir) {
            std::wstring dpath = outDir + L"\\" + e.name;
            // 规范化路径分隔符
            for (auto& c : dpath) if (c == L'/') c = L'\\';
            CreateDirectoryW(dpath.c_str(), NULL);
            continue;
        }

        // 定位本地文件头
        if (e.localOff + 30 > zipSize) { if (outErrors) (*outErrors)++; continue; }
        if (!(zip[e.localOff] == 0x50 && zip[e.localOff + 1] == 0x4B &&
              zip[e.localOff + 2] == 0x03 && zip[e.localOff + 3] == 0x04)) {
            if (outErrors) (*outErrors)++; continue;
        }
        uint16_t lNameLen  = rd16(zip + e.localOff + 26);
        uint16_t lExtraLen = rd16(zip + e.localOff + 28);
        uint64_t dataOff = (uint64_t)e.localOff + 30 + lNameLen + lExtraLen;
        if (dataOff + e.comp > zipSize) { if (outErrors) (*outErrors)++; continue; }

        const BYTE* src = zip + dataOff;

        std::vector<BYTE> outBuf;
        bool ok = false;
        if (e.method == 0) { // store
            outBuf.assign(src, src + e.comp);
            ok = (outBuf.size() == e.comp);
        } else if (e.method == 8) { // deflate
            ok = InflateRaw(src, e.comp, outBuf, e.uncomp);
        } else {
            // 不支持的压缩方式
            if (outErrors) (*outErrors)++;
            continue;
        }
        if (!ok) { if (outErrors) (*outErrors)++; continue; }

        // 写出
        std::wstring rel = e.name;
        for (auto& c : rel) if (c == L'/') c = L'\\';
        std::wstring full = outDir + L"\\" + rel;
        EnsureParentDirs(full);
        if (WriteFileAll(full, outBuf.data(), outBuf.size())) {
            if (outBytes) *outBytes += (int)outBuf.size();
        } else {
            if (outErrors) (*outErrors)++;
        }

        // 记录顶层 .exe
        if (outExeName && outExeName->empty()) {
            bool hasSlash = (e.name.find_first_of(L"\\/") != std::wstring::npos);
            if (!hasSlash && baseName.size() >= 4 &&
                _wcsicmp(baseName.substr(baseName.size() - 4).c_str(), L".exe") == 0) {
                *outExeName = e.name;
            }
        }
    }
    return true;
}
