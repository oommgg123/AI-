#ifndef ZIP_EXTRACT_H
#define ZIP_EXTRACT_H

#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// 从内存中的 ZIP 数据解压到指定目录（outDir）。
// 支持 method 0(store) 与 8(deflate, 经 zlib 原始 inflate)。
// 自动按 '/' 与 '\' 创建子目录；UTF-8 文件名会转 UTF-16。
// cb(done, total, fileName) 用于进度回调（fileName 为宽字符）。
// 返回 true 表示整体成功（即使个别文件失败也会继续，错误数在 outErrors 中）。
bool ExtractBundle(
    const BYTE* zip, DWORD zipSize, const std::wstring& outDir,
    std::function<void(int, int, const std::wstring&)> cb,
    std::wstring* outExeName,   // 顶层 .exe 的条目名（如 L"awa.exe"），找不到则为空
    int* outErrors,
    __int64* outBytes);

#endif // ZIP_EXTRACT_H
