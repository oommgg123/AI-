// ============================================================================
//
//
// ============================================================================
#pragma once

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES  // 移除函数原型声明（动态加载，避免与函数指针变量重名冲突）
#endif

#include <windows.h>
#include <vulkan/vulkan.h>

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#define VK_GLOBAL_FUNCS(X) \
    X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties)

#define VK_INSTANCE_FUNCS(X) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceFormatProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetDeviceProcAddr) \
    X(vkCreateDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer) \
    X(vkCreateFence) X(vkWaitForFences) X(vkResetFences) X(vkDestroyFence) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkAllocateMemory) X(vkFreeMemory) \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkGetBufferMemoryRequirements) X(vkGetImageMemoryRequirements) \
    X(vkCreateImage) X(vkDestroyImage) X(vkBindImageMemory) \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
    X(vkCreateRenderPass) X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) \
    X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers) X(vkCmdBindDescriptorSets) \
    X(vkCmdPushConstants) X(vkCmdSetViewport) X(vkCmdSetScissor) \
    X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdBindIndexBuffer) \
    X(vkCmdCopyBuffer) X(vkCmdCopyBufferToImage) X(vkCmdPipelineBarrier) \
    X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkDestroyInstance) X(vkDestroyDevice)

// 注意：vkCmdBeginRendering / vkCmdEndRendering 是 Vulkan 1.3 core 函数，**不放入**上述
// 强制加载列表（VK_INSTANCE_FUNCS 加载失败会导致 LoadInstanceProcs 返回 false）。
// 仅在 useDynamicRendering == true（驱动支持 1.3）时才调用——兼容 Vulkan 1.0/1.1 老驱动、

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#define DECL_PFN_EXTERN(name) extern PFN_##name name;
VK_GLOBAL_FUNCS(DECL_PFN_EXTERN)
VK_INSTANCE_FUNCS(DECL_PFN_EXTERN)
#undef DECL_PFN_EXTERN

extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
extern PFN_vkCreateWin32SurfaceKHR            g_pfnCreateWin32SurfaceKHR;
extern PFN_vkGetPhysicalDeviceSurfaceSupportKHR g_pfnGetPhysicalDeviceSurfaceSupportKHR;
extern PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR g_pfnGetPhysicalDeviceSurfaceCapabilitiesKHR;
extern PFN_vkGetPhysicalDeviceSurfaceFormatsKHR g_pfnGetPhysicalDeviceSurfaceFormatsKHR;
extern PFN_vkGetPhysicalDeviceSurfacePresentModesKHR g_pfnGetPhysicalDeviceSurfacePresentModesKHR;
extern PFN_vkGetPhysicalDeviceFeatures2        g_pfnGetPhysicalDeviceFeatures2;
extern PFN_vkCmdBeginRendering                 g_pfnCmdBeginRendering;
extern PFN_vkCmdEndRendering                   g_pfnCmdEndRendering;
extern PFN_vkCreateSwapchainKHR                g_pfnCreateSwapchainKHR;
extern PFN_vkDestroySwapchainKHR               g_pfnDestroySwapchainKHR;
extern PFN_vkGetSwapchainImagesKHR             g_pfnGetSwapchainImagesKHR;
extern PFN_vkAcquireNextImageKHR               g_pfnAcquireNextImageKHR;
extern PFN_vkQueuePresentKHR                   g_pfnQueuePresentKHR;

#ifdef VKB_ENABLE_VALIDATION
extern PFN_vkCreateDebugUtilsMessengerEXT  g_pfnCreateDebugUtilsMessengerEXT;
extern PFN_vkDestroyDebugUtilsMessengerEXT g_pfnDestroyDebugUtilsMessengerEXT;
extern VkDebugUtilsMessengerEXT            g_debugMessenger;
#endif

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 诊断日志：写 exe 同目录单个 awa.log（无文件夹；每次运行按时间戳记录；成功静默、失败时由 SetError/Load* 调用）
void VkbLog(const char* msg);

bool LoadVulkanCore();

// 加载失败原因（UTF-8，供主程序弹窗显示具体原因；成功时为 nullptr）
extern const char* g_loaderError;

bool LoadInstanceProcs(VkInstance instance);

bool LoadDeviceProcs(VkDevice device);

// 软件 ICD 兜底：设置 VK_ICD_FILENAMES 环境变量指向「资源」文件夹内置的
bool EnableSoftwareIcd();
