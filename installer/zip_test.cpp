#define UNICODE
#define _UNICODE
#include "zip_extract.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <windows.h>

static std::vector<BYTE> ReadFileBytes(const wchar_t* path) {
    std::vector<BYTE> buf;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return buf;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    buf.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, NULL);
    CloseHandle(h);
    return buf;
}

int main(int argc, char** argv) {
    std::wstring zipPath = L"awa_bundle.zip";
    std::wstring outDir  = L"_extract_test";
    if (argc > 1) zipPath = (const wchar_t*)argv[1];
    if (argc > 2) outDir  = (const wchar_t*)argv[2];

    CreateDirectoryW(outDir.c_str(), NULL);
    auto data = ReadFileBytes(zipPath.c_str());
    if (data.empty()) { wprintf(L"无法读取 zip: %s\n", zipPath.c_str()); return 1; }

    std::wstring exeName;
    int errors = 0; __int64 bytes = 0;
    wprintf(L"zip bytes=%zu\n", data.size());
    bool ok = ExtractBundle(data.data(), (DWORD)data.size(), outDir,
        [](int d, int t, const std::wstring& f) {
            wprintf(L"[%d/%d] %s\n", d, t, f.c_str());
        }, &exeName, &errors, &bytes);

    wprintf(L"\n结果: ok=%d errors=%d bytes=%lld exeName=%s\n",
            ok, errors, bytes, exeName.c_str());

    // 验证关键文件存在
    std::wstring check[] = {
        outDir + L"\\awa.exe",
        outDir + L"\\资源\\vulkan-1.dll",
        outDir + L"\\资源\\s1\\ss1-RGB.png",
        outDir + L"\\button_theme.txt",
    };
    for (auto& c : check) {
        DWORD a = GetFileAttributesW(c.c_str());
        wprintf(L"  check %s -> %s\n", c.c_str(),
                (a != INVALID_FILE_ATTRIBUTES) ? L"OK" : L"MISSING");
    }
    return (ok && errors == 0) ? 0 : 2;
}
