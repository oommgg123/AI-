#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>
#include <vector>
#include <functional>
#include "zip_extract.h"

using namespace Gdiplus;
using namespace std;
namespace fs = std::filesystem;

#define IDI_APP 101
#define IDB_BG  102

#define WM_UPDATE_PROGRESS (WM_APP + 1)
#define WM_INSTALL_DONE    (WM_APP + 2)
#define WM_TRAY            (WM_APP + 3)

// ---------- 全局状态 ----------
static HINSTANCE g_inst        = NULL;
static HWND       g_hwnd        = NULL;
static HWND       g_hPath       = NULL;
static HWND       g_hBrowse     = NULL;
static HWND       g_hChkDesktop = NULL;
static HWND       g_hChkTray    = NULL;
static HWND       g_hInstall    = NULL;
static HWND       g_hProgress   = NULL;   // 容器（自绘进度条）
static HWND       g_hStatus     = NULL;    // 状态文字
static HWND       g_hFileInfo   = NULL;    // 当前文件名
static ULONG_PTR  g_gdip        = 0;
static Bitmap*    g_bg          = NULL;
static atomic<bool> g_busy(false);
static NOTIFYICONDATAW g_nid    = { 0 };
static bool       g_tray        = false;
static bool       g_optDesktop  = true;
static bool       g_optTray     = false;

// 进度条数据
static int  g_progressDone  = 0;
static int  g_progressTotal = 0;
static wstring g_currentFile = L"";

static const int WIN_W = 540;
static const int WIN_H = 480;
static const int PROG_X = 30;
static const int PROG_Y = 258;
static const int PROG_W = 470;
static const int PROG_H = 28;

// ---------- 资源加载背景图 ----------
static Bitmap* LoadBgFromResource() {
    HRSRC hRes = FindResourceW(g_inst, MAKEINTRESOURCE(IDB_BG), RT_RCDATA);
    if (!hRes) return NULL;
    HGLOBAL hGlob = LoadResource(g_inst, hRes);
    if (!hGlob) return NULL;
    DWORD size = SizeofResource(g_inst, hRes);
    void* pData = LockResource(hGlob);
    if (!pData || size == 0) return NULL;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return NULL;
    void* pMem = GlobalLock(hMem);
    memcpy(pMem, pData, size);
    GlobalUnlock(hMem);
    IStream* pStream = NULL;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) { GlobalFree(hMem); return NULL; }
    Bitmap* bmp = Bitmap::FromStream(pStream);
    pStream->Release();
    if (bmp && bmp->GetLastStatus() != Ok) { delete bmp; bmp = NULL; }
    return bmp;
}

// ---------- 路径辅助 ----------
static wstring ExeDir() {
    wchar_t p[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, p, MAX_PATH);
    wstring s(p);
    size_t pos = s.find_last_of(L"\\/");
    return (pos == wstring::npos) ? L"" : s.substr(0, pos);
}

// 源目录：exe 同级 "awa"
static wstring SourceDir() {
    wstring ed = ExeDir();
    wstring a = ed + L"\\awa";
    if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES) return a;
    return a;
}

// 从源目录查找实际的 .exe 文件名（不硬编码）
static wstring FindSourceExe(const wstring& srcDir) {
    error_code ec;
    for (auto& de : fs::directory_iterator(srcDir, ec)) {
        if (ec) break;
        if (de.is_regular_file(ec)) {
            wstring ext = de.path().extension().wstring();
            // 不区分大小写比较
            if (_wcsicmp(ext.c_str(), L".exe") == 0)
                return de.path().filename().wstring();
        }
    }
    return L"awa.exe";  // 回退默认
}

// ---------- 托盘 ----------
static void AddTray() {
    if (g_tray) return;
    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP));
    wcscpy_s(g_nid.szTip, _countof(g_nid.szTip), L"awa 安装程序");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_tray = true;
}
static void RemoveTray() {
    if (!g_tray) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_tray = false;
}

// ---------- 安全复制（带校验与重试）----------
// 返回: 0=成功 1=失败 2=跳过(log)
struct CopyResult { int filesCopied; int bytesCopied; int errors; };

static bool SafeCopyFile(const wstring& src, const wstring& dst) {
    // 先确保目标目录存在
    wstring ddir = dst.substr(0, dst.find_last_of(L"\\/"));
    fs::create_directories(ddir);

    // 获取源文件大小用于后续校验
    error_code ec;
    auto srcSize = fs::file_size(src, ec);
    if (ec) return false;

    // 尝试复制，最多重试 3 次
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
            // 校验：比对文件大小
            auto dstSize = fs::file_size(dst, ec);
            if (!ec && dstSize == srcSize) return true;
            // 大小不一致 → 删除残文件重试
            DeleteFileW(dst.c_str());
        }
        Sleep(200);
    }
    return false;
}

// 收集源文件清单（用于进度计算）
static vector<wstring> CollectFiles(const wstring& dir) {
    vector<wstring> files;
    error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (fs::is_regular_file(it->status(ec))) {
            wstring fn = it->path().filename().wstring();
            if (fn == L"awa.log") continue;  // 跳过日志
            files.push_back(it->path().wstring());
        }
    }
    return files;
}

// 单线程顺序复制（带进度回调）
static CopyResult DoCopyAll(const wstring& srcDir, const wstring& dstDir,
                             function<void(int,int,const wstring&)> onProgress) {
    CopyResult result = {0, 0, 0};
    auto files = CollectFiles(srcDir);
    int total = (int)files.size();

    for (int i = 0; i < total; ++i) {
        const wstring& srcPath = files[i];
        // 计算相对路径：srcDir -> 保持子目录结构
        wstring rel = srcPath.substr(srcDir.size());
        if (rel[0] == L'\\' || rel[0] == L'/') rel = rel.substr(1);
        wstring dstPath = dstDir + L"\\" + rel;

        // 提取纯文件名用于显示
        wstring fileName = fs::path(srcPath).filename().wstring();

        // 进度回调（含当前文件名）
        if (onProgress) onProgress(i, total, fileName);

        if (SafeCopyFile(srcPath, dstPath)) {
            result.filesCopied++;
            error_code ec;
            auto sz = fs::file_size(srcPath, ec);
            if (!ec) result.bytesCopied += (int)sz;
        } else {
            result.errors++;
        }

        // 每个文件后更新一次 UI 进度
        PostMessageW(g_hwnd, WM_UPDATE_PROGRESS,
                     (WPARAM)(i + 1), (LPARAM)total);
    }
    return result;
}

// ---------- 桌面快捷方式（动态 exe 名 + 工作目录）----------
static void MakeShortcut(const wstring& exeFullPath, const wstring& lnkPath,
                          const wstring& workDir) {
    CoInitialize(NULL);
    IShellLinkW* sl = NULL;
    IPersistFile* pf = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(exeFullPath.c_str());
        sl->SetWorkingDirectory(workDir.c_str());      // 关键：设置工作目录
        sl->SetDescription(L"awa 3D 查看器");
        sl->SetIconLocation(exeFullPath.c_str(), 0);   // 图标从 exe 自身提取
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            pf->Save(lnkPath.c_str(), TRUE);
            pf->Release();
        }
        sl->Release();
    }
    CoUninitialize();
}

// 格式化字节数
static wstring FormatBytes(__int64 bytes) {
    wchar_t buf[64];
    if (bytes >= 1048576)
        swprintf_s(buf, _countof(buf), L"%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024)
        swprintf_s(buf, _countof(buf), L"%.1f KB", bytes / 1024.0);
    else
        swprintf_s(buf, _countof(buf), L"%lld B", bytes);
    return wstring(buf);
}

// ---------- 内嵌资源解压 ----------
// 优先从资源 103 解压（真正内嵌，单文件自包含）；
// 若资源缺失则回退到同级 awa/ 文件夹（开发调试用）。
static bool ExtractEmbedded(const wstring& dest, wstring& outExeName, CopyResult& cr) {
    outExeName.clear();
    cr = { 0, 0, 0 };
    int entryTotal = 0;
    __int64 bytes = 0;

    HRSRC hRes = FindResourceW(g_inst, MAKEINTRESOURCE(103), RT_RCDATA);
    if (hRes) {
        HGLOBAL hGlob = LoadResource(g_inst, hRes);
        if (hGlob) {
            DWORD size = SizeofResource(g_inst, hRes);
            void* p = LockResource(hGlob);
            if (p && size) {
                ExtractBundle((const BYTE*)p, size, dest,
                    [&](int d, int t, const wstring& f) {
                        g_currentFile = f;
                        g_progressDone = d;
                        g_progressTotal = t;
                        entryTotal = t;
                        PostMessageW(g_hwnd, WM_UPDATE_PROGRESS, (WPARAM)d, (LPARAM)t);
                    },
                    &outExeName, &cr.errors, &bytes);
                cr.bytesCopied = (int)bytes;
                cr.filesCopied = (entryTotal > 0 ? entryTotal : 0) - cr.errors;
                if (cr.filesCopied < 0) cr.filesCopied = 0;
                return true;
            }
        }
    }

    // 回退：同级 awa 文件夹
    wstring src = SourceDir();
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    auto allFiles = CollectFiles(src);
    int total = (int)allFiles.size();
    PostMessageW(g_hwnd, WM_UPDATE_PROGRESS, 0, (LPARAM)total);
    cr = DoCopyAll(src, dest, [&](int d, int t, const wstring& f) {
        g_currentFile = f; g_progressDone = d; g_progressTotal = t;
    });
    outExeName = FindSourceExe(src);
    return true;
}

// ---------- 安装线程 ----------
static DWORD WINAPI InstallProc(LPVOID) {
    wchar_t buf[MAX_PATH] = { 0 };
    GetWindowTextW(g_hPath, buf, MAX_PATH);
    wstring dest(buf);
    if (dest.empty() || dest.back() == L'\\') {
        if (!dest.empty()) dest.pop_back();
        if (dest.empty()) {
            PostMessageW(g_hwnd, WM_INSTALL_DONE, 1, 0);
            return 0;
        }
    }

    // 创建目标根目录
    error_code ec;
    fs::create_directories(dest, ec);

    wstring exeName;
    CopyResult res = { 0, 0, 0 };
    ExtractEmbedded(dest, exeName, res);

    // 创建快捷方式（用实际 exe 名）
    if (g_optDesktop && !exeName.empty()) {
        wchar_t desk[MAX_PATH] = { 0 };
        SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk);
        wstring exeFullPath = dest + L"\\" + exeName;
        MakeShortcut(exeFullPath, wstring(desk) + L"\\awa.lnk", dest);
    }

    // 结果报告
    if (res.errors > 0) {
        wchar_t msg[256];
        swprintf_s(msg, _countof(msg),
            L"安装完成，但 %d 个文件复制失败。\n\n已复制: %d 个文件 (%s)\n请检查目标目录或重试。",
            res.errors, res.filesCopied, FormatBytes(res.bytesCopied).c_str());
        MessageBoxW(g_hwnd, msg, L"awa - 安装警告", MB_OK | MB_ICONWARNING);
        PostMessageW(g_hwnd, WM_INSTALL_DONE, 2, 0);
    } else {
        PostMessageW(g_hwnd, WM_INSTALL_DONE, 0, 0);
    }
    return 0;
}

// ---------- 自绘进度条 ----------
// 手动构建圆角矩形路径（GDI+ 无 AddRoundRect）
static void AddRoundRectPath(GraphicsPath& path, int x, int y, int w, int h, int r) {
    path.AddArc(x, y, r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2, y, r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2, y+h-r*2, r*2, r*2, 0, 90);
    path.AddArc(x, y+h-r*2, r*2, r*2, 90, 90);
    path.CloseFigure();
}

static void DrawProgressBar(HDC hdc, const RECT& r) {
    Graphics g(hdc);

    // 背景（深灰圆角矩形）
    SolidBrush bgBrush(Color(45, 45, 55));
    GraphicsPath bgPath;
    AddRoundRectPath(bgPath, r.left, r.top, r.right-r.left-1, r.bottom-r.top-1, 14);
    g.FillPath(&bgBrush, &bgPath);

    if (g_progressTotal > 0 && g_progressDone > 0) {
        float pct = (float)g_progressDone / (float)g_progressTotal;
        if (pct > 1.0f) pct = 1.0f;

        int fillW = (int)((r.right - r.left - 2) * pct);
        if (fillW > 0) {
            // 渐变填充（青蓝→蓝紫渐变）
            Rect fillR(r.left+1, r.top+1, fillW, r.bottom-r.top-3);
            LinearGradientBrush grad(
                Point(fillR.X, fillR.Y),
                Point(fillR.X + fillR.Width, fillR.Y),
                Color(0, 180, 216),    // 青色 #00B4D8
                Color(102, 51, 153));   // 紫色 #663399
            GraphicsPath fillPath;
            AddRoundRectPath(fillPath, fillR.X, fillR.Y, fillR.Width, fillR.Height, 12);
            g.FillPath(&grad, &fillPath);

            // 高光（顶部亮线）
            if (fillW > 10) {
                Pen hlPen(Color(255, 255, 255, 60), 1.0f);
                g.DrawLine(&hlPen, fillR.X+8, fillR.Y+3,
                           min(fillR.X+fillW-8, fillR.X+fillR.Width-2), fillR.Y+3);
            }
        }

        // 百分比文字（居中，白色粗体）
        wchar_t pctText[32];
        swprintf_s(pctText, _countof(pctText), L"%d%%", (int)(pct * 100));
        Font font(L"Segoe UI", 11, FontStyleBold);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush txtBrush(Color(255, 255, 255));
        RectF textR(r.left, r.top, r.right-r.left, r.bottom-r.top);
        g.DrawString(pctText, -1, &font, textR, &sf, &txtBrush);
    } else {
        // 0% 状态显示 "准备中..."
        Font font(L"Segoe UI", 10);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush txtBrush(Color(160, 160, 170));
        RectF textR(r.left, r.top, r.right-r.left, r.bottom-r.top);
        g.DrawString(L"准备中...", -1, &font, textR, &sf, &txtBrush);
    }
}

// ---------- 控件颜色 ----------
static LRESULT CALLBACK CtlColor(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORDLG) {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(232, 232, 238));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    if (msg == WM_CTLCOLOREDIT) {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(235, 235, 240));
        SetBkColor(hdc, RGB(28, 28, 36));
        return (LRESULT)CreateSolidBrush(RGB(28, 28, 36));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------- 浏览文件夹 ----------
static void BrowseFolder() {
    BROWSEINFO bi = { 0 };
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"选择安装位置";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
    if (pid) {
        wchar_t path[MAX_PATH] = { 0 };
        if (SHGetPathFromIDListW(pid, path))
            SetWindowTextW(g_hPath, path);
        CoTaskMemFree(pid);
    }
}

// ---------- 主窗口过程 ----------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        // 标题
        CreateWindowW(L"STATIC", L"awa 安装程序",
            WS_CHILD | WS_VISIBLE,
            30, 22, 380, 38, hwnd, NULL, g_inst, NULL);

        // 安装位置标签
        CreateWindowW(L"STATIC", L"安装位置：",
            WS_CHILD | WS_VISIBLE,
            30, 78, 200, 22, hwnd, NULL, g_inst, NULL);

        // 路径输入框
        g_hPath = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            30, 102, 340, 26, hwnd, NULL, g_inst, NULL);
        wstring def = ExeDir() + L"\\awa";
        SetWindowTextW(g_hPath, def.c_str());

        // 浏览按钮
        g_hBrowse = CreateWindowW(L"BUTTON", L"浏览...",
            WS_CHILD | WS_VISIBLE,
            382, 101, 110, 28, hwnd, NULL, g_inst, NULL);

        // 桌面快捷方式复选框
        g_hChkDesktop = CreateWindowW(L"BUTTON", L" 生成桌面快捷方式",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            30, 156, 260, 26, hwnd, NULL, g_inst, NULL);
        SendMessageW(g_hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);

        // 系统托盘图标复选框
        g_hChkTray = CreateWindowW(L"BUTTON", L" 生成系统栏图标（托盘）",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            30, 196, 300, 26, hwnd, NULL, g_inst, NULL);

        // ====== 美化进度条区域 ======
        // 进度条容器（owner-draw 风格，实际由 WM_PAINT 绘制）
        g_hProgress = CreateWindowW(L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            PROG_X, PROG_Y, PROG_W, PROG_H,
            hwnd, (HMENU)100, g_inst, NULL);

        // 当前文件名
        g_hFileInfo = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            PROG_X, PROG_Y + PROG_H + 4, PROG_W, 20,
            hwnd, NULL, g_inst, NULL);

        // 状态文字
        g_hStatus = CreateWindowW(L"STATIC",
            L"准备就绪，请选择安装位置后点击安装。",
            WS_CHILD | WS_VISIBLE,
            30, PROG_Y + PROG_H + 30, 470, 24,
            hwnd, NULL, g_inst, NULL);

        // 安装按钮
        g_hInstall = CreateWindowW(L"BUTTON", L"安 装",
            WS_CHILD | WS_VISIBLE,
            195, 395, 140, 40, hwnd, NULL, g_inst, NULL);

        return 0;
    }
    case WM_COMMAND: {
        if ((HWND)lp == g_hBrowse && HIWORD(wp) == BN_CLICKED) {
            BrowseFolder(); return 0;
        }
        if ((HWND)lp == g_hChkTray && HIWORD(wp) == BN_CLICKED) {
            g_optTray = (SendMessageW(g_hChkTray, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (g_optTray) AddTray(); else RemoveTray();
            return 0;
        }
        if ((HWND)lp == g_hChkDesktop && HIWORD(wp) == BN_CLICKED) {
            g_optDesktop = (SendMessageW(g_hChkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }
        if ((HWND)lp == g_hInstall && HIWORD(wp) == BN_CLICKED) {
            if (g_busy) return 0;
            g_busy = true;
            EnableWindow(g_hInstall, FALSE);
            EnableWindow(g_hBrowse, FALSE);
            EnableWindow(g_hChkDesktop, FALSE);
            EnableWindow(g_hChkTray, FALSE);
            SetWindowTextW(g_hStatus, L"正在安装，请稍候...");
            SetWindowTextW(g_hFileInfo, L"正在扫描文件...");
            g_progressDone = 0;
            g_progressTotal = 0;
            InvalidateRect(g_hProgress, NULL, TRUE);  // 重绘进度条
            CreateThread(NULL, 0, InstallProc, NULL, 0, NULL);
            return 0;
        }
        break;
    }
    case WM_DRAWITEM: {
        // 自绘进度条
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlID == 100) {
            DrawProgressBar(dis->hDC, dis->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_UPDATE_PROGRESS: {
        int done = (int)wp, total = (int)lp;
        g_progressDone = done;
        g_progressTotal = total;
        InvalidateRect(g_hProgress, NULL, TRUE);  // 触发 WM_DRAWITEM 重绘

        // 更新状态文字
        wchar_t s[128];
        if (total > 0) {
            int pct = (total > 0) ? (done * 100 / total) : 0;
            swprintf_s(s, _countof(s), L"正在复制文件... %d%% (%d/%d)", pct, done, total);
        } else {
            wcscpy_s(s, _countof(s), L"正在扫描文件...");
        }
        SetWindowTextW(g_hStatus, s);

        // 更新当前文件名
        if (!g_currentFile.empty()) {
            wchar_t fi[260];
            swprintf_s(fi, _countof(fi), L"↳ %s", g_currentFile.c_str());
            SetWindowTextW(g_hFileInfo, fi);
        }
        return 0;
    }
    case WM_INSTALL_DONE: {
        g_busy = false;
        EnableWindow(g_hInstall, TRUE);
        EnableWindow(g_hBrowse, TRUE);
        EnableWindow(g_hChkDesktop, TRUE);
        EnableWindow(g_hChkTray, TRUE);

        // 进度条填满
        g_progressDone = g_progressTotal;
        InvalidateRect(g_hProgress, NULL, TRUE);

        if (wp == 1) {
            MessageBoxW(g_hwnd, L"请先选择一个有效的安装位置。",
                L"awa - 安装", MB_OK | MB_ICONERROR);
            SetWindowTextW(g_hStatus, L"安装已取消：未选择位置。");
        } else if (wp == 2) {
            SetWindowTextW(g_hStatus, L"安装完成（部分文件失败），请检查目录。");
        } else {
            SetWindowTextW(g_hStatus, L"安装完成 ✓ 你可以在所选目录中找到 awa 并运行。");
            SetWindowTextW(g_hFileInfo, L"✓ 所有文件已就绪");
            MessageBoxW(g_hwnd,
                L"安装完成！\n\n"
                L"awa 已安装到你选择的位置。\n"
                L"双击 awa.exe 即可运行。",
                L"awa - 安装", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }
    case WM_TRAY: {
        if (lp == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"打开");
            AppendMenuW(m, MF_STRING, 2, L"退出");
            SetForegroundWindow(g_hwnd);
            int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, g_hwnd, NULL);
            DestroyMenu(m);
            if (id == 1) { ShowWindow(g_hwnd, SW_RESTORE); SetForegroundWindow(g_hwnd); }
            else if (id == 2) DestroyWindow(g_hwnd);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc);
        if (g_bg) {
            g.DrawImage(g_bg, 0, 0, WIN_W, WIN_H);
        } else {
            SolidBrush b(Color(40, 40, 50));
            g.FillRectangle(&b, 0, 0, WIN_W, WIN_H);
        }
        // 半透明蒙版
        SolidBrush ov(Color(130, 0, 0, 0));
        g.FillRectangle(&ov, 0, 0, WIN_W, WIN_H);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
        return CtlColor(hwnd, msg, wp, lp);
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lp;
        mmi->ptMinTrackSize.x = mmi->ptMaxTrackSize.x = WIN_W;
        mmi->ptMinTrackSize.y = mmi->ptMaxTrackSize.y = WIN_H;
        return 0;
    }
    case WM_DESTROY:
        RemoveTray();
        if (g_bg) { delete g_bg; g_bg = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    g_inst = hInst;
    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdip, &gsi, NULL);

    // 加载内嵌背景图
    g_bg = LoadBgFromResource();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"awaSetupCls";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCE(IDI_APP));
    RegisterClassW(&wc);

    // 不可最大化、不可缩放
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"awa 安装程序",
        style, CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    RECT r = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&r, style, FALSE);
    SetWindowPos(g_hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(g_gdip);
    return 0;
}
