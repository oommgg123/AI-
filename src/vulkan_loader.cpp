// ============================================================================
//
// 加载外部 vulkan-1.dll（正确执行 DllMain/TLS/延迟加载等，兼容性更好）。
// ============================================================================
#include "vulkan_loader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

// 加载失败原因（UTF-8），供主程序弹窗显示
const char* g_loaderError = nullptr;

// ---------------------------------------------------------------------------
// 启动首条日志前写分隔行——用户要求"单一个 LOG 文件 + 每次按时间戳记录"）
// ---------------------------------------------------------------------------
void VkbLog(const char* msg) {
    static wchar_t logPath[MAX_PATH] = {};
    if (!logPath[0]) {
        if (GetModuleFileNameW(nullptr, logPath, MAX_PATH) == 0) {
            lstrcatW(logPath, L"awa.log");
        } else {
            wchar_t* slash = nullptr;
            for (wchar_t* s = logPath; *s; ++s) if (*s == L'\\') slash = s;
            if (slash) {
                *slash = 0;
                lstrcatW(logPath, L"\\awa.log");
            } else {
                lstrcatW(logPath, L"awa.log");
            }
        }
    }
    if (!logPath[0]) return;
    HANDLE h = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        static bool firstEntry = true;
        if (firstEntry) {
            firstEntry = false;
            SYSTEMTIME st;
            GetLocalTime(&st);
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "\r\n========== awa 启动 %04d-%02d-%02d %02d:%02d:%02d ==========",
                          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            DWORD written = 0;
            WriteFile(h, buf, static_cast<DWORD>(std::strlen(buf)), &written, nullptr);
            WriteFile(h, "\r\n", 2, &written, nullptr);
        }
        DWORD written = 0;
        WriteFile(h, msg, static_cast<DWORD>(std::strlen(msg)), &written, nullptr);
        WriteFile(h, "\r\n", 2, &written, nullptr);
        CloseHandle(h);
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#define DECL_PFN(name) PFN_##name name = nullptr;
VK_GLOBAL_FUNCS(DECL_PFN)
VK_INSTANCE_FUNCS(DECL_PFN)
#undef DECL_PFN
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

PFN_vkCreateWin32SurfaceKHR             g_pfnCreateWin32SurfaceKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR g_pfnGetPhysicalDeviceSurfaceSupportKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR g_pfnGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR g_pfnGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR g_pfnGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
PFN_vkGetPhysicalDeviceFeatures2         g_pfnGetPhysicalDeviceFeatures2 = nullptr;
PFN_vkCmdBeginRendering                 g_pfnCmdBeginRendering = nullptr;
PFN_vkCmdEndRendering                   g_pfnCmdEndRendering = nullptr;
PFN_vkCreateSwapchainKHR                 g_pfnCreateSwapchainKHR = nullptr;
PFN_vkDestroySwapchainKHR                g_pfnDestroySwapchainKHR = nullptr;
PFN_vkGetSwapchainImagesKHR              g_pfnGetSwapchainImagesKHR = nullptr;
PFN_vkAcquireNextImageKHR                g_pfnAcquireNextImageKHR = nullptr;
PFN_vkQueuePresentKHR                    g_pfnQueuePresentKHR = nullptr;

#ifdef VKB_ENABLE_VALIDATION
PFN_vkCreateDebugUtilsMessengerEXT  g_pfnCreateDebugUtilsMessengerEXT = nullptr;
PFN_vkDestroyDebugUtilsMessengerEXT g_pfnDestroyDebugUtilsMessengerEXT = nullptr;
VkDebugUtilsMessengerEXT            g_debugMessenger = VK_NULL_HANDLE;
#endif

// ---------------------------------------------------------------------------
// 从「资源」文件夹（兜底 exe 目录）加载 vulkan-1.dll，并加载「全局」Vulkan 核心函数
// ---------------------------------------------------------------------------
bool LoadVulkanCore() {
    g_loaderError = nullptr;
    // 获取 exe 目录（窄字符 UTF-8，绕开 CRT wchar_t 4 字节错位）
    wchar_t exePathW[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePathW, MAX_PATH)) {
        g_loaderError = "无法获取程序路径";
        return false;
    }
    char exeUtf8[MAX_PATH * 2] = {0};
    WideCharToMultiByte(CP_UTF8, 0, exePathW, -1, exeUtf8, sizeof(exeUtf8), nullptr, nullptr);
    std::string dir(exeUtf8);
    const size_t slash = dir.find_last_of('\\');
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);

    // 兜底：exe 同目录「资源」文件夹自带的 vulkan-1.dll（兼容性更好，但系统无 ICD 时同样失败）。
    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) {
        const std::string full = dir + "资源\\vulkan-1.dll";
        wchar_t fullW[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, full.c_str(), -1, fullW, MAX_PATH);
        loader = LoadLibraryW(fullW);
    }
    if (!loader) {
        g_loaderError = "未找到 Vulkan 运行库（vulkan-1.dll）。\n请确保「资源」文件夹与程序在同一目录。";
        VkbLog("[loadvulkan] 未找到 vulkan-1.dll（资源文件夹或 exe 目录）");
        return false;
    }

    {
        wchar_t loaderPathW[MAX_PATH];
        if (GetModuleFileNameW(loader, loaderPathW, MAX_PATH)) {
            char utf8[MAX_PATH * 2] = {0};
            WideCharToMultiByte(CP_UTF8, 0, loaderPathW, -1, utf8, sizeof(utf8), nullptr, nullptr);
            VkbLog((std::string("[loadvulkan] loader=") + utf8).c_str());
        }
        wchar_t icdEnv[1024] = {};
        if (GetEnvironmentVariableW(L"VK_ICD_FILENAMES", icdEnv, 1024) > 0) {
            char utf8[2048] = {0};
            WideCharToMultiByte(CP_UTF8, 0, icdEnv, -1, utf8, sizeof(utf8), nullptr, nullptr);
            VkbLog((std::string("[loadvulkan] VK_ICD_FILENAMES=") + utf8).c_str());
        } else {
            VkbLog("[loadvulkan] VK_ICD_FILENAMES 未设置");
        }
    }

    // GetProcAddress 返回 FARPROC，用 memcpy 转函数指针（避免 -Wcast-function-type 警告）
    FARPROC proc = GetProcAddress(loader, "vkGetInstanceProcAddr");
    if (!proc) {
        g_loaderError = "vulkan-1.dll 无效（找不到 vkGetInstanceProcAddr 导出）。";
        VkbLog("[loadvulkan] vkGetInstanceProcAddr 导出找不到");
        return false;
    }
    static_assert(sizeof(proc) == sizeof(vkGetInstanceProcAddr), "函数指针大小不匹配");
    std::memcpy(&vkGetInstanceProcAddr, &proc, sizeof(proc));
    bool ok = true;
#define LOAD_PFN(name) \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name)); \
    if (!name) { VkbLog("[loadvulkan] 加载函数失败: " #name); ok = false; }
    VK_GLOBAL_FUNCS(LOAD_PFN)
#undef LOAD_PFN
    if (!ok) g_loaderError = "Vulkan 核心函数加载失败。";
    return ok;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool LoadInstanceProcs(VkInstance instance) {
    g_pfnCreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    g_pfnGetPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    g_pfnGetPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    g_pfnGetPhysicalDeviceSurfaceFormatsKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    g_pfnGetPhysicalDeviceSurfacePresentModesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
    g_pfnGetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    g_pfnCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(
        vkGetInstanceProcAddr(instance, "vkCmdBeginRendering"));
    g_pfnCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(
        vkGetInstanceProcAddr(instance, "vkCmdEndRendering"));
#ifdef VKB_ENABLE_VALIDATION
    g_pfnCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    g_pfnDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
#endif

    bool ok = true;
#define LOAD_PFN(name) \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)); \
    if (!name) { VkbLog("[loadinstance] 加载函数失败: " #name); ok = false; }
    VK_INSTANCE_FUNCS(LOAD_PFN)
#undef LOAD_PFN
    return ok;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool LoadDeviceProcs(VkDevice device) {
    g_pfnCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
    g_pfnDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
        vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
    g_pfnGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
    g_pfnAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
    g_pfnQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(
        vkGetDeviceProcAddr(device, "vkQueuePresentKHR"));
    return g_pfnCreateSwapchainKHR && g_pfnDestroySwapchainKHR &&
           g_pfnGetSwapchainImagesKHR && g_pfnAcquireNextImageKHR && g_pfnQueuePresentKHR;
}

// ---------------------------------------------------------------------------
// 软件 ICD 兜底：设置 VK_ICD_FILENAMES 指向「资源」文件夹的 SwiftShader 软件渲染器。
// Win7 兼容修复（Round278）：Win7 老 Vulkan loader（VMware 自带 1.0/1.1 era）不识别
// VK_ICD_FILENAMES 环境变量也不递归子目录扫描 ICD manifest → 额外把 icd json
// 复制到 exe 同目录（library_path 改为 .\\资源\\vk_swiftshader.dll），让任何 loader
// 都能在应用根自动发现。
bool EnableSoftwareIcd() {
    wchar_t exePathW[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePathW, MAX_PATH)) return false;
    char exeUtf8[MAX_PATH * 2] = {0};
    WideCharToMultiByte(CP_UTF8, 0, exePathW, -1, exeUtf8, sizeof(exeUtf8), nullptr, nullptr);
    std::string dir(exeUtf8);
    const size_t slash = dir.find_last_of('\\');
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);
    const std::string jsonSrc = dir + "资源\\vk_swiftshader_icd.json";
    const std::string dllPath = dir + "资源\\vk_swiftshader.dll";

    // 确认 JSON 与 dll 均存在
    wchar_t jsonSrcW[MAX_PATH], dllW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, jsonSrc.c_str(), -1, jsonSrcW, MAX_PATH);
    MultiByteToWideChar(CP_UTF8, 0, dllPath.c_str(), -1, dllW, MAX_PATH);
    if (GetFileAttributesW(jsonSrcW) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(dllW) == INVALID_FILE_ATTRIBUTES) {
        VkbLog("[swiftshader] 内置软件 ICD 资源缺失，跳过兜底");
        return false;
    }

    // ① 复制 icd json 到 exe 同目录（Win7 老 loader 在应用根自动扫描 .json manifest）
    //    library_path 用相对路径 ".\\资源\\vk_swiftshader.dll"（相对 json = exe 根 → 资源/）
    const std::string jsonDst = dir + "vk_swiftshader_icd.json";
    const std::string jsonContent =
        std::string("{\"file_format_version\": \"1.0.0\", \"ICD\": "
                    "{\"library_path\": \".\\\\资源\\\\vk_swiftshader.dll\", \"api_version\": \"1.0.5\"}}");
    {
        std::ofstream out(jsonDst, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(jsonContent.data(), static_cast<std::streamsize>(jsonContent.size()));
            out.close();
        }
    }

    // ② 设环境变量（Win10 新 loader / Vulkan 1.2+ loader 优先用此；Win7 老 loader 忽略但已能扫到根目录 json）
    wchar_t jsonDstW[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, jsonDst.c_str(), -1, jsonDstW, MAX_PATH);
    SetEnvironmentVariableW(L"VK_ICD_FILENAMES", jsonDstW);
    VkbLog("[swiftshader] 已启用软件 ICD 兜底（json 已复制到 exe 同目录 + VK_ICD_FILENAMES）");
    return true;
}
