// ============================================================================
//  vulkan_init.cpp（#208 拆分：原 main.cpp ）
// ============================================================================
#include "awa_internal.h"
#include <initguid.h>
#include <wincodec.h>
#include "resource.h"
#include "mc_blocks.h"
#include "import_window.h"
#include <rounded_rect.vert.inc>
#include <rounded_rect.frag.inc>
#include <unified3d.vert.inc>
#include <unified3d.frag.inc>
#include <grid.vert.inc>
#include <grid.frag.inc>
#include <axis3d.vert.inc>
#include <axis3d.frag.inc>
#include <line3d.vert.inc>
#include <line3d.frag.inc>
#include <text.vert.inc>
#include <text.frag.inc>
#include <fxaa.vert.inc>
#include <fxaa.frag.inc>
#include <outline.vert.inc>
#include <outline.frag.inc>
#include <gear_png.inc>
#include <pen_png.inc>
#include <import_png.inc>
#include <export_png.inc>

using std::uint32_t;

static const uint32_t kColorStride[5] = {24u, 32u, 28u, 26u, 25u};

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    OutputDebugStringA(data->pMessage);
    OutputDebugStringA("\n");
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        SetError(std::string("验证层错误: ") + data->pMessage);
    }
    return VK_FALSE;
}
uint16_t FloatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
    const uint32_t mant = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFF) == 0xFF) return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0u));
    if (exp >= 0x1F) return static_cast<uint16_t>(sign | 0x7C00u);
    if (exp <= 0) {
        if (exp < -10) return static_cast<uint16_t>(sign);
        const uint32_t m = mant | 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exp);
        uint32_t h = m >> shift;
        const uint32_t rem = m & ((1u << shift) - 1u);
        if (rem > (1u << (shift - 1u)) || (rem == (1u << (shift - 1u)) && (h & 1u))) ++h;
        return static_cast<uint16_t>(sign | h);
    }
    uint32_t h = sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13);
    const uint32_t rem = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
    return static_cast<uint16_t>(h);
}
bool CreateInstance(App& app) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = kAppName.data();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = kAppName.data();
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
 appInfo.apiVersion = VK_API_VERSION_1_0; // 兼容：老驱动 / 核显

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> instanceExts(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, instanceExts.data());
    for (const auto& ext : instanceExts) {
        if (std::string_view(ext.extensionName) == VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) {
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            break;
        }
    }

    std::vector<const char*> layers;
#ifdef VKB_ENABLE_VALIDATION
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layerProps(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
    for (const auto& prop : layerProps) {
        if (std::string_view(prop.layerName) == "VK_LAYER_KHRONOS_validation") {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            break;
        }
    }
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    wchar_t forceEnv[16] = {};
    const bool forceSS = GetEnvironmentVariableW(L"AWA_FORCE_SWIFTSHADER", forceEnv, 16) > 0 &&
                         forceEnv[0] == L'1';
    VkResult res = forceSS ? VK_ERROR_INCOMPATIBLE_DRIVER : vkCreateInstance(&createInfo, nullptr, &app.vk.instance);
 // 失败且是「无兼容驱动」时，回退内置软件 ICD（SwiftShader，CPU 渲染）再试一次——
    if (res != VK_SUCCESS && res == VK_ERROR_INCOMPATIBLE_DRIVER && EnableSoftwareIcd()) {
        VkbLog("[createinstance] 硬件 ICD 不可用，已回退 SwiftShader 软件渲染");
        res = vkCreateInstance(&createInfo, nullptr, &app.vk.instance);
    }
    if (res != VK_SUCCESS) {
        SetError(std::string("Vulkan 调用失败: vkCreateInstance (VkResult=") +
                 std::to_string(static_cast<int>(res)) + ")");
        return false;
    }
    if (!LoadInstanceProcs(app.vk.instance)) {
        SetError("实例级 Vulkan 函数加载失败（动态加载）");
        return false;
    }

#ifdef VKB_ENABLE_VALIDATION
    if (g_pfnCreateDebugUtilsMessengerEXT && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT dbgInfo{};
        dbgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgInfo.pfnUserCallback = DebugCallback;
        if (g_pfnCreateDebugUtilsMessengerEXT(app.vk.instance, &dbgInfo, nullptr, &g_debugMessenger) != VK_SUCCESS) {
            g_debugMessenger = VK_NULL_HANDLE;
        }
    }
#endif
    return true;
}
bool CreateSurface(App& app, HINSTANCE hInstance) {
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = hInstance;
    surfaceInfo.hwnd = app.hwnd;
    VKB_TRY(g_pfnCreateWin32SurfaceKHR(app.vk.instance, &surfaceInfo, nullptr, &app.vk.surface));
    return true;
}
bool PickPhysicalDevice(App& app) {
    wchar_t forceEnv[16] = {};
    const bool forceSS = GetEnvironmentVariableW(L"AWA_FORCE_SWIFTSHADER", forceEnv, 16) > 0 &&
                         forceEnv[0] == L'1';
    if (forceSS) VkbLog("[pdev] AWA_FORCE_SWIFTSHADER=1：只选 CPU 软件渲染设备");

    uint32_t deviceCount = 0;
    VKB_TRY(vkEnumeratePhysicalDevices(app.vk.instance, &deviceCount, nullptr));
    if (deviceCount == 0) {
        SetError("未找到任何 Vulkan 物理设备");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VKB_TRY(vkEnumeratePhysicalDevices(app.vk.instance, &deviceCount, devices.data()));

    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties devProps;
        vkGetPhysicalDeviceProperties(dev, &devProps);
        if (forceSS && devProps.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) continue;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, families.data());

        for (uint32_t i = 0; i < familyCount; ++i) {
            VkBool32 presentSupport = VK_FALSE;
            VKB_TRY(g_pfnGetPhysicalDeviceSurfaceSupportKHR(dev, i, app.vk.surface, &presentSupport));
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
                app.vk.physicalDevice = dev;
                app.vk.graphicsFamily = i;
 // 检查设备是否支持 4x MSAA 颜色附件（不支持则降级到 1x 防止崩溃）
                const VkSampleCountFlags msaaSupported = devProps.limits.framebufferColorSampleCounts;
                if (app.aa.msaaEnabled && !(msaaSupported & VK_SAMPLE_COUNT_4_BIT)) {
                    app.aa.msaaEnabled = false;
                    app.aa.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
                }
 // **深度格式回退**（核显兼容关键）：VK_FORMAT_D32_SFLOAT 是 Vulkan 1.0
 // vkCreateImage 失败 → 软件打不开。按优先级回退：
                {
                    static const VkFormat kDepthCandidates[] = {
                        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
                    VkFormat chosen = VK_FORMAT_D16_UNORM;
                    for (VkFormat fmt : kDepthCandidates) {
                        VkFormatProperties fp;
                        vkGetPhysicalDeviceFormatProperties(dev, fmt, &fp);
                        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                            chosen = fmt;
                            break;
                        }
                    }
                    if (chosen != app.vk.depthFormat) {
                        app.vk.depthFormat = chosen;
                        VkbLog(("[depth] 深度格式回退: D32_SFLOAT 不支持 → 格式码 " +
                                std::to_string(static_cast<int>(app.vk.depthFormat))).c_str());
                    }
                }
                app.vk.gpuApiVersion = devProps.apiVersion;
                app.vk.gpuType = static_cast<int>(devProps.deviceType);
                app.vk.gpuVendor = devProps.vendorID;
                {
 // 安全拷贝设备名（最多 255 字符 + NUL；截断时仍保证以 NUL 结尾）
                    size_t n = 0;
                    while (n < sizeof(app.vk.gpuName) - 1 && devProps.deviceName[n]) {
                        app.vk.gpuName[n] = devProps.deviceName[n];
                        ++n;
                    }
                    app.vk.gpuName[n] = '\0';
                }
                VkbLog(("[gpu] " + std::string(app.vk.gpuName) +
                        " api=" + std::to_string(VK_API_VERSION_MAJOR(devProps.apiVersion)) + "." +
                        std::to_string(VK_API_VERSION_MINOR(devProps.apiVersion)) +
                        " type=" + std::to_string(app.vk.gpuType)).c_str());
                return true;
            }
        }
    }

    SetError("未找到支持图形队列并可呈现的物理设备");
    return false;
}
bool CreateDevice(App& app) {
 // 探测 Vulkan 1.3 动态渲染能力（老驱动不支持则自动回退传统 render pass）
    bool supportsDynamicRendering = false;
    if (g_pfnGetPhysicalDeviceFeatures2) {
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;
        g_pfnGetPhysicalDeviceFeatures2(app.vk.physicalDevice, &features2);
        supportsDynamicRendering = (features13.dynamicRendering == VK_TRUE);
    }
 // 核显无硬件 Vulkan 时回退 SwiftShader 后走动态渲染路径会导致 vkCreateDevice/
    if (app.vk.gpuType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        app.vk.useDynamicRendering = false;
        VkbLog("[device] CPU 软件渲染设备：强制传统 render pass 路径（跳过动态渲染）");
    } else {
        app.vk.useDynamicRendering = supportsDynamicRendering;
    }
    VkbLog(("[device] 渲染路径: " + std::string(app.vk.useDynamicRendering ? "动态渲染(1.3)" : "传统 render pass(1.0)")).c_str());

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = app.vk.graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceVulkan13Features enabled13{};
    enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabled13.dynamicRendering = VK_TRUE;

 // 宽线（lineWidth>1）必需 wideLines 特性——线框加粗用（pipelineLine3dWide）
 // 删除 fillModeNonSolid 请求——wire 组（polygonMode=LINE）已删，无管线使用 LINE 模式
    VkPhysicalDeviceFeatures devFeatures{};
    if (g_pfnGetPhysicalDeviceFeatures2) {
        VkPhysicalDeviceFeatures2 f2{};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        g_pfnGetPhysicalDeviceFeatures2(app.vk.physicalDevice, &f2);
        if (f2.features.wideLines == VK_TRUE) devFeatures.wideLines = VK_TRUE;
    } else {
 devFeatures.wideLines = VK_TRUE; // 无查询接口时假定支持
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = app.vk.useDynamicRendering ? &enabled13 : nullptr;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    deviceInfo.pEnabledFeatures = &devFeatures;

    VKB_TRY(vkCreateDevice(app.vk.physicalDevice, &deviceInfo, nullptr, &app.vk.device));
    vkGetDeviceQueue(app.vk.device, app.vk.graphicsFamily, 0, &app.vk.graphicsQueue);
    if (!LoadDeviceProcs(app.vk.device)) {
        SetError("交换链扩展函数加载失败（动态加载）");
        return false;
    }
    return true;
}
bool CreateSwapchain(App& app, VkSwapchainKHR oldSwapchain) {
    VkSurfaceCapabilitiesKHR caps{};
    VKB_TRY(g_pfnGetPhysicalDeviceSurfaceCapabilitiesKHR(app.vk.physicalDevice, app.vk.surface, &caps));

    uint32_t formatCount = 0;
    VKB_TRY(g_pfnGetPhysicalDeviceSurfaceFormatsKHR(app.vk.physicalDevice, app.vk.surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VKB_TRY(g_pfnGetPhysicalDeviceSurfaceFormatsKHR(app.vk.physicalDevice, app.vk.surface, &formatCount, formats.data()));
    if (formats.empty()) {
        SetError("表面未提供任何颜色格式");
        return false;
    }
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = f;
            break;
        }
    }

 // 呈现模式：**FIFO（垂直同步，双缓冲）**——"主窗口也创建双缓冲"：
 // 旧逻辑优先 MAILBOX（三缓冲低延迟、无 VSync），帧率波动时图像交替快慢不一，
 // 视觉上像"闪烁/跳动"；FIFO 严格等 VSync 换帧（双缓冲），无撕裂无闪烁最稳。
 // FIFO 是 Vulkan 强制支持的呈现模式，任何平台可用。
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        RECT clientRect{};
        GetClientRect(app.hwnd, &clientRect);
        extent.width = static_cast<uint32_t>(clientRect.right - clientRect.left);
        extent.height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
    }
    app.vk.swapchainExtent = extent;
    app.vk.swapchainFormat = surfaceFormat.format;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = app.vk.surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = surfaceFormat.format;
    swapInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = presentMode;
    swapInfo.clipped = VK_TRUE;
    swapInfo.oldSwapchain = oldSwapchain;

    VKB_TRY(g_pfnCreateSwapchainKHR(app.vk.device, &swapInfo, nullptr, &app.vk.swapchain));

    if (oldSwapchain != VK_NULL_HANDLE) {
        for (VkImageView view : app.vk.swapchainImageViews) vkDestroyImageView(app.vk.device, view, nullptr);
        app.vk.swapchainImageViews.clear();
        for (VkFramebuffer fb : app.vk.framebuffers) vkDestroyFramebuffer(app.vk.device, fb, nullptr);
        app.vk.framebuffers.clear();
        g_pfnDestroySwapchainKHR(app.vk.device, oldSwapchain, nullptr);
    }

 // actualCount 初始化为 0；允许 INCOMPLETE（数组不足时合法返回值）——循环直到 SUCCESS
    uint32_t actualCount = 0;
    VkResult imgRes = g_pfnGetSwapchainImagesKHR(app.vk.device, app.vk.swapchain, &actualCount, nullptr);
    if (imgRes != VK_SUCCESS && imgRes != VK_INCOMPLETE) {
        SetError(std::string("Vulkan 调用失败: vkGetSwapchainImagesKHR(nullptr) (VkResult=") + std::to_string(imgRes) + ")");
        return false;
    }
    app.vk.swapchainImages.resize(actualCount);
    imgRes = g_pfnGetSwapchainImagesKHR(app.vk.device, app.vk.swapchain, &actualCount, app.vk.swapchainImages.data());
 while (imgRes == VK_INCOMPLETE) { // resize 后图像数变化——重新查询并扩容
        actualCount = 0;
        g_pfnGetSwapchainImagesKHR(app.vk.device, app.vk.swapchain, &actualCount, nullptr);
        app.vk.swapchainImages.resize(actualCount);
        imgRes = g_pfnGetSwapchainImagesKHR(app.vk.device, app.vk.swapchain, &actualCount, app.vk.swapchainImages.data());
    }
    if (imgRes != VK_SUCCESS) {
        SetError(std::string("Vulkan 调用失败: vkGetSwapchainImagesKHR(data) (VkResult=") + std::to_string(imgRes) + ")");
        return false;
    }

    app.vk.swapchainImageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = app.vk.swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VKB_TRY(vkCreateImageView(app.vk.device, &viewInfo, nullptr, &app.vk.swapchainImageViews[i]));
    }
    return true;
}
bool CreateRenderPass(App& app) {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = app.vk.swapchainFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = app.vk.depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    VKB_TRY(vkCreateRenderPass(app.vk.device, &rpInfo, nullptr, &app.vk.renderPass));
    return true;
}
bool CreateFramebuffers(App& app) {
    app.vk.framebuffers.resize(app.vk.swapchainImageViews.size());
    for (size_t i = 0; i < app.vk.swapchainImageViews.size(); ++i) {
        VkImageView fbAttachments[2] = {app.vk.swapchainImageViews[i], app.vk.depthView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = app.vk.renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments;
        fbInfo.width = app.vk.swapchainExtent.width;
        fbInfo.height = app.vk.swapchainExtent.height;
        fbInfo.layers = 1;
        VKB_TRY(vkCreateFramebuffer(app.vk.device, &fbInfo, nullptr, &app.vk.framebuffers[i]));
    }
    return true;
}
bool RecreateSwapchain(App& app) {
    vkQueueWaitIdle(app.vk.graphicsQueue);
    if (!CreateSwapchain(app, app.vk.swapchain)) return false;
    if (!CreateMSAAColorResources(app)) return false;
    if (!CreateFXAAResources(app)) return false;
 if (!CreateOutlineResources(app)) return false; // mask RT 随视口尺寸重建
    if (!CreateDepthResources(app)) return false;
 // 兼容路径（传统 render pass）需按新尺寸重建 framebuffer
    if (!app.vk.useDynamicRendering && !CreateFramebuffers(app)) return false;
    return true;
}
bool CreateShaderModule(VkDevice device, const unsigned char* code, size_t size, VkShaderModule& module) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = reinterpret_cast<const uint32_t*>(code);
    return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS;
}
bool CreateVertexBuffer(App& app) {
    const float vertices[] = {
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
    };
    const VkDeviceSize bufferSize = sizeof(vertices);

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKB_TRY(vkCreateBuffer(app.vk.device, &bufInfo, nullptr, &app.vk.vertexBuffer));

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(app.vk.device, app.vk.vertexBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.vk.physicalDevice, &memProps);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & want) == want) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        SetError("未找到可用的主机可见内存类型");
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.vk.device, &allocInfo, nullptr, &app.vk.vertexBufferMemory));
    VKB_TRY(vkBindBufferMemory(app.vk.device, app.vk.vertexBuffer, app.vk.vertexBufferMemory, 0));

    void* mapped = nullptr;
    VKB_TRY(vkMapMemory(app.vk.device, app.vk.vertexBufferMemory, 0, bufferSize, 0, &mapped));
    std::memcpy(mapped, vertices, bufferSize);
    vkUnmapMemory(app.vk.device, app.vk.vertexBufferMemory);
    return true;
}
bool CreateVertexBuffer3D(App& app) {
    VkbLog(("[upload] CreateVertexBuffer3D 入口 objects=" + std::to_string(app.scene.objects.size()) + " vertexColorMode=" + std::to_string(app.scene.vertexColorMode)).c_str());
 // 未算过 AABB 的物体先算（选中拾取用；导入/复制后自动维护）
    for (auto& o : app.scene.objects)
        if (o.boundsMin[0] > 1e29f) ComputeObjectBounds(o);
 // wireVerts 已由导入器按原始面生成（四边）。仅当 wireVerts 空（如纯线物体）才用 solid 补建，
 // 否则会清掉已生成的四边 wireVerts（MC 合并网格尤其不能触发，否则对角线又回来）。
 // featureVerts 已延迟构建，不再参与此条件（导入期恒空）
    for (auto& o : app.scene.objects)
        if (!o.solidIndices.empty() && o.wireVerts.empty())
            BuildObjectWireframe(o);
    uint64_t totalVerts = 0, totalIndices = 0, totalWireVerts = 0;
    for (auto& o : app.scene.objects) {
        const uint32_t wv = static_cast<uint32_t>(o.wireVerts.size());
        const uint32_t sv = static_cast<uint32_t>(o.solidVerts.size());
        const uint32_t wi = static_cast<uint32_t>(o.wireIndices.size());
        const uint32_t si = static_cast<uint32_t>(o.solidIndices.size());
        o.vertexOffset = static_cast<uint32_t>(totalVerts);
 o.wireVtxOffset = static_cast<uint32_t>(totalWireVerts); // 专用 40B 缓冲偏移
        o.wireIndexOffset = static_cast<uint32_t>(totalIndices);
        o.solidIndexOffset = static_cast<uint32_t>(totalIndices) + wi;
 // 实体缓冲不再含 wireVerts 冗余副本（线框用独立 wireVtxBuffer3D，此副本从未被绘制引用）
        totalVerts += static_cast<uint64_t>(sv);
        totalWireVerts += static_cast<uint64_t>(wv);
        totalIndices += static_cast<uint64_t>(wi) + si;
    }
    if (totalVerts == 0 || totalIndices == 0) return true;
    VkbLog(("[upload] 无数据提前返回（totalVerts=0），本次未上传 GPU"));

 // 颜色位深模式GPU buffer 按模式压缩写入颜色
 // 0=无颜色 24B(pos+normal) | 1=16bit half4 32B | 2=8bit uchar4 28B | 3=4bit 26B | 4=1bit 25B
    const int cmode = app.scene.vertexColorMode < 0 ? 0 : (app.scene.vertexColorMode > 4 ? 4 : app.scene.vertexColorMode);
    const VkDeviceSize vtxStride = kColorStride[cmode];
    const VkDeviceSize vertBytes = totalVerts * vtxStride;
    const VkDeviceSize idxBytes = totalIndices * sizeof(uint32_t);

 // 复用已有 buffer"导入卡一下"：容量足够时直接复用，
 // 省 vkDestroyBuffer/vkFreeMemory/vkAllocateMemory/vkBindBuffer 全部开销；
 // 仅在容量不足时才重建（一次性代价，触发一次）
    auto ensureDeviceLocal = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkDeviceSize& capacity, VkBuffer& buf, VkDeviceMemory& mem) -> bool {
 if (buf != VK_NULL_HANDLE && size <= capacity) return true; // 容量够，直接复用
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(app.vk.device, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(app.vk.device, mem, nullptr); mem = VK_NULL_HANDLE; }
        capacity = 0;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.vk.device, &bi, nullptr, &buf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.vk.device, buf, &mr);
        const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == UINT32_MAX) { SetError("未找到可用的显存类型"); return false; }
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = mi;
        VKB_TRY(vkAllocateMemory(app.vk.device, &ai, nullptr, &mem));
        VKB_TRY(vkBindBufferMemory(app.vk.device, buf, mem, 0));
        capacity = size;
        return true;
    };
    if (!ensureDeviceLocal(vertBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           app.vk.vertexBuffer3DCapacity, app.vk.vertexBuffer3D, app.vk.vertexBufferMemory3D)) return false;
    if (!ensureDeviceLocal(idxBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           app.vk.indexBuffer3DCapacity, app.vk.indexBuffer3D, app.vk.indexBufferMemory3D)) return false;
 // 线框专用 40B 顶点缓冲（VertexSolid 原样，供 line3d 管线）
    const VkDeviceSize wireBytes = totalWireVerts * sizeof(VertexSolid);
    if (wireBytes > 0 &&
        !ensureDeviceLocal(wireBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           app.vk.wireVtxBuffer3DCapacity, app.vk.wireVtxBuffer3D, app.vk.wireVtxBufferMemory3D)) return false;

    const VkDeviceSize stageSize = vertBytes + idxBytes + wireBytes;
 // staging 分批上传——峰值 RAM 从「全部顶点+索引」降为固定 128MB 块
    constexpr VkDeviceSize kUploadChunk = 128ull * 1024 * 1024;
    const VkDeviceSize kStageChunk = (stageSize < kUploadChunk) ? stageSize : kUploadChunk;
    VkBuffer stageBuf = VK_NULL_HANDLE;
    VkDeviceMemory stageMem = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = kStageChunk;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.vk.device, &bi, nullptr, &stageBuf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.vk.device, stageBuf, &mr);
        const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mi == UINT32_MAX) { SetError("未找到可用的暂存内存类型"); return false; }
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = mi;
        VKB_TRY(vkAllocateMemory(app.vk.device, &ai, nullptr, &stageMem));
        VKB_TRY(vkBindBufferMemory(app.vk.device, stageBuf, stageMem, 0));
    }

 // 分批生成 + 上传（每块 128MB）——先写 staging 块，记录 copy 区间，最后一次提交
    struct ChunkRegion { VkBuffer dst; VkDeviceSize dstOff; VkDeviceSize size; };
    std::vector<ChunkRegion> regions;
    regions.reserve((stageSize / kStageChunk) + 8);
    const auto mapStage = [&](VkDeviceSize bytes) -> char* {
        void* mp = nullptr;
        if (vkMapMemory(app.vk.device, stageMem, 0, bytes, 0, &mp) != VK_SUCCESS) return nullptr;
        return static_cast<char*>(mp);
    };
 // solidVerts 压缩写入（按全局顶点序号区间；o.vertexOffset = 该物体 solid 顶点起点）
    const auto fillSolidRange = [&](uint64_t startV, uint64_t count, char* dst) {
        for (const auto& o : app.scene.objects) {
            const uint64_t oStart = static_cast<uint64_t>(o.vertexOffset);
            const uint64_t oEnd = oStart + o.solidVerts.size();
            const uint64_t lo = std::max(startV, oStart);
            const uint64_t hi = std::min(startV + count, oEnd);
            if (lo >= hi) continue;
            for (uint64_t i = lo; i < hi; ++i) {
                const VertexSolid& v = o.solidVerts[i - oStart];
                char* d = dst + (i - startV) * vtxStride;
                std::memcpy(d, v.pos, 12);
                std::memcpy(d + 12, v.normal, 12);
 if (cmode == 1) { // 16bit 半精度
                    uint16_t c[4];
                    for (int j = 0; j < 4; ++j) c[j] = FloatToHalf(v.color[j]);
                    std::memcpy(d + 24, c, 8);
 } else if (cmode == 2) { // 8bit UNORM
                    uint8_t c[4];
                    for (int j = 0; j < 4; ++j)
                        c[j] = static_cast<uint8_t>(std::clamp(v.color[j], 0.0f, 1.0f) * 255.0f + 0.5f);
                    std::memcpy(d + 24, c, 4);
 } else if (cmode == 3) { // 4bit 打包
                    uint16_t c = 0;
                    for (int j = 0; j < 4; ++j) {
                        const uint8_t q = static_cast<uint8_t>(std::clamp(v.color[j], 0.0f, 1.0f) * 15.0f + 0.5f);
                        c |= static_cast<uint16_t>(q) << (j * 4);
                    }
                    std::memcpy(d + 24, &c, 2);
 } else if (cmode == 4) { // 1bit 灰度
                    const float lum = (v.color[0] + v.color[1] + v.color[2]) * (1.0f / 3.0f);
                    d[24] = static_cast<uint8_t>(std::clamp(lum, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
        }
    };
 // 索引区（wire 段 + solid 段；o.wireIndexOffset/solidIndexOffset 为全局索引偏移）
    const auto fillIdxRange = [&](uint64_t startI, uint64_t count, uint32_t* dst) {
        for (const auto& o : app.scene.objects) {
            const uint64_t w0 = static_cast<uint64_t>(o.wireIndexOffset);
            const uint64_t s0 = static_cast<uint64_t>(o.solidIndexOffset);
            for (int pass = 0; pass < 2; ++pass) {
                const uint64_t segStart = (pass == 0) ? w0 : s0;
                const uint64_t segLen = (pass == 0) ? o.wireIndices.size() : o.solidIndices.size();
                const uint64_t lo = std::max(startI, segStart);
                const uint64_t hi = std::min(startI + count, segStart + segLen);
                if (lo >= hi) continue;
                for (uint64_t i = lo; i < hi; ++i) {
                    const uint32_t v = (pass == 0) ? o.wireIndices[i - segStart] : o.solidIndices[i - segStart];
                    dst[i - startI] = v;
                }
            }
        }
    };
 // 线框区（12B pos → 40B VertexSolid 展开，统一灰白线色）
    const auto fillWireRange = [&](uint64_t startW, uint64_t count, VertexSolid* dst) {
        for (const auto& o : app.scene.objects) {
            const uint64_t oStart = static_cast<uint64_t>(o.wireVtxOffset);
            const uint64_t oEnd = oStart + o.wireVerts.size();
            const uint64_t lo = std::max(startW, oStart);
            const uint64_t hi = std::min(startW + count, oEnd);
            if (lo >= hi) continue;
            for (uint64_t i = lo; i < hi; ++i) {
                VertexSolid vs{};
                vs.pos[0] = o.wireVerts[i - oStart].pos[0];
                vs.pos[1] = o.wireVerts[i - oStart].pos[1];
                vs.pos[2] = o.wireVerts[i - oStart].pos[2];
                vs.normal[0] = 0.0f; vs.normal[1] = 1.0f; vs.normal[2] = 0.0f;
                vs.color[0] = 0.72f; vs.color[1] = 0.72f; vs.color[2] = 0.74f; vs.color[3] = 1.0f;
                dst[i - startW] = vs;
            }
        }
    };
 // vert 区
    {
        uint64_t globalV = 0;
        while (globalV * vtxStride < vertBytes) {
            const VkDeviceSize chunkBytes = std::min<VkDeviceSize>(kStageChunk, vertBytes - globalV * vtxStride);
            const uint64_t count = chunkBytes / vtxStride;
            char* mp = mapStage(chunkBytes);
            if (!mp) return false;
            fillSolidRange(globalV, count, mp);
            vkUnmapMemory(app.vk.device, stageMem);
            regions.push_back({app.vk.vertexBuffer3D, globalV * vtxStride, chunkBytes});
            globalV += count;
        }
    }
 // idx 区
    if (idxBytes > 0) {
        uint64_t globalI = 0;
        while (globalI * 4 < idxBytes) {
            const VkDeviceSize chunkBytes = std::min<VkDeviceSize>(kStageChunk, idxBytes - globalI * 4);
            const uint64_t count = chunkBytes / 4;
            char* mp = mapStage(chunkBytes);
            if (!mp) return false;
            fillIdxRange(globalI, count, reinterpret_cast<uint32_t*>(mp));
            vkUnmapMemory(app.vk.device, stageMem);
            regions.push_back({app.vk.indexBuffer3D, globalI * 4, chunkBytes});
            globalI += count;
        }
    }
 // wire 区
    if (wireBytes > 0) {
        uint64_t globalW = 0;
        while (globalW * sizeof(VertexSolid) < wireBytes) {
            const VkDeviceSize chunkBytes = std::min<VkDeviceSize>(kStageChunk, wireBytes - globalW * sizeof(VertexSolid));
            const uint64_t count = chunkBytes / sizeof(VertexSolid);
            char* mp = mapStage(chunkBytes);
            if (!mp) return false;
            fillWireRange(globalW, count, reinterpret_cast<VertexSolid*>(mp));
            vkUnmapMemory(app.vk.device, stageMem);
            regions.push_back({app.vk.wireVtxBuffer3D, globalW * sizeof(VertexSolid), chunkBytes});
            globalW += count;
        }
    }

    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = app.vk.graphicsFamily;
        VKB_TRY(vkCreateCommandPool(app.vk.device, &pci, nullptr, &pool));
        VkCommandBufferAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        a.commandPool = pool;
        a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VKB_TRY(vkAllocateCommandBuffers(app.vk.device, &a, &cmd));
        VkCommandBufferBeginInfo b{};
        b.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VKB_TRY(vkBeginCommandBuffer(cmd, &b));
 // 每块：staging(HOST_WRITE) → TRANSFER_READ barrier → copy
        for (const auto& rg : regions) {
            VkBufferMemoryBarrier bb{};
            bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            bb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            bb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bb.buffer = stageBuf;
            bb.offset = 0;
            bb.size = rg.size;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 1, &bb, 0, nullptr);
            VkBufferCopy region{};
            region.srcOffset = 0; region.dstOffset = rg.dstOff; region.size = rg.size;
            vkCmdCopyBuffer(cmd, stageBuf, rg.dst, 1, &region);
        }
        VKB_TRY(vkEndCommandBuffer(cmd));
        VkSubmitInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        s.commandBufferCount = 1;
        s.pCommandBuffers = &cmd;
 // →vkQueueSubmit 偶发返 INCOMPLETE/DEVICE_LOST（驱动 TDR/超时/显存压力）
 // INCOMPLETE 重试一次；DEVICE_LOST(-4)/OUT_OF_* 不弹窗（下一帧重建 swapchain 自动恢复）
        VkResult submitRes = vkQueueSubmit(app.vk.graphicsQueue, 1, &s, VK_NULL_HANDLE);
        if (submitRes == VK_INCOMPLETE) submitRes = vkQueueSubmit(app.vk.graphicsQueue, 1, &s, VK_NULL_HANDLE);
    VkbLog(("[upload] vkQueueSubmit 结果=" + std::to_string(static_cast<int>(submitRes)) + "（0=SUCCESS -4=DEVICE_LOST -7=INCOMPLETE）").c_str());
        if (submitRes != VK_SUCCESS && submitRes != VK_SUBOPTIMAL_KHR) {
 // DEVICE_LOST / OUT_OF_HOST|DEVICE_MEMORY 等驱动级错误——静默不弹窗
 // 数据已写入 GPU 缓冲，只是命令未执行；下一帧 DrawFrame 的 RecreateSwapchain 会自动恢复
            if (submitRes != VK_ERROR_DEVICE_LOST &&
                submitRes != VK_ERROR_OUT_OF_HOST_MEMORY &&
                submitRes != VK_ERROR_OUT_OF_DEVICE_MEMORY)
                VKB_TRY(submitRes);
        }
 // vkQueueWaitIdle 同样偶发 DEVICE_LOST(-4)/OUT_OF_*（驱动 TDR/超时），
 // 与上方 vkQueueSubmit 一致——静默不弹窗，下一帧 RecreateSwapchain 自动恢复
        {
            VkResult waitRes = vkQueueWaitIdle(app.vk.graphicsQueue);
            if (waitRes != VK_SUCCESS && waitRes != VK_SUBOPTIMAL_KHR) {
    VkbLog(("[upload] vkQueueWaitIdle 结果=" + std::to_string(static_cast<int>(waitRes)) + "（0=SUCCESS -4=DEVICE_LOST）").c_str());
                if (waitRes != VK_ERROR_DEVICE_LOST &&
                    waitRes != VK_ERROR_OUT_OF_HOST_MEMORY &&
                    waitRes != VK_ERROR_OUT_OF_DEVICE_MEMORY)
                    VKB_TRY(waitRes);
            }
        }
        vkFreeCommandBuffers(app.vk.device, pool, 1, &cmd);
        vkDestroyCommandPool(app.vk.device, pool, nullptr);
    }

        vkDestroyBuffer(app.vk.device, stageBuf, nullptr);
    vkFreeMemory(app.vk.device, stageMem, nullptr);
    VkbLog(("[upload] 上传完成，return true"));
    return true;
}
bool CreateRoundedRectPipeline(App& app, VkPipelineLayout& outLayout, VkPipeline& outPipeline) {
    VkbLog("[pipeline] 开始创建圆角矩形管线");
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kRoundedRectVertSpv, kRoundedRectVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kRoundedRectFragSpv, kRoundedRectFragSpvSize, fragModule)) {
        SetError("着色器模块创建失败");
        return false;
    }
    VkbLog("[pipeline] shader 模块 OK");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.aa.msaaSamples;

 // 深度模板：2D UI 不测试/不写深度。**关键**：传统路径的 renderPass 带 depth 附件，
 // Vulkan 规范要求此时 pDepthStencilState 必须非 NULL——否则部分驱动（SwiftShader）
 // 在 vkCreateGraphicsPipelines 解引用空指针崩溃（核显 SwiftShader 打不开的根因）。
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &outLayout));
    VkbLog("[pipeline] pipelineLayout OK");

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
 pipelineInfo.pDepthStencilState = &depthStencil; // 传统 render pass 有 depth 附件，必须非 NULL
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = outLayout;
    pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
    pipelineInfo.subpass = 0;

    VkbLog("[pipeline] vkCreateGraphicsPipelines 前");
    VkResult res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline);
    VkbLog("[pipeline] vkCreateGraphicsPipelines 后");
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}
bool CreatePanelBlendPipeline(App& app, VkPipelineLayout layout, VkPipeline& outPipeline) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kRoundedRectVertSpv, kRoundedRectVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kRoundedRectFragSpv, kRoundedRectFragSpvSize, fragModule)) {
        SetError("着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.aa.msaaSamples;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
    pipelineInfo.subpass = 0;
    VkResult res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline);
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    return res == VK_SUCCESS;
}
bool CreatePipeline(App& app) {
    if (!CreateRoundedRectPipeline(app, app.vk.pipelineLayout, app.vk.pipeline)) return false;
 // 框选矩形半透明管线（panel 同款 shader/layout + SRC_ALPHA 混合）——非致命，失败仅框选填充不透明
    if (!CreatePanelBlendPipeline(app, app.vk.pipelineLayout, app.vk.pipelinePanelBlend)) {
        app.vk.pipelinePanelBlend = VK_NULL_HANDLE;
        VkbLog("[warn] pipelinePanelBlend 创建失败（框选矩形将无半透明）");
    }
    return true;
}
bool CreateMenuPipeline(App& app) {
    return CreateRoundedRectPipeline(app, app.vk.menuPipelineLayout, app.vk.menuPipeline);
}
bool CreatePipelineAxis(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kAxis3dVertSpv, kAxis3dVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kAxis3dFragSpv, kAxis3dFragSpvSize, fragModule)) {
        SetError("世界坐标轴着色器模块创建失败");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 0;
    vertexInput.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.aa.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Axis3DPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &app.vk.pipelineLayoutAxis));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.vk.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.vk.pipelineLayoutAxis;
    pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.vk.pipelineAxis);
    VKB_TRY(res);

    depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
    res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.vk.pipelineAxisOccluded);
    VKB_TRY(res);

    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    return true;
}
bool CreateMSAAColorResources(App& app) {
    if (app.vk.msaaColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(app.vk.device, app.vk.msaaColorView, nullptr);
        app.vk.msaaColorView = VK_NULL_HANDLE;
    }
    if (app.vk.msaaColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(app.vk.device, app.vk.msaaColorImage, nullptr);
        app.vk.msaaColorImage = VK_NULL_HANDLE;
    }
    if (app.vk.msaaColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(app.vk.device, app.vk.msaaColorMemory, nullptr);
        app.vk.msaaColorMemory = VK_NULL_HANDLE;
    }
    if (!app.aa.msaaEnabled) return true;
    const VkExtent2D& e = app.vk.swapchainExtent;
    if (e.width == 0 || e.height == 0) return false;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = app.vk.swapchainFormat;
    imgInfo.extent = {e.width, e.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = app.aa.msaaSamples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.vk.device, &imgInfo, nullptr, &app.vk.msaaColorImage));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(app.vk.device, app.vk.msaaColorImage, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.vk.physicalDevice, &memProps);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & want) == want) { memTypeIndex = i; break; }
    }
    if (memTypeIndex == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.vk.device, &allocInfo, nullptr, &app.vk.msaaColorMemory));
    VKB_TRY(vkBindImageMemory(app.vk.device, app.vk.msaaColorImage, app.vk.msaaColorMemory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = app.vk.msaaColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = app.vk.swapchainFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.vk.device, &viewInfo, nullptr, &app.vk.msaaColorView));
    return true;
}
bool CreateFXAAResources(App& app) {
    if (app.vk.fxaaDescriptorSet != VK_NULL_HANDLE) { app.vk.fxaaDescriptorSet = VK_NULL_HANDLE; }
    if (app.vk.fxaaDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(app.vk.device, app.vk.fxaaDescriptorPool, nullptr);
        app.vk.fxaaDescriptorPool = VK_NULL_HANDLE;
    }
 // 注意：fxaaDescriptorLayout 不销毁——其内容（1 个 combined image sampler）不随
 // 中间纹理尺寸变化，保留可避免 resize 后 fxaaPipeline 的 pipelineLayout 引用已销毁
    if (app.vk.fxaaSampler != VK_NULL_HANDLE) {
        vkDestroySampler(app.vk.device, app.vk.fxaaSampler, nullptr);
        app.vk.fxaaSampler = VK_NULL_HANDLE;
    }
    if (app.vk.fxaaView != VK_NULL_HANDLE) {
        vkDestroyImageView(app.vk.device, app.vk.fxaaView, nullptr);
        app.vk.fxaaView = VK_NULL_HANDLE;
    }
    if (app.vk.fxaaImage != VK_NULL_HANDLE) {
        vkDestroyImage(app.vk.device, app.vk.fxaaImage, nullptr);
        app.vk.fxaaImage = VK_NULL_HANDLE;
    }
    if (app.vk.fxaaMemory != VK_NULL_HANDLE) {
        vkFreeMemory(app.vk.device, app.vk.fxaaMemory, nullptr);
        app.vk.fxaaMemory = VK_NULL_HANDLE;
    }
    if (app.aa.aaMode != AAMode::FXAA) return true;
    const VkExtent2D& e = app.vk.swapchainExtent;
    if (e.width == 0 || e.height == 0) return false;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = app.vk.swapchainFormat;
    imgInfo.extent = {e.width, e.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.vk.device, &imgInfo, nullptr, &app.vk.fxaaImage));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(app.vk.device, app.vk.fxaaImage, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.vk.physicalDevice, &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { memTypeIndex = i; break; }
    }
    if (memTypeIndex == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.vk.device, &allocInfo, nullptr, &app.vk.fxaaMemory));
    VKB_TRY(vkBindImageMemory(app.vk.device, app.vk.fxaaImage, app.vk.fxaaMemory, 0));

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = app.vk.fxaaImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = app.vk.swapchainFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.vk.device, &vi, nullptr, &app.vk.fxaaView));

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VKB_TRY(vkCreateSampler(app.vk.device, &si, nullptr, &app.vk.fxaaSampler));

 // descriptor set layout：内容不随尺寸变化，首次创建后复用（避免 resize 后失效）
    if (app.vk.fxaaDescriptorLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{};
        dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dl.bindingCount = 1;
        dl.pBindings = &binding;
        VKB_TRY(vkCreateDescriptorSetLayout(app.vk.device, &dl, nullptr, &app.vk.fxaaDescriptorLayout));
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dp{};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.maxSets = 1;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &poolSize;
    VKB_TRY(vkCreateDescriptorPool(app.vk.device, &dp, nullptr, &app.vk.fxaaDescriptorPool));

    VkDescriptorSetAllocateInfo da{};
    da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = app.vk.fxaaDescriptorPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &app.vk.fxaaDescriptorLayout;
    VKB_TRY(vkAllocateDescriptorSets(app.vk.device, &da, &app.vk.fxaaDescriptorSet));

    VkDescriptorImageInfo dii{};
    dii.sampler = app.vk.fxaaSampler;
    dii.imageView = app.vk.fxaaView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = app.vk.fxaaDescriptorSet;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.vk.device, 1, &wd, 0, nullptr);
    return true;
}
bool CreatePipelineFXAA(App& app) {
    if (app.aa.aaMode != AAMode::FXAA) return true;
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kFxaaVertSpv, kFxaaVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kFxaaFragSpv, kFxaaFragSpvSize, fragModule)) {
        SetError("FXAA 着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

 // 深度模板：后处理不测试/不写深度；传统 render pass 有 depth 附件必须非 NULL（同 rounded_rect）
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 2 * sizeof(float);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &app.vk.fxaaDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &app.vk.fxaaPipelineLayout));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
 pipelineInfo.pDepthStencilState = &depthStencil; // 传统 render pass 有 depth 附件，必须非 NULL
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.vk.fxaaPipelineLayout;
    pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                             nullptr, &app.vk.fxaaPipeline);
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}
bool CreateOutlineResources(App& app) {
    const VkExtent2D& e = app.vk.swapchainExtent;
    if (e.width == 0 || e.height == 0) return false;
 // 销毁旧资源（resize 重建；descriptor layout 复用不销毁）
    if (app.vk.outlineView)   vkDestroyImageView(app.vk.device, app.vk.outlineView, nullptr);
    if (app.vk.outlineImage)  vkDestroyImage(app.vk.device, app.vk.outlineImage, nullptr);
    if (app.vk.outlineMemory) vkFreeMemory(app.vk.device, app.vk.outlineMemory, nullptr);
    if (app.vk.outlineDepthView)   vkDestroyImageView(app.vk.device, app.vk.outlineDepthView, nullptr);
    if (app.vk.outlineDepthImage)  vkDestroyImage(app.vk.device, app.vk.outlineDepthImage, nullptr);
    if (app.vk.outlineDepthMemory) vkFreeMemory(app.vk.device, app.vk.outlineDepthMemory, nullptr);
    app.vk.outlineView = VK_NULL_HANDLE; app.vk.outlineImage = VK_NULL_HANDLE; app.vk.outlineMemory = VK_NULL_HANDLE;
    app.vk.outlineDepthView = VK_NULL_HANDLE; app.vk.outlineDepthImage = VK_NULL_HANDLE; app.vk.outlineDepthMemory = VK_NULL_HANDLE;

 // mask RT（R8G8B8A8 UNORM，COLOR_ATTACHMENT | SAMPLED）
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = {e.width, e.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.vk.device, &imgInfo, nullptr, &app.vk.outlineImage));

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.vk.physicalDevice, &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { memTypeIndex = i; break; }
    if (memTypeIndex == UINT32_MAX) return false;
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(app.vk.device, app.vk.outlineImage, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.vk.device, &allocInfo, nullptr, &app.vk.outlineMemory));
    VKB_TRY(vkBindImageMemory(app.vk.device, app.vk.outlineImage, app.vk.outlineMemory, 0));

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = app.vk.outlineImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.vk.device, &vi, nullptr, &app.vk.outlineView));

 // mask pass 专用深度（1x D32，独立于主帧——主帧深度可能是 MSAA 采样数，不能直接复用）
    VkImageCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    di.imageType = VK_IMAGE_TYPE_2D;
    di.format = VK_FORMAT_D32_SFLOAT;
    di.extent = {e.width, e.height, 1};
    di.mipLevels = 1;
    di.arrayLayers = 1;
    di.samples = VK_SAMPLE_COUNT_1_BIT;
    di.tiling = VK_IMAGE_TILING_OPTIMAL;
    di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    di.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    di.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.vk.device, &di, nullptr, &app.vk.outlineDepthImage));
    VkMemoryRequirements dmr;
    vkGetImageMemoryRequirements(app.vk.device, app.vk.outlineDepthImage, &dmr);
    VkMemoryAllocateInfo da{};
    da.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    da.allocationSize = dmr.size;
    da.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.vk.device, &da, nullptr, &app.vk.outlineDepthMemory));
    VKB_TRY(vkBindImageMemory(app.vk.device, app.vk.outlineDepthImage, app.vk.outlineDepthMemory, 0));
    VkImageViewCreateInfo dv{};
    dv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dv.image = app.vk.outlineDepthImage;
    dv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dv.format = VK_FORMAT_D32_SFLOAT;
    dv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dv.subresourceRange.levelCount = 1;
    dv.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.vk.device, &dv, nullptr, &app.vk.outlineDepthView));

 // sampler（NEAREST：Sobel 需要精确 texel 采样）
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VKB_TRY(vkCreateSampler(app.vk.device, &si, nullptr, &app.vk.outlineSampler));

    if (app.vk.outlineDescriptorLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{};
        dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dl.bindingCount = 1;
        dl.pBindings = &binding;
        VKB_TRY(vkCreateDescriptorSetLayout(app.vk.device, &dl, nullptr, &app.vk.outlineDescriptorLayout));
    }
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dp{};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.maxSets = 1;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &poolSize;
    VKB_TRY(vkCreateDescriptorPool(app.vk.device, &dp, nullptr, &app.vk.outlineDescriptorPool));
    VkDescriptorSetAllocateInfo dsa{};
    dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa.descriptorPool = app.vk.outlineDescriptorPool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &app.vk.outlineDescriptorLayout;
    VKB_TRY(vkAllocateDescriptorSets(app.vk.device, &dsa, &app.vk.outlineDescriptorSet));
    VkDescriptorImageInfo dii{};
    dii.sampler = app.vk.outlineSampler;
    dii.imageView = app.vk.outlineView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = app.vk.outlineDescriptorSet;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.vk.device, 1, &wd, 0, nullptr);
    return true;
}
bool CreatePipelineOutline(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kOutlineVertSpv, kOutlineVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kOutlineFragSpv, kOutlineFragSpvSize, fragModule)) {
        SetError("选中描边着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
 // alpha blend：黄线（alpha=1）覆盖，非边缘（alpha=0）保留底下画面
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 2 * sizeof(float);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &app.vk.outlineDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &app.vk.outlinePipelineLayout));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.vk.outlinePipelineLayout;
    pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
    pipelineInfo.subpass = 0;
    VkResult res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                             nullptr, &app.vk.outlinePipeline);
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}
bool CreateDepthResources(App& app) {
    if (app.vk.depthImage != VK_NULL_HANDLE) {
        vkDestroyImageView(app.vk.device, app.vk.depthView, nullptr);
        vkDestroyImage(app.vk.device, app.vk.depthImage, nullptr);
        vkFreeMemory(app.vk.device, app.vk.depthMemory, nullptr);
        app.vk.depthView = VK_NULL_HANDLE;
        app.vk.depthImage = VK_NULL_HANDLE;
        app.vk.depthMemory = VK_NULL_HANDLE;
    }
    const VkExtent2D& e = app.vk.swapchainExtent;
    if (e.width == 0 || e.height == 0) return false;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = app.vk.depthFormat;
    imgInfo.extent = {e.width, e.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = app.aa.msaaSamples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.vk.device, &imgInfo, nullptr, &app.vk.depthImage));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(app.vk.device, app.vk.depthImage, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.vk.physicalDevice, &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { memTypeIndex = i; break; }
    }
    if (memTypeIndex == UINT32_MAX) { SetError("无设备本地内存"); return false; }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.vk.device, &allocInfo, nullptr, &app.vk.depthMemory));
    VKB_TRY(vkBindImageMemory(app.vk.device, app.vk.depthImage, app.vk.depthMemory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = app.vk.depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = app.vk.depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.vk.device, &viewInfo, nullptr, &app.vk.depthView));
    return true;
}
bool CreatePipelineGrid(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kGridVertSpv, kGridVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kGridFragSpv, kGridFragSpvSize, fragModule)) {
        SetError("网格着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.aa.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 160;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &app.vk.grid.layout));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.vk.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.vk.grid.layout;
    pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.vk.grid.pipeline);
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}
bool CreatePipelineSolid(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kUnified3dVertSpv, kUnified3dVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kUnified3dFragSpv, kUnified3dFragSpvSize, fragModule)) {
        SetError("实体着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

 // 颜色位深动态切换5 种管线 —— 无颜色 / 16bit / 8bit / 4bit / 1bit
 // 顶点布局：pos(12) + normal(12) + color(位深决定)；无颜色模式只读 pos+normal
    VkVertexInputAttributeDescription attrsNoColor[2]{};
    attrsNoColor[0].location = 0; attrsNoColor[0].binding = 0;
    attrsNoColor[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrsNoColor[0].offset = 0;
    attrsNoColor[1].location = 1; attrsNoColor[1].binding = 0;
    attrsNoColor[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrsNoColor[1].offset = 12;

    VkVertexInputAttributeDescription attrsColor[5][3]{};
    for (int m = 1; m <= 4; ++m) {
        attrsColor[m][0].location = 0; attrsColor[m][0].binding = 0;
        attrsColor[m][0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrsColor[m][0].offset = 0;
        attrsColor[m][1].location = 1; attrsColor[m][1].binding = 0;
        attrsColor[m][1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrsColor[m][1].offset = 12;
        attrsColor[m][2].location = 2; attrsColor[m][2].binding = 0;
        attrsColor[m][2].offset = 24;
 // 16bit=半精度 float4；8bit=UNORM uchar4；4bit=打包 4×4bit；1bit=单字节灰度（R8）
        attrsColor[m][2].format =
            (m == 1) ? VK_FORMAT_R16G16B16A16_SFLOAT :
            (m == 2) ? VK_FORMAT_R8G8B8A8_UNORM :
            (m == 3) ? VK_FORMAT_R4G4B4A4_UNORM_PACK16 :
                       VK_FORMAT_R8_UNORM;
    }
 // 各模式的顶点 stride：24（无颜色）/ 32 / 28 / 26 / 25
 // （全局 kColorStride 定义于 VertexSolid 之后）

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.aa.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Push3D);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &app.vk.pipelineLayoutSolid));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.vk.depthFormat;

    auto createSolid = [&](uint32_t stride, const VkVertexInputAttributeDescription* attrs,
                           uint32_t attrCount, VkPipeline& out) -> bool {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = stride;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = attrCount;
        vertexInput.pVertexAttributeDescriptions = attrs;

        VkPipelineRasterizationStateCreateInfo ras = rasterizer;
        ras.polygonMode = VK_POLYGON_MODE_FILL;
        VkPipelineMultisampleStateCreateInfo ms = multisample;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &ras;
        pipelineInfo.pMultisampleState = &ms;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = app.vk.pipelineLayoutSolid;
        pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
        pipelineInfo.subpass = 0;
        return vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out) == VK_SUCCESS;
    };
 // 实体组（FILL）
    if (!createSolid(kColorStride[0], attrsNoColor, 2, app.vk.pipelineSolidNoColor) ||
        !createSolid(kColorStride[1], attrsColor[1], 3, app.vk.pipelineSolid) ||
        !createSolid(kColorStride[2], attrsColor[2], 3, app.vk.pipelineSolid8) ||
        !createSolid(kColorStride[3], attrsColor[3], 3, app.vk.pipelineSolid4) ||
        !createSolid(kColorStride[4], attrsColor[4], 3, app.vk.pipelineSolid1)) {
        SetError("实体管线创建失败（颜色位深 5 模式）");
        vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
        vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
        return false;
    }
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    return true;
}
bool CreatePipelineLine3d(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kLine3dVertSpv, kLine3dVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kLine3dFragSpv, kLine3dFragSpvSize, fragModule)) {
        SetError("3D 线框着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

 // VertexSolid 布局：pos(0) + normal(12) + color(24)，stride 40；只读 pos 和 color
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 40;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = 24;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

 // 深度：测试开（LEQUAL 保证线不被同深度面遮挡）、不写深度
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.aa.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
 pushRange.size = 64; // 仅 mvp

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &app.vk.pipelineLayoutLine3d));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.vk.depthFormat;

 // 共用同一布局；变体：
 // pipelineLine3d LINE_LIST depthTest=TRUE lineWidth=1（高亮框/线框预览）
 // pipelineLine3dWide LINE_LIST depthTest=TRUE lineWidth=2线框模式加粗，Blender 边框
 // pipelineLine3dNoDepth LINE_LIST depthTest=FALSE（三向标线框：最顶层始终可见）
 // pipelineGizmoSolid TRIANGLE_LIST depthTest=FALSE（三向标锥头实体填充，）
    const auto makePipe = [&](VkBool32 depthTest, VkBool32 depthWrite,
                              VkPrimitiveTopology topo, float lineW, VkPipeline& out) -> bool {
        VkPipelineDepthStencilStateCreateInfo ds = depthStencil;
        ds.depthTestEnable = depthTest;
        ds.depthWriteEnable = depthWrite;
        VkPipelineInputAssemblyStateCreateInfo ia = inputAssembly;
        ia.topology = topo;
        VkPipelineRasterizationStateCreateInfo ras = rasterizer;
        ras.lineWidth = lineW;
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &ia;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &ras;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &ds;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = app.vk.pipelineLayoutLine3d;
        pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
        pipelineInfo.subpass = 0;
        return vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out) == VK_SUCCESS;
    };
 // fix：宽线管线（lineWidth=2）创建非致命——部分驱动/硬件不支持宽线，
 // 失败时保留 NULL，DrawFrame 已有回退到 1px line3d 管线的逻辑
    const bool ok =
        makePipe(VK_TRUE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 1.0f, app.vk.pipelineLine3d) &&
        makePipe(VK_FALSE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 1.0f, app.vk.pipelineLine3dNoDepth) &&
        makePipe(VK_FALSE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 1.0f, app.vk.pipelineGizmoSolid);
 // 宽线独立尝试（失败不影响 ok）
    if (ok && !makePipe(VK_TRUE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 2.0f, app.vk.pipelineLine3dWide)) {
 app.vk.pipelineLine3dWide = VK_NULL_HANDLE; // 显式置空（makePipe 失败时不保证清零）
        VkbLog("[warn] pipelineLine3dWide 创建失败（wideLines 不支持或驱动限制），回退 1px");
    }
 // 无深度宽线（旋转 gizmo 环 2px）——失败回退 1px 无深度管线
    if (ok && !makePipe(VK_FALSE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 2.0f, app.vk.pipelineLine3dNoDepthWide)) {
        app.vk.pipelineLine3dNoDepthWide = VK_NULL_HANDLE;
        VkbLog("[warn] pipelineLine3dNoDepthWide 创建失败，旋转环回退 1px");
    }
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    return ok;
}
bool CreateCommandResources(App& app) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = app.vk.graphicsFamily;
    VKB_TRY(vkCreateCommandPool(app.vk.device, &poolInfo, nullptr, &app.vk.commandPool));

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = app.vk.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VKB_TRY(vkAllocateCommandBuffers(app.vk.device, &allocInfo, &app.vk.commandBuffer));

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VKB_TRY(vkCreateSemaphore(app.vk.device, &semInfo, nullptr, &app.vk.imageAvailable));
    VKB_TRY(vkCreateSemaphore(app.vk.device, &semInfo, nullptr, &app.vk.renderFinished));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VKB_TRY(vkCreateFence(app.vk.device, &fenceInfo, nullptr, &app.vk.inFlightFence));
    return true;
}
void CmdImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
                     VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void ImageBarrierAspect(App& app, VkImage image, VkImageAspectFlags aspect,
                        VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
                        VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    CmdImageBarrier(app.vk.commandBuffer, image, aspect, oldLayout, newLayout,
                    srcStage, srcAccess, dstStage, dstAccess);
}
void ImageBarrier(App& app, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
                  VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    ImageBarrierAspect(app, image, VK_IMAGE_ASPECT_COLOR_BIT, oldLayout, newLayout,
                       srcStage, srcAccess, dstStage, dstAccess);
}
void TransitionBeforeRender(App& app, VkImage image) {
    ImageBarrier(app, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}
void TransitionAfterRender(App& app, VkImage image) {
    ImageBarrier(app, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
}
uint32_t FindMemoryType(const App& app, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.vk.physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return UINT32_MAX;
}
VkCommandBuffer BeginOneTimeCommand(const App& app) {
    VkCommandBufferAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    a.commandPool = app.vk.commandPool;
    a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    a.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(app.vk.device, &a, &cmd);
    VkCommandBufferBeginInfo b{};
    b.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &b);
    return cmd;
}
void EndOneTimeCommand(const App& app, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    s.commandBufferCount = 1;
    s.pCommandBuffers = &cmd;
    vkQueueSubmit(app.vk.graphicsQueue, 1, &s, VK_NULL_HANDLE);
    vkQueueWaitIdle(app.vk.graphicsQueue);
    vkFreeCommandBuffers(app.vk.device, app.vk.commandPool, 1, &cmd);
}
bool CreateTextResources(App& app, const std::vector<uint8_t>& rgba, int w, int h) {
    app.vk.textWidth = w;
    app.vk.textHeight = h;
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = imageSize;
        b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.vk.device, &b, nullptr, &stagingBuffer));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.vk.device, stagingBuffer, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX) { SetError("文字纹理：找不到主机可见内存"); return false; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        VKB_TRY(vkAllocateMemory(app.vk.device, &a, nullptr, &stagingMemory));
        VKB_TRY(vkBindBufferMemory(app.vk.device, stagingBuffer, stagingMemory, 0));
        void* data = nullptr;
        VKB_TRY(vkMapMemory(app.vk.device, stagingMemory, 0, imageSize, 0, &data));
        std::memcpy(data, rgba.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(app.vk.device, stagingMemory);
    }

    VkImageCreateInfo img{};
    img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType = VK_IMAGE_TYPE_2D;
    img.format = VK_FORMAT_R8G8B8A8_UNORM;
    img.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.vk.device, &img, nullptr, &app.vk.textImage));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(app.vk.device, app.vk.textImage, &mr);
    const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (idx == UINT32_MAX) { SetError("文字纹理：找不到设备本地内存"); return false; }
    VkMemoryAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    a.allocationSize = mr.size;
    a.memoryTypeIndex = idx;
    VKB_TRY(vkAllocateMemory(app.vk.device, &a, nullptr, &app.vk.textMemory));
    VKB_TRY(vkBindImageMemory(app.vk.device, app.vk.textImage, app.vk.textMemory, 0));

    {
        VkCommandBuffer cmd = BeginOneTimeCommand(app);
        CmdImageBarrier(cmd, app.vk.textImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, app.vk.textImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        CmdImageBarrier(cmd, app.vk.textImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        EndOneTimeCommand(app, cmd);
    }
    vkDestroyBuffer(app.vk.device, stagingBuffer, nullptr);
    vkFreeMemory(app.vk.device, stagingMemory, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = app.vk.textImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.vk.device, &vi, nullptr, &app.vk.textView));

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VKB_TRY(vkCreateSampler(app.vk.device, &si, nullptr, &app.vk.textSampler));

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{};
    dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dl.bindingCount = 1;
    dl.pBindings = &binding;
    VKB_TRY(vkCreateDescriptorSetLayout(app.vk.device, &dl, nullptr, &app.vk.textDescriptorLayout));

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
 poolSize.descriptorCount = 16; // 8→16（球3 + 变换3 共 6 个新图标）
    VkDescriptorPoolCreateInfo dp{};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
 dp.maxSets = 16; // 支持 16 个 descriptor set（球/变换/顶栏图标预留充足）
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &poolSize;
    VKB_TRY(vkCreateDescriptorPool(app.vk.device, &dp, nullptr, &app.vk.textDescriptorPool));

    VkDescriptorSetAllocateInfo da{};
    da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = app.vk.textDescriptorPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &app.vk.textDescriptorLayout;
    VKB_TRY(vkAllocateDescriptorSets(app.vk.device, &da, &app.vk.textDescriptorSet));

    VkDescriptorImageInfo dii{};
    dii.sampler = app.vk.textSampler;
    dii.imageView = app.vk.textView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = app.vk.textDescriptorSet;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.vk.device, 1, &wd, 0, nullptr);
    return true;
}
bool CreateIconTexture(App& app, const std::vector<uint8_t>& rgba, int w, int h,
                       VkImage& outImage, VkDeviceMemory& outMemory,
                       VkImageView& outView, VkDescriptorSet& outSet) {
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = imageSize;
        b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.vk.device, &b, nullptr, &stagingBuffer));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.vk.device, stagingBuffer, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX) { SetError("图标纹理：找不到主机可见内存"); return false; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        VKB_TRY(vkAllocateMemory(app.vk.device, &a, nullptr, &stagingMemory));
        VKB_TRY(vkBindBufferMemory(app.vk.device, stagingBuffer, stagingMemory, 0));
        void* data = nullptr;
        VKB_TRY(vkMapMemory(app.vk.device, stagingMemory, 0, imageSize, 0, &data));
        std::memcpy(data, rgba.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(app.vk.device, stagingMemory);
    }

    VkImageCreateInfo img{};
    img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType = VK_IMAGE_TYPE_2D;
    img.format = VK_FORMAT_R8G8B8A8_UNORM;
    img.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.vk.device, &img, nullptr, &outImage));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(app.vk.device, outImage, &mr);
    const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (idx == UINT32_MAX) { SetError("图标纹理：找不到设备本地内存"); return false; }
    VkMemoryAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    a.allocationSize = mr.size;
    a.memoryTypeIndex = idx;
    VKB_TRY(vkAllocateMemory(app.vk.device, &a, nullptr, &outMemory));
    VKB_TRY(vkBindImageMemory(app.vk.device, outImage, outMemory, 0));

    {
        VkCommandBuffer cmd = BeginOneTimeCommand(app);
        CmdImageBarrier(cmd, outImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, outImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        CmdImageBarrier(cmd, outImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        EndOneTimeCommand(app, cmd);
    }
    vkDestroyBuffer(app.vk.device, stagingBuffer, nullptr);
    vkFreeMemory(app.vk.device, stagingMemory, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = outImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.vk.device, &vi, nullptr, &outView));

    VkDescriptorSetAllocateInfo da{};
    da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = app.vk.textDescriptorPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &app.vk.textDescriptorLayout;
    VKB_TRY(vkAllocateDescriptorSets(app.vk.device, &da, &outSet));

    VkDescriptorImageInfo dii{};
    dii.sampler = app.vk.textSampler;
    dii.imageView = outView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = outSet;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.vk.device, 1, &wd, 0, nullptr);
    return true;
}
bool LoadBallIcons(App& app) {
 // 前 3 = 球按钮（1/2/3 线框/实体/渲染），后 3 = 顶栏变换按钮（4/5/6 移动/旋转/缩放）
    static const WORD kIconResIds[6] = {ID_BTN_ICON_1, ID_BTN_ICON_2, ID_BTN_ICON_3,
                                        ID_BTN_ICON_4, ID_BTN_ICON_5, ID_BTN_ICON_6};
    constexpr int kBallTexSize = 128;
 const HMODULE hMod = GetModuleHandleW(nullptr); // 当前 exe 句柄
    char msg[256];
    for (int i = 0; i < 6; ++i) {
        App::BallIconImage& dst = (i < 3) ? app.ui.ballIcons[i] : app.ui.transformIcons[i - 3];
 // FindResourceA 接受 LPSTR：资源 ID 用 MAKEINTRESOURCE(数字) 走"整型 ID"分支，类型 RT_RCDATA 走"预定义资源类型"分支
        HRSRC hRes = FindResourceA(hMod, MAKEINTRESOURCE(kIconResIds[i]), RT_RCDATA);
        if (!hRes) {
            std::snprintf(msg, sizeof(msg), "[icon] FindResource ID %d 失败", kIconResIds[i]);
            VkbLog(msg);
            continue;
        }
        HGLOBAL hLoaded = LoadResource(hMod, hRes);
        if (!hLoaded) continue;
        const void* data = LockResource(hLoaded);
        const DWORD size = SizeofResource(hMod, hRes);
        if (!data || size == 0) continue;

        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!DecodePngWic(static_cast<const unsigned char*>(data), size, rgba, w, h,
                          kBallTexSize, kBallTexSize)) {
            std::snprintf(msg, sizeof(msg), "[ballicon] WIC 解码图标 %d 失败", i);
            VkbLog(msg);
            continue;
        }
 // 球按钮图标圆形裁剪（圆外 alpha=0）——叠在圆形按钮上，透明角露出按钮底色；
 // 变换按钮（i>=3）是顶栏方形按钮，保持方形不裁剪
        if (i < 3) {
            const float cx = w * 0.5f, cy = h * 0.5f;
 const float r = std::min(w, h) * 0.5f - 1.0f; // 半径略缩 1px 抗锯齿
            const float r2 = r * r;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                    if (dx * dx + dy * dy > r2)
                        rgba[static_cast<size_t>(y * w + x) * 4 + 3] = 0;
                }
        }
        if (!CreateIconTexture(app, rgba, w, h,
                               dst.image, dst.memory,
                               dst.view, dst.set)) {
            std::snprintf(msg, sizeof(msg), "[icon] 图标 %d 纹理创建失败: %s", i, g_error.c_str());
            VkbLog(msg);
            g_error.clear();
            continue;
        }
        dst.w = static_cast<uint32_t>(w);
        dst.h = static_cast<uint32_t>(h);
        dst.valid = true;
        std::snprintf(msg, sizeof(msg), "[icon] 图标 %d OK %dx%d", i, w, h);
        VkbLog(msg);
    }
 return true; // 降级语义：单图失败不影响其他/整体启动
}
bool CreateTextPipeline(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.vk.device, kTextVertSpv, kTextVertSpvSize, vertModule) ||
        !CreateShaderModule(app.vk.device, kTextFragSpv, kTextFragSpvSize, fragModule)) {
        SetError("文字着色器模块创建失败");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.aa.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(TextPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &app.vk.textDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.vk.device, &layoutInfo, nullptr, &app.vk.textPipelineLayout));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.vk.swapchainFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.vk.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.vk.textPipelineLayout;
    pipelineInfo.renderPass = app.vk.useDynamicRendering ? VK_NULL_HANDLE : app.vk.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.vk.textPipeline);
    vkDestroyShaderModule(app.vk.device, vertModule, nullptr);
    vkDestroyShaderModule(app.vk.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}
void DestroyAllPipelines(App& app) {
    if (app.vk.pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipeline, nullptr); app.vk.pipeline = VK_NULL_HANDLE; }
    if (app.vk.pipelinePanelBlend != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelinePanelBlend, nullptr); app.vk.pipelinePanelBlend = VK_NULL_HANDLE; }
    if (app.vk.pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayout, nullptr); app.vk.pipelineLayout = VK_NULL_HANDLE; }
    if (app.vk.menuPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.menuPipeline, nullptr); app.vk.menuPipeline = VK_NULL_HANDLE; }
    if (app.vk.menuPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.menuPipelineLayout, nullptr); app.vk.menuPipelineLayout = VK_NULL_HANDLE; }
    if (app.vk.grid.pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.grid.pipeline, nullptr); app.vk.grid.pipeline = VK_NULL_HANDLE; }
    if (app.vk.grid.layout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.grid.layout, nullptr); app.vk.grid.layout = VK_NULL_HANDLE; }
    if (app.vk.pipelineSolid != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid, nullptr); app.vk.pipelineSolid = VK_NULL_HANDLE; }
    if (app.vk.pipelineSolidNoColor != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineSolidNoColor, nullptr); app.vk.pipelineSolidNoColor = VK_NULL_HANDLE; }
    if (app.vk.pipelineSolid8 != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid8, nullptr); app.vk.pipelineSolid8 = VK_NULL_HANDLE; }
    if (app.vk.pipelineSolid4 != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid4, nullptr); app.vk.pipelineSolid4 = VK_NULL_HANDLE; }
    if (app.vk.pipelineSolid1 != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid1, nullptr); app.vk.pipelineSolid1 = VK_NULL_HANDLE; }
    if (app.vk.pipelineLayoutSolid != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayoutSolid, nullptr); app.vk.pipelineLayoutSolid = VK_NULL_HANDLE; }
    if (app.vk.pipelineAxis != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineAxis, nullptr); app.vk.pipelineAxis = VK_NULL_HANDLE; }
    if (app.vk.pipelineAxisOccluded != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineAxisOccluded, nullptr); app.vk.pipelineAxisOccluded = VK_NULL_HANDLE; }
    if (app.vk.pipelineLayoutAxis != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayoutAxis, nullptr); app.vk.pipelineLayoutAxis = VK_NULL_HANDLE; }
    if (app.vk.pipelineLine3d != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3d, nullptr); app.vk.pipelineLine3d = VK_NULL_HANDLE; }
    if (app.vk.pipelineLine3dWide != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3dWide, nullptr); app.vk.pipelineLine3dWide = VK_NULL_HANDLE; }
    if (app.vk.pipelineLine3dNoDepthWide != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3dNoDepthWide, nullptr); app.vk.pipelineLine3dNoDepthWide = VK_NULL_HANDLE; }
    if (app.vk.pipelineLine3dNoDepth != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3dNoDepth, nullptr); app.vk.pipelineLine3dNoDepth = VK_NULL_HANDLE; }
    if (app.vk.pipelineGizmoSolid != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.pipelineGizmoSolid, nullptr); app.vk.pipelineGizmoSolid = VK_NULL_HANDLE; }
    if (app.vk.pipelineLayoutLine3d != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayoutLine3d, nullptr); app.vk.pipelineLayoutLine3d = VK_NULL_HANDLE; }
    if (app.vk.textPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.textPipeline, nullptr); app.vk.textPipeline = VK_NULL_HANDLE; }
    if (app.vk.textPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.textPipelineLayout, nullptr); app.vk.textPipelineLayout = VK_NULL_HANDLE; }
    if (app.vk.fxaaPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.vk.device, app.vk.fxaaPipeline, nullptr); app.vk.fxaaPipeline = VK_NULL_HANDLE; }
    if (app.vk.fxaaPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.vk.device, app.vk.fxaaPipelineLayout, nullptr); app.vk.fxaaPipelineLayout = VK_NULL_HANDLE; }
}
bool ApplyAAMode(App& app, AAMode mode) {
    if (app.vk.device == VK_NULL_HANDLE) return false;
    if (mode == app.aa.aaMode) return true;
    vkDeviceWaitIdle(app.vk.device);

    app.aa.aaMode = mode;
    app.aa.msaaEnabled = false;
    app.aa.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    switch (mode) {
    case AAMode::MSAA_2x: app.aa.msaaEnabled = true; app.aa.msaaSamples = VK_SAMPLE_COUNT_2_BIT; break;
    case AAMode::MSAA_4x: app.aa.msaaEnabled = true; app.aa.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    case AAMode::SSAA:    app.aa.msaaEnabled = true; app.aa.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    default: break;
    }

    DestroyAllPipelines(app);
 // fxaaDescriptorLayout（CreatePipelineFXAA 依赖它，必须在其之前）。
    if (!RecreateSwapchain(app)) return false;
    if (!CreatePipeline(app) || !CreateMenuPipeline(app) ||
        !CreatePipelineGrid(app) || !CreatePipelineSolid(app) ||
        !CreatePipelineAxis(app) || !CreatePipelineLine3d(app) ||
        !CreatePipelineFXAA(app) || !CreatePipelineOutline(app) ||
        !CreateTextPipeline(app)) {
        SetError("抗锯齿管线重建失败");
        return false;
    }
    return true;
}
void Cleanup(App& app) {
    SaveSettingInt("aa_mode", static_cast<int>(app.aa.aaMode));
    CloseSettingsWindow();
    if (app.vk.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(app.vk.device);
 // 标签纹理释放（image/view/memory；descriptor set 随 textDescriptorPool 统一销毁）
        const auto freeLabel = [&](App::LabelTexture& lt) {
            if (lt.view)   vkDestroyImageView(app.vk.device, lt.view, nullptr);
            if (lt.image)  vkDestroyImage(app.vk.device, lt.image, nullptr);
            if (lt.memory) vkFreeMemory(app.vk.device, lt.memory, nullptr);
            lt.view = VK_NULL_HANDLE; lt.image = VK_NULL_HANDLE; lt.memory = VK_NULL_HANDLE;
        };
        freeLabel(app.ui.objNameLabel);
        freeLabel(app.ui.scaleLabel);
        freeLabel(app.ui.importUpLabel);
        for (auto& lt : app.ui.coordLabels) freeLabel(lt);
 // 选中描边资源释放（descriptor layout 随 device 销毁；pool/pipeline 显式释放）
        if (app.vk.outlinePipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.outlinePipeline, nullptr);
        if (app.vk.outlinePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.outlinePipelineLayout, nullptr);
        if (app.vk.outlineDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(app.vk.device, app.vk.outlineDescriptorPool, nullptr);
        if (app.vk.outlineSampler != VK_NULL_HANDLE) vkDestroySampler(app.vk.device, app.vk.outlineSampler, nullptr);
        if (app.vk.outlineDepthView)  vkDestroyImageView(app.vk.device, app.vk.outlineDepthView, nullptr);
        if (app.vk.outlineDepthImage) vkDestroyImage(app.vk.device, app.vk.outlineDepthImage, nullptr);
        if (app.vk.outlineDepthMemory) vkFreeMemory(app.vk.device, app.vk.outlineDepthMemory, nullptr);
        if (app.vk.outlineView)   vkDestroyImageView(app.vk.device, app.vk.outlineView, nullptr);
        if (app.vk.outlineImage)  vkDestroyImage(app.vk.device, app.vk.outlineImage, nullptr);
        if (app.vk.outlineMemory) vkFreeMemory(app.vk.device, app.vk.outlineMemory, nullptr);
        app.vk.outlinePipeline = VK_NULL_HANDLE; app.vk.outlinePipelineLayout = VK_NULL_HANDLE;
        app.vk.outlineDescriptorPool = VK_NULL_HANDLE; app.vk.outlineSampler = VK_NULL_HANDLE;
        app.vk.outlineDepthView = VK_NULL_HANDLE; app.vk.outlineDepthImage = VK_NULL_HANDLE; app.vk.outlineDepthMemory = VK_NULL_HANDLE;
        app.vk.outlineView = VK_NULL_HANDLE; app.vk.outlineImage = VK_NULL_HANDLE; app.vk.outlineMemory = VK_NULL_HANDLE;
        if (app.vk.inFlightFence != VK_NULL_HANDLE) vkDestroyFence(app.vk.device, app.vk.inFlightFence, nullptr);
        if (app.vk.renderFinished != VK_NULL_HANDLE) vkDestroySemaphore(app.vk.device, app.vk.renderFinished, nullptr);
        if (app.vk.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(app.vk.device, app.vk.imageAvailable, nullptr);
        if (app.vk.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(app.vk.device, app.vk.commandPool, nullptr);
        if (app.vk.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipeline, nullptr);
        if (app.vk.pipelinePanelBlend != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelinePanelBlend, nullptr);
        if (app.vk.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayout, nullptr);
        if (app.vk.menuPipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.menuPipeline, nullptr);
        if (app.vk.menuPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.menuPipelineLayout, nullptr);
        if (app.vk.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.vk.device, app.vk.vertexBuffer, nullptr);
        if (app.vk.vertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.vertexBufferMemory, nullptr);
        if (app.vk.grid.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.grid.pipeline, nullptr);
        if (app.vk.grid.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.grid.layout, nullptr);
        if (app.vk.pipelineSolid != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid, nullptr);
        if (app.vk.pipelineSolidNoColor != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineSolidNoColor, nullptr);
        if (app.vk.pipelineSolid8 != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid8, nullptr);
        if (app.vk.pipelineSolid4 != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid4, nullptr);
        if (app.vk.pipelineSolid1 != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineSolid1, nullptr);
        if (app.vk.pipelineLayoutSolid != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayoutSolid, nullptr);
        if (app.vk.pipelineAxis != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineAxis, nullptr);
        if (app.vk.pipelineAxisOccluded != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineAxisOccluded, nullptr);
        if (app.vk.pipelineLayoutAxis != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayoutAxis, nullptr);
        if (app.vk.pipelineLine3d != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3d, nullptr);
        if (app.vk.pipelineLine3dWide != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3dWide, nullptr);
        if (app.vk.pipelineLine3dNoDepthWide != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3dNoDepthWide, nullptr);
        if (app.vk.pipelineLine3dNoDepth != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineLine3dNoDepth, nullptr);
        if (app.vk.pipelineGizmoSolid != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.pipelineGizmoSolid, nullptr);
        if (app.vk.pipelineLayoutLine3d != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.pipelineLayoutLine3d, nullptr);
        if (app.vk.textPipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.textPipeline, nullptr);
        if (app.vk.textPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.textPipelineLayout, nullptr);
        if (app.vk.textDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(app.vk.device, app.vk.textDescriptorPool, nullptr);
        if (app.vk.textDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(app.vk.device, app.vk.textDescriptorLayout, nullptr);
        if (app.vk.textSampler != VK_NULL_HANDLE) vkDestroySampler(app.vk.device, app.vk.textSampler, nullptr);
        if (app.vk.textView != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.vk.textView, nullptr);
        if (app.vk.textImage != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.vk.textImage, nullptr);
        if (app.vk.textMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.textMemory, nullptr);
        if (app.vk.penView != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.vk.penView, nullptr);
        if (app.vk.penImage != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.vk.penImage, nullptr);
        if (app.vk.penMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.penMemory, nullptr);
        if (app.vk.importView != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.vk.importView, nullptr);
        if (app.vk.importImage != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.vk.importImage, nullptr);
        if (app.vk.importMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.importMemory, nullptr);
        if (app.vk.exportView != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.vk.exportView, nullptr);
        if (app.vk.exportImage != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.vk.exportImage, nullptr);
        if (app.vk.exportMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.exportMemory, nullptr);
 // 球按钮图标（描述集由 textDescriptorPool 统一管理，无需单独释放）
        for (int i = 0; i < 3; ++i) {
            if (app.ui.ballIcons[i].view != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.ui.ballIcons[i].view, nullptr);
            if (app.ui.ballIcons[i].image != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.ui.ballIcons[i].image, nullptr);
            if (app.ui.ballIcons[i].memory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.ui.ballIcons[i].memory, nullptr);
            app.ui.ballIcons[i].view = VK_NULL_HANDLE; app.ui.ballIcons[i].image = VK_NULL_HANDLE;
            app.ui.ballIcons[i].memory = VK_NULL_HANDLE; app.ui.ballIcons[i].set = VK_NULL_HANDLE;
            app.ui.ballIcons[i].valid = false;
        }
 // 顶栏变换按钮图标（移动/旋转/缩放）
        for (int i = 0; i < 3; ++i) {
            if (app.ui.transformIcons[i].view != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.ui.transformIcons[i].view, nullptr);
            if (app.ui.transformIcons[i].image != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.ui.transformIcons[i].image, nullptr);
            if (app.ui.transformIcons[i].memory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.ui.transformIcons[i].memory, nullptr);
            app.ui.transformIcons[i].view = VK_NULL_HANDLE; app.ui.transformIcons[i].image = VK_NULL_HANDLE;
            app.ui.transformIcons[i].memory = VK_NULL_HANDLE; app.ui.transformIcons[i].set = VK_NULL_HANDLE;
            app.ui.transformIcons[i].valid = false;
        }
        if (app.vk.vertexBuffer3D != VK_NULL_HANDLE) vkDestroyBuffer(app.vk.device, app.vk.vertexBuffer3D, nullptr);
        if (app.vk.vertexBufferMemory3D != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.vertexBufferMemory3D, nullptr);
        if (app.vk.indexBuffer3D != VK_NULL_HANDLE) vkDestroyBuffer(app.vk.device, app.vk.indexBuffer3D, nullptr);
        if (app.vk.indexBufferMemory3D != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.indexBufferMemory3D, nullptr);
        if (app.vk.wireVtxBuffer3D != VK_NULL_HANDLE) vkDestroyBuffer(app.vk.device, app.vk.wireVtxBuffer3D, nullptr);
        if (app.vk.wireVtxBufferMemory3D != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.wireVtxBufferMemory3D, nullptr);
        if (app.vk.selVtxBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.vk.device, app.vk.selVtxBuffer, nullptr);
        if (app.vk.selVtxMem != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.selVtxMem, nullptr);
        if (app.vk.gizmoVtxBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.vk.device, app.vk.gizmoVtxBuffer, nullptr);
        if (app.vk.gizmoVtxMem != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.gizmoVtxMem, nullptr);
        if (app.vk.gizmoSolidVtxBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.vk.device, app.vk.gizmoSolidVtxBuffer, nullptr);
        if (app.vk.gizmoSolidVtxMem != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.gizmoSolidVtxMem, nullptr);
        // 物体显示栏标签纹理+ 距离比例尺
        for (App::LabelTexture* lt : {&app.ui.objNameLabel, &app.ui.scaleLabel,
                                       &app.ui.coordLabels[0], &app.ui.coordLabels[1],
                                       &app.ui.coordLabels[2]}) {
            if (lt->view) vkDestroyImageView(app.vk.device, lt->view, nullptr);
            if (lt->image) vkDestroyImage(app.vk.device, lt->image, nullptr);
            if (lt->memory) vkFreeMemory(app.vk.device, lt->memory, nullptr);
            lt->view = VK_NULL_HANDLE; lt->image = VK_NULL_HANDLE; lt->memory = VK_NULL_HANDLE;
        }
        if (app.vk.depthView != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.vk.depthView, nullptr);
        if (app.vk.depthImage != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.vk.depthImage, nullptr);
        if (app.vk.depthMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.depthMemory, nullptr);
        if (app.vk.msaaColorView != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.vk.msaaColorView, nullptr);
        if (app.vk.msaaColorImage != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.vk.msaaColorImage, nullptr);
        if (app.vk.msaaColorMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.msaaColorMemory, nullptr);
        if (app.vk.fxaaPipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.vk.device, app.vk.fxaaPipeline, nullptr);
        if (app.vk.fxaaPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.vk.device, app.vk.fxaaPipelineLayout, nullptr);
        if (app.vk.fxaaDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(app.vk.device, app.vk.fxaaDescriptorPool, nullptr);
        if (app.vk.fxaaDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(app.vk.device, app.vk.fxaaDescriptorLayout, nullptr);
        if (app.vk.fxaaSampler != VK_NULL_HANDLE) vkDestroySampler(app.vk.device, app.vk.fxaaSampler, nullptr);
        if (app.vk.fxaaView != VK_NULL_HANDLE) vkDestroyImageView(app.vk.device, app.vk.fxaaView, nullptr);
        if (app.vk.fxaaImage != VK_NULL_HANDLE) vkDestroyImage(app.vk.device, app.vk.fxaaImage, nullptr);
        if (app.vk.fxaaMemory != VK_NULL_HANDLE) vkFreeMemory(app.vk.device, app.vk.fxaaMemory, nullptr);
        for (VkFramebuffer fb : app.vk.framebuffers) vkDestroyFramebuffer(app.vk.device, fb, nullptr);
        if (app.vk.renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(app.vk.device, app.vk.renderPass, nullptr);
        for (VkImageView view : app.vk.swapchainImageViews) vkDestroyImageView(app.vk.device, view, nullptr);
        if (app.vk.swapchain != VK_NULL_HANDLE) g_pfnDestroySwapchainKHR(app.vk.device, app.vk.swapchain, nullptr);
        vkDestroyDevice(app.vk.device, nullptr);
    }
#ifdef VKB_ENABLE_VALIDATION
    if (g_debugMessenger != VK_NULL_HANDLE && g_pfnDestroyDebugUtilsMessengerEXT) {
        g_pfnDestroyDebugUtilsMessengerEXT(app.vk.instance, g_debugMessenger, nullptr);
    }
#endif
    if (app.vk.surface != VK_NULL_HANDLE) {
        auto pfnDestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            vkGetInstanceProcAddr(app.vk.instance, "vkDestroySurfaceKHR"));
        if (pfnDestroySurfaceKHR) pfnDestroySurfaceKHR(app.vk.instance, app.vk.surface, nullptr);
    }
    if (app.vk.instance != VK_NULL_HANDLE) vkDestroyInstance(app.vk.instance, nullptr);
    if (app.hwnd != nullptr) DestroyWindow(app.hwnd);
}
// 顶点含颜色否决瘦身：以后要做带颜色的光照渲染，颜色必须留在顶点里
// 结构与 SceneObject 已移至 model_import.hSTL/glTF/FBX 共享

// 颜色位深模式0=无颜色 1=16bit 2=8bit 3=4bit 4=1bit → 顶点 stride
