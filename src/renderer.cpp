// ============================================================================
//  renderer.cpp（#208 拆分：原 main.cpp ）
// ============================================================================
#include "awa_internal.h"

using std::uint32_t;

// 上一帧 vkQueueSubmit 失败后栅栏已未置位且不会自行置位；置位此标志让下一帧跳过 vkWaitForFences，
// 避免每帧等满 10s 的伪卡死（详见 DrawFrame / EndFrameAndPresent）。
static bool s_skipFenceWait = false;

void DrawSelectionOutline(App& app, SceneObject& sel, int selIndex, const float mvp[16]) { // 非 const（延迟构建特征边）
    if (app.vk.pipelineLine3d == VK_NULL_HANDLE) return;
 // 缓存失效重建（拓扑不变仅变换时无需重建）
    if (app.scene.selSilIndex != selIndex || app.scene.selSilName != sel.name) {
        sel.featureVerts.clear(); // 缓存失效时释放旧物体特征边（延迟构建省内存）
        BuildSelSilhouette(sel, app);
        app.scene.selSilIndex = selIndex;
        app.scene.selSilName = sel.name;
    }
    enum : int { kSil = 0, kFeature, kIndexed, kPairs, kAabb } mode = kSil;
    const bool big = app.scene.selSilA.size() > 200000; // 大物体免逐帧判定（轮廓边过多）
    if (app.scene.selSilA.empty() || big) {
 // 特征边延迟构建（首次选中才生成；大物体 silhouette 过大回退用）
        if (sel.featureVerts.empty() && !sel.solidIndices.empty() && !sel.solidVerts.empty())
            BuildFeatureVerts(sel);
        if (!sel.featureVerts.empty())      mode = kFeature;
        else if (!sel.wireVerts.empty() && !sel.wireIndices.empty()) mode = kIndexed;
        else if (!sel.wireVerts.empty())    mode = kPairs;
        else                                mode = kAabb;
    }
    size_t maxVerts = 0;
    switch (mode) {
        case kSil:     maxVerts = app.scene.selSilA.size() * 2; break;
        case kFeature: maxVerts = sel.featureVerts.size(); break;
        case kIndexed: maxVerts = sel.wireIndices.size(); break;
        case kPairs:   maxVerts = sel.wireVerts.size(); break;
        case kAabb:    maxVerts = 24; break;
    }
    if (maxVerts == 0) return;

 // 确保顶点缓冲容量（复用 selVtxBuffer/selVtxMem/selVtxCapacity）
    const VkDeviceSize need = static_cast<VkDeviceSize>(maxVerts) * 40;
    if (app.vk.selVtxBuffer != VK_NULL_HANDLE && app.vk.selVtxCapacity < need) {
        vkDestroyBuffer(app.vk.device, app.vk.selVtxBuffer, nullptr);
        vkFreeMemory(app.vk.device, app.vk.selVtxMem, nullptr);
        app.vk.selVtxBuffer = VK_NULL_HANDLE;
        app.vk.selVtxMem = VK_NULL_HANDLE;
        app.vk.selVtxCapacity = 0;
    }
    if (app.vk.selVtxBuffer == VK_NULL_HANDLE) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = need;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(app.vk.device, &bi, nullptr, &app.vk.selVtxBuffer);
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.vk.device, app.vk.selVtxBuffer, &mr);
        const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = mi;
        vkAllocateMemory(app.vk.device, &ai, nullptr, &app.vk.selVtxMem);
        vkBindBufferMemory(app.vk.device, app.vk.selVtxBuffer, app.vk.selVtxMem, 0);
        app.vk.selVtxCapacity = need;
    }

    float model[16] = {};
    BuildModelMatrix(sel, model);
    void* p = nullptr;
    if (vkMapMemory(app.vk.device, app.vk.selVtxMem, 0, need, 0, &p) != VK_SUCCESS) return;
    VertexSolid* v = static_cast<VertexSolid*>(p);
    size_t vi = 0;
    if (mode == kSil) {
 // 视相关轮廓：边中点→相机方向，两邻接面法线一正一负（或边界边）→ 轮廓
        const float r00 = model[0], r01 = model[1], r02 = model[2], tx = model[12];
        const float r10 = model[4], r11 = model[5], r12 = model[6], ty = model[13];
        const float r20 = model[8], r21 = model[9], r22 = model[10], tz = model[14];
        const auto& sv = sel.solidVerts;
        const float cxp = app.camera.position.x, cyp = app.camera.position.y, czp = app.camera.position.z;
        for (size_t i = 0; i < app.scene.selSilA.size(); ++i) {
            const uint32_t ia = app.scene.selSilA[i], ib = app.scene.selSilB[i];
            if (ia >= sv.size() || ib >= sv.size()) continue;
            const VertexSolid& va = sv[ia];
            const VertexSolid& vb = sv[ib];
            const float wax = r00 * va.pos[0] + r01 * va.pos[1] + r02 * va.pos[2] + tx;
            const float way = r10 * va.pos[0] + r11 * va.pos[1] + r12 * va.pos[2] + ty;
            const float waz = r20 * va.pos[0] + r21 * va.pos[1] + r22 * va.pos[2] + tz;
            const float wbx = r00 * vb.pos[0] + r01 * vb.pos[1] + r02 * vb.pos[2] + tx;
            const float wby = r10 * vb.pos[0] + r11 * vb.pos[1] + r12 * vb.pos[2] + ty;
            const float wbz = r20 * vb.pos[0] + r21 * vb.pos[1] + r22 * vb.pos[2] + tz;
            const float mdx = (wax + wbx) * 0.5f - cxp;
            const float mdy = (way + wby) * 0.5f - cyp;
            const float mdz = (waz + wbz) * 0.5f - czp;
            const float inv = 1.0f / (std::sqrt(mdx * mdx + mdy * mdy + mdz * mdz) + 1e-9f);
            const float cdx = mdx * inv, cdy = mdy * inv, cdz = mdz * inv;
            const float n0x = app.scene.selSilN0[i * 3], n0y = app.scene.selSilN0[i * 3 + 1], n0z = app.scene.selSilN0[i * 3 + 2];
            const float n1x = app.scene.selSilN1[i * 3], n1y = app.scene.selSilN1[i * 3 + 1], n1z = app.scene.selSilN1[i * 3 + 2];
            bool draw;
            if (n1x == 0.0f && n1y == 0.0f && n1z == 0.0f) {
 draw = true; // 边界边（开放网格外沿）恒画
            } else {
                const float d0 = (r00 * n0x + r01 * n0y + r02 * n0z) * cdx +
                                 (r10 * n0x + r11 * n0y + r12 * n0z) * cdy +
                                 (r20 * n0x + r21 * n0y + r22 * n0z) * cdz;
                const float d1 = (r00 * n1x + r01 * n1y + r02 * n1z) * cdx +
                                 (r10 * n1x + r11 * n1y + r12 * n1z) * cdy +
                                 (r20 * n1x + r21 * n1y + r22 * n1z) * cdz;
 draw = (d0 * d1 < 0.0f); // 一正面一背面 → 轮廓边
            }
            if (draw) { v[vi++] = va; v[vi++] = vb; }
        }
    } else if (mode == kFeature) {
        for (const auto& a : sel.featureVerts) v[vi++] = a;
    } else if (mode == kIndexed) {
        for (size_t i = 0; i + 1 < sel.wireIndices.size(); i += 2) {
            const uint32_t a = sel.wireIndices[i], b = sel.wireIndices[i + 1];
            if (a < sel.wireVerts.size() && b < sel.wireVerts.size()) {
                v[vi].pos[0] = sel.wireVerts[a].pos[0]; v[vi].pos[1] = sel.wireVerts[a].pos[1]; v[vi].pos[2] = sel.wireVerts[a].pos[2]; ++vi;
                v[vi].pos[0] = sel.wireVerts[b].pos[0]; v[vi].pos[1] = sel.wireVerts[b].pos[1]; v[vi].pos[2] = sel.wireVerts[b].pos[2]; ++vi;
            }
        }
    } else if (mode == kPairs) {
        for (const auto& a : sel.wireVerts) {
 v[vi].pos[0] = a.pos[0]; v[vi].pos[1] = a.pos[1]; v[vi].pos[2] = a.pos[2]; // WirePos 仅坐标（颜色由下方统一黄循环补）
            ++vi;
        }
 } else { // kAabb：黄色 AABB 12 边
        const float minx = sel.boundsMin[0], miny = sel.boundsMin[1], minz = sel.boundsMin[2];
        const float maxx = sel.boundsMax[0], maxy = sel.boundsMax[1], maxz = sel.boundsMax[2];
        const float c[8][3] = {
            {minx, miny, minz}, {maxx, miny, minz}, {minx, maxy, minz}, {maxx, maxy, minz},
            {minx, miny, maxz}, {maxx, miny, maxz}, {minx, maxy, maxz}, {maxx, maxy, maxz}};
        const int edges[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{3,7},{2,6}};
        for (int e = 0; e < 12; ++e) for (int k = 0; k < 2; ++k) {
            VertexSolid& vv = v[vi++];
            const int ci = edges[e][k];
            vv.pos[0] = c[ci][0]; vv.pos[1] = c[ci][1]; vv.pos[2] = c[ci][2];
        }
    }
 // 统一黄色 + 法线占位（line3d shader 只读 pos+color）
    for (size_t i = 0; i < vi; ++i) {
        v[i].normal[0] = 0.0f; v[i].normal[1] = 1.0f; v[i].normal[2] = 0.0f;
 v[i].color[0] = 1.0f; v[i].color[1] = 0.84f; v[i].color[2] = 0.1f; v[i].color[3] = 1.0f; // 黄
    }
    vkUnmapMemory(app.vk.device, app.vk.selVtxMem);

    float mvpm[16];
    MatMul4(mvp, model, mvpm);
    VkPipeline pipe = (app.vk.pipelineLine3dWide != VK_NULL_HANDLE) ? app.vk.pipelineLine3dWide : app.vk.pipelineLine3d;
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.selVtxBuffer, &off);
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvpm);
    vkCmdDraw(app.vk.commandBuffer, static_cast<uint32_t>(vi), 1, 0, 0);
}
void Grid::Draw(VkCommandBuffer cmd, VkBuffer quadBuffer, const float mvp[16],
                const float invVP[16], const Camera& cam, float dynamicGridRadius,
                const float fadeCenter[2], float smallFade) {
    if (pipeline == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &quadBuffer, &off);
    float push[40];
 std::memset(push, 0, sizeof(push)); // push constant 清零防 AMD 驱动未初始化字节敏感（0xC0000094）
    std::memcpy(push, mvp, 64);
    std::memcpy(push + 16, invVP, 64);
    const Vec3 cr{cam.position.x - cam.target.x,
                  cam.position.y - cam.target.y,
                  cam.position.z - cam.target.z};
    const float dist = std::sqrt(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z);
 const float distSafe = std::max(dist, 1e-3f); // 相机距离钳零防 AMD 驱动 1/0 整除崩溃
 push[32] = 0.0f; // lodBlend 不再使用
    push[33] = distSafe;
    push[34] = fadeCenter[0];
    push[35] = fadeCenter[1];
    push[36] = dynamicGridRadius;
    push[37] = smallFade;
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 160, push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}
void DrawCoordHud(App& app, const Layout& layout) {
    if (app.ui.navCoordAlpha <= 0.004f) return;
    const float ca = app.ui.navCoordAlpha;
    const VkRect2D& vp = layout.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return;
 const Vec3 p = app.camera.target; // 注视点（旋转/缩放不变，仅平移变）

 // 全屏视口（DrawIcon 依赖当前视口把像素坐标映射到屏幕）
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(app.vk.swapchainExtent.width),
                        static_cast<float>(app.vk.swapchainExtent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);

    const float margin = 16.0f;
    const float baseY = static_cast<float>(vp.offset.y + vp.extent.height) - margin;
    const float x0 = static_cast<float>(vp.offset.x) + margin + 4.0f;
    const float lineH = 18.0f;
    const char* axes[3] = {"X", "Y", "Z"};
    const float vals[3] = {p.x, p.y, p.z};

    for (int i = 0; i < 3; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %.3f", axes[i], vals[i]);
        wchar_t wbuf[64];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 64);
        if (app.ui.coordLabels[i].text != wbuf) {
            std::vector<uint8_t> rgba;
            int tw = 0, th = 0;
            if (RasterizeText(wbuf, 13, 2, L"Segoe UI", rgba, tw, th)) {
                UploadLabelRgba(app, app.ui.coordLabels[i], rgba, tw, th);
                app.ui.coordLabels[i].text = wbuf;
            }
        }
        if (app.ui.coordLabels[i].set != VK_NULL_HANDLE) {
            const float wTxt = static_cast<float>(app.ui.coordLabels[i].w);
            const float hTxt = static_cast<float>(app.ui.coordLabels[i].h);
            const float lx = x0;
 const float ly = baseY - 24.0f - (2 - i) * lineH; // 整体下移 20px（X 最上、Z 最下）
            const VkRect2D tRect{{static_cast<int32_t>(lx), static_cast<int32_t>(ly)},
                                 {static_cast<uint32_t>(wTxt), static_cast<uint32_t>(hTxt)}};
            VkClearColorValue col{{0.90f * ca, 0.90f * ca, 0.92f * ca, ca}};
            DrawIcon(app, tRect, col, app.ui.coordLabels[i].set);
        }
    }
}
void BeginFrameRendering(App& app, uint32_t imageIndex, bool fxaa, const VkClearValue& clearValue, const VkClearValue& depthClear, const VkRect2D& fullArea) {
    if (app.vk.useDynamicRendering) {
        const VkImage swapImage = app.vk.swapchainImages[imageIndex];
        ImageBarrier(app, swapImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        ImageBarrier(app, app.vk.depthImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT);
        if (fxaa) {
            ImageBarrier(app, app.vk.fxaaImage,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        }
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (fxaa) {
            colorAttachment.imageView = app.vk.fxaaView;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        } else if (app.aa.msaaEnabled) {
            colorAttachment.imageView = app.vk.msaaColorView;
            colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colorAttachment.resolveImageView = app.vk.swapchainImageViews[imageIndex];
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        } else {
            colorAttachment.imageView = app.vk.swapchainImageViews[imageIndex];
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.clearValue = clearValue;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = app.vk.depthView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue = depthClear;

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = fullArea;
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        g_pfnCmdBeginRendering(app.vk.commandBuffer, &renderInfo);
    } else {
        VkClearValue clearValues[2] = {clearValue, depthClear};
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = app.vk.renderPass;
        rpBegin.framebuffer = app.vk.framebuffers[imageIndex];
        rpBegin.renderArea = fullArea;
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clearValues;

        vkCmdBeginRenderPass(app.vk.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    }
}
void DrawGridFloor(App& app, const float* mvp, const float* invVP, float fadeRadius) {
 // 网格地板：实体模式在物体之后（保证"立方体遮挡网格"）；线框模式在物体之后、白线之前绘制
    {
        g_stage = "DrawFrame:网格";
        app.vk.grid.Draw(app.vk.commandBuffer, app.vk.vertexBuffer, mvp, invVP, app.camera, fadeRadius,
                      app.scene.fadeCenterXZ, app.scene.smallGridFade);
    }
}
void DrawViewportGizmoIndicator(App& app, const float* camView, const Layout& layout, const VkViewport& viewport, const VkRect2D& fullArea, VkDeviceSize vertexOffset) {
 // 3) 视口右上角坐标轴指示器（正方形视口方案——上一版，用户确认回退）——
    const VkRect2D vp = layout.viewport;
    if (vp.extent.width > 0 && vp.extent.height > 0) {
        g_stage = "DrawFrame:gizmo";
        const float gSize = kGizmoViewportSize;
        const float vpH = static_cast<float>(vp.extent.height);
        const float gcx = static_cast<float>(vp.offset.x + vp.extent.width) - kGizmoMargin;
        const float gcy = static_cast<float>(vp.offset.y) + kGizmoMargin;
        const VkRect2D gRect{{static_cast<int32_t>(gcx - gSize * 0.5f),
                              static_cast<int32_t>(gcy - gSize * 0.5f)},
                             {static_cast<uint32_t>(gSize), static_cast<uint32_t>(gSize)}};
        const float halfPx = kGizmoHalfPx * std::pow(vpH / 540.0f, 0.3f);
        const float halfSize = gSize / (2.0f * halfPx);
        float viewRot[16] = {};
        viewRot[0]  = camView[0];  viewRot[4]  = camView[4];  viewRot[8]  = camView[8];
        viewRot[1]  = camView[1];  viewRot[5]  = camView[5];  viewRot[9]  = camView[9];
        viewRot[2]  = camView[2];  viewRot[6]  = camView[6];  viewRot[10] = camView[10];
        viewRot[15] = 1.0f;
        float gOrtho[16], gizmoMvp[16];
        OrthoMatrix(halfSize, gOrtho);
        MatMul4(gOrtho, viewRot, gizmoMvp);
        VkViewport gViewport{gcx - gSize * 0.5f, gcy - gSize * 0.5f, gSize, gSize, 0.0f, 1.0f};

        vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &fullArea);
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);

        const float crossHalf = gSize * 0.5f - 10.0f;
        DrawLine(app, gRect, gcx - crossHalf, gcy, gcx + crossHalf, gcy,
                 kCrosshairColor, 1.0f);
        DrawLine(app, gRect, gcx, gcy - crossHalf, gcx, gcy + crossHalf,
                 kCrosshairColor, 1.0f);

        const VkClearColorValue* gizmoColors[3] = {&kAxisXColor, &kAxisYColor, &kAxisZColor};
        const Vec3 gizmoAxes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        const Vec3 camFwd{-camView[8], -camView[9], -camView[10]};
        for (int i = 0; i < 3; ++i) {
            float ex = 0.0f, ey = 0.0f;
            if (!ProjectToViewport(gizmoMvp, gViewport, gizmoAxes[i].x, gizmoAxes[i].y, gizmoAxes[i].z, ex, ey)) continue;
 // 投影长度：轴几乎垂直屏幕（退化 <1e-3）时跳过（避免方向噪声/乱指）
            const float adx = ex - gcx;
            const float ady = ey - gcy;
            const float alen = std::sqrt(adx * adx + ady * ady);
            if (alen < 1e-3f) continue;
            const float len = std::min(alen, halfPx);
            const float ex2 = gcx + adx / alen * len;
            const float ey2 = gcy + ady / alen * len;
            const float facing = camFwd.x * gizmoAxes[i].x + camFwd.y * gizmoAxes[i].y
                               + camFwd.z * gizmoAxes[i].z;
            const float bright = std::clamp(facing * 3.0f + 0.5f, 0.0f, 1.0f);
            VkClearColorValue col = *gizmoColors[i];
            const float lum = 0.35f + 0.65f * bright;
            col.float32[0] *= lum;
            col.float32[1] *= lum;
            col.float32[2] *= lum;
            DrawLine(app, gRect, gcx, gcy, ex2, ey2, col, 1.8f);
            VkClearColorValue letterCol = *gizmoColors[i];
            letterCol.float32[0] = 0.65f + 0.35f * letterCol.float32[0];
            letterCol.float32[1] = 0.65f + 0.35f * letterCol.float32[1];
            letterCol.float32[2] = 0.65f + 0.35f * letterCol.float32[2];
            const float m = kGizmoLabelSize + 2.0f;
            const float lx = std::clamp(ex2, static_cast<float>(gRect.offset.x) + m,
                                        static_cast<float>(gRect.offset.x + gRect.extent.width) - m);
            const float ly = std::clamp(ey2, static_cast<float>(gRect.offset.y) + m,
                                        static_cast<float>(gRect.offset.y + gRect.extent.height) - m);
            DrawLetter(app, gRect, "XYZ"[i], lx, ly, kGizmoLabelSize, letterCol);
        }
    }
}
void DrawSelectionMaskPass(App& app, const float* mvp, const VkRect2D& fullArea, const Layout& layout, const VkViewport& viewport, bool selValid) {
    const auto drawSelectionMask = [&]() {
        if (app.vk.outlinePipeline == VK_NULL_HANDLE || app.vk.outlineView == VK_NULL_HANDLE ||
            app.vk.vertexBuffer3D == VK_NULL_HANDLE || app.vk.indexBuffer3D == VK_NULL_HANDLE || !selValid) return;
        ImageBarrier(app, app.vk.outlineImage,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        ImageBarrier(app, app.vk.outlineDepthImage,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT);
        VkRenderingAttachmentInfo maskColor{};
        maskColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        maskColor.imageView = app.vk.outlineView;
        maskColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        maskColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        maskColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        maskColor.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkRenderingAttachmentInfo maskDepth{};
        maskDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        maskDepth.imageView = app.vk.outlineDepthView;
        maskDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        maskDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        maskDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        maskDepth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo maskInfo{};
        maskInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        maskInfo.renderArea = fullArea;
        maskInfo.layerCount = 1;
        maskInfo.colorAttachmentCount = 1;
        maskInfo.pColorAttachments = &maskColor;
        maskInfo.pDepthAttachment = &maskDepth;
        g_pfnCmdBeginRendering(app.vk.commandBuffer, &maskInfo);
        vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &layout.viewport);
        const SceneObject& selObj = app.scene.objects[static_cast<size_t>(app.scene.selectedObject)];
        const uint32_t n = static_cast<uint32_t>(selObj.solidIndices.size());
        if (n > 0) {
            const int cmode2 = app.scene.vertexColorMode < 0 ? 0 : (app.scene.vertexColorMode > 4 ? 4 : app.scene.vertexColorMode);
            // mask pass 渲染到 1 采样 outlineImage，必须用 1 采样管线（pipelineSolidMask），
            // 不能用随 MSAA 变 4 采样的实体管线，否则采样数不匹配导致 GPU 挂起/黑屏
            VkPipeline solidPipe2 = app.vk.pipelineSolidMask;
            vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, solidPipe2);
            VkDeviceSize off0 = 0;
            vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer3D, &off0);
            vkCmdBindIndexBuffer(app.vk.commandBuffer, app.vk.indexBuffer3D, off0, VK_INDEX_TYPE_UINT32);
            float model2[16] = {};
            BuildModelMatrix(selObj, model2);
            float mvpm2[16];
            MatMul4(mvp, model2, mvpm2);
            Push3D push3d2{};
            std::memcpy(push3d2.mvp, mvpm2, 64);
            push3d2.mode = 1.0f;
            push3d2.gridRadius = kDefaultFadeRadius;
            push3d2.modelRadius = kDefaultFadeRadius;
            push3d2.hasColor = (cmode2 == 0) ? 0.0f : (cmode2 == 4 ? 2.0f : 1.0f);
            push3d2.camXZ[0] = app.scene.fadeCenterXZ[0];
            push3d2.camXZ[1] = app.scene.fadeCenterXZ[1];
            vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutSolid,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push3D), &push3d2);
 // 与普通实体绘制一致——solidIndices 为局部索引，firstVertex=selObj.vertexOffset
            vkCmdDrawIndexed(app.vk.commandBuffer, n, 1, selObj.solidIndexOffset,
                             static_cast<int32_t>(selObj.vertexOffset), 0);
        }
        g_pfnCmdEndRendering(app.vk.commandBuffer);
        ImageBarrier(app, app.vk.outlineImage,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    };
    drawSelectionMask();
}
void EndFrameAndPresent(App& app, uint32_t imageIndex) {
    g_stage = "DrawFrame:结束pass";
    if (app.vk.useDynamicRendering) {
        g_pfnCmdEndRendering(app.vk.commandBuffer);
    } else {
        vkCmdEndRenderPass(app.vk.commandBuffer);
    }

    const VkImage swapImage = app.vk.swapchainImages[imageIndex];
    if (app.vk.useDynamicRendering) {
        ImageBarrier(app, swapImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
    }
    vkEndCommandBuffer(app.vk.commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &app.vk.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &app.vk.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &app.vk.renderFinished;
    g_stage = "DrawFrame:submit命令";
    const VkResult submitRes = vkQueueSubmit(app.vk.graphicsQueue, 1, &submitInfo, app.vk.inFlightFence);
    if (submitRes != VK_SUCCESS) {
        // 防御：submit 失败 → renderFinished 信号量永不被置位 → 紧接着的 vkQueuePresentKHR 会无限阻塞
        // （Vulkan present 无超时），主线程冻结、黑屏、只能任务管理器强杀。故 submit 失败直接跳过呈现。
        // 同时置 s_skipFenceWait，使下一帧跳过 vkWaitForFences（栅栏已未置位且不会自行置位），
        // 避免每帧等满 10s 的伪卡死；循环继续存活，日志持续记录 submit 错误便于定位。
        s_skipFenceWait = true;
        { static int s_dfz=0; if(s_dfz<4){++s_dfz; VkbLog(("[df] submit res="+std::to_string((int)submitRes)+" -> return").c_str());} }
        return;
    }

    { static int s_df4=0; if(s_df4<4){++s_df4; VkbLog("[df] post-submit ok");} }
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &app.vk.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &app.vk.swapchain;
    presentInfo.pImageIndices = &imageIndex;
    g_stage = "DrawFrame:present呈现";
    const VkResult presentResult = g_pfnQueuePresentKHR(app.vk.graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        app.resizePending = true;
    { static int s_df5=0; if(s_df5<4){++s_df5; VkbLog(("[df] post-present res="+std::to_string((int)presentResult)).c_str());} }
    }
}
void DrawFrame(App& app) {
    { static int s_de=0; if(s_de<4){++s_de; VkbLog("[df] enter");} }
    g_stage = "DrawFrame:交换链/acquire";
    if (app.resizePending && app.vk.swapchain != VK_NULL_HANDLE) {
        RECT cr{};
        GetClientRect(app.hwnd, &cr);
        const uint32_t cw = static_cast<uint32_t>(cr.right - cr.left);
        const uint32_t ch = static_cast<uint32_t>(cr.bottom - cr.top);
        app.resizePending = false;
        if (cw == 0 || ch == 0) {
            return;
        }
        if (cw != app.vk.swapchainExtent.width || ch != app.vk.swapchainExtent.height) {
            if (!RecreateSwapchain(app)) {
                return;
            }
        }
    }
    if (app.vk.swapchainExtent.width == 0 || app.vk.swapchainExtent.height == 0) {
        // 交换链尺寸异常（驱动在 init 期把 currentExtent 报成 0 等）→ 触发重建以获取有效尺寸，
        // 而非每帧静默 return 导致整窗永久黑屏；并记一次性日志便于复现诊断。
        app.resizePending = true;
        return;
    }

 // 有限超时(100ms)替代 2s/无限——设备丢失/命令未完成时不再长时间阻塞主线程（手感优化）。
 // 超时即跳过本帧（记录原因与等待毫秒），保持消息循环存活 → UI 不冻结；下一帧重试。
 // s_skipFenceWait：上一帧 vkQueueSubmit 失败后栅栏已处于未置位且不会自行置位，若仍硬等 10s 会每帧假死；
 // 故失败后下一帧直接跳过等待，避免"黑屏+只能任务管理器"的伪卡死（日志已记录 submit 错误）。
    g_stage = "DrawFrame:等待inFlight栅栏";
    VkResult fenceRes = VK_SUCCESS;
    if (s_skipFenceWait) {
        s_skipFenceWait = false;   // 本帧不再等待，直接继续录制/提交
    } else {
        fenceRes = vkWaitForFences(app.vk.device, 1, &app.vk.inFlightFence, VK_TRUE, 10000000ull);
        if (fenceRes != VK_SUCCESS) {
            { static int s_dfx=0; if(s_dfx<4){++s_dfx; VkbLog(("[df] fence-wait res="+std::to_string((int)fenceRes)+" -> return").c_str());} }
            return;
        }
    }

 // 帧首安全点：处理 DrawFrame 录制中途（缩放改距离条 / 平移改坐标标签）暂存的标签纹理上传，
 // 避免在命令缓冲录制中途阻塞 2 秒（#215 回归根因：旧 UploadLabelRgba 在此 vkWaitForFences(inFlightFence,2s)，
 // 当时该栅栏已复位、当前帧未提交→卡满 2 秒）。此处上一帧已结束、当前帧未录制，销毁/重建纹理安全。
    { static int s_df2=0; if(s_df2<4){++s_df2; VkbLog("[df] post-fence ok");} }
    FlushPendingLabelUploads(app);

    uint32_t imageIndex = 0;
 // 有限超时(100ms)替代 2s/无限——呈现引擎无法提供图像/交换链失效时不再长时间阻塞（手感优化）。
    g_stage = "DrawFrame:acquire图像";
    VkResult result = g_pfnAcquireNextImageKHR(app.vk.device, app.vk.swapchain, 10000000ull,
                                               app.vk.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain(app);
        return;
    }
    if (result == VK_TIMEOUT || result == VK_ERROR_SURFACE_LOST_KHR ||
        result == VK_ERROR_DEVICE_LOST) {
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        { static int s_dfy=0; if(s_dfy<4){++s_dfy; VkbLog(("[df] acquire res="+std::to_string((int)result)+" -> return").c_str());} }
        return;
    }
    { static int s_df3=0; if(s_df3<4){++s_df3; VkbLog(("[df] post-acquire ok idx="+std::to_string(imageIndex)).c_str());} }
    vkResetFences(app.vk.device, 1, &app.vk.inFlightFence);

    vkResetCommandBuffer(app.vk.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(app.vk.commandBuffer, &beginInfo);

    VkClearValue clearValue{};
    clearValue.color = kBackgroundColor;
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 1.0f;

    const VkRect2D fullArea = {{0, 0}, app.vk.swapchainExtent};
    const bool fxaa = (app.aa.aaMode == AAMode::FXAA && app.vk.useDynamicRendering);
    BeginFrameRendering(app, imageIndex, fxaa, clearValue, depthClear, fullArea);
    g_stage = "DrawFrame:beginPass完成";

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(app.vk.swapchainExtent.width),
                        static_cast<float>(app.vk.swapchainExtent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &fullArea);

    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);

    const Layout layout = ComputeLayout(app);

 // 3D 视口背景延伸：覆盖左右面板圆角裁切的缺口"显示黑边"
 // —— 面板 kCornerRadius=6 把右/左边切成圆角，露出背景色（0.12，比视口 0.14 更暗）→ 视觉黑色细条
 // 把 3D 视口背景的 rect 略向左/右/上/下扩展，填上这片角缺口
    if (layout.viewport.extent.width > 0 && layout.viewport.extent.height > 0) {
        VkRect2D bgRect = layout.viewport;
        bgRect.offset.x -= static_cast<int32_t>(kCornerRadius);
        bgRect.offset.y -= static_cast<int32_t>(kCornerRadius);
        bgRect.extent.width  += 2 * static_cast<uint32_t>(kCornerRadius);
        bgRect.extent.height += 2 * static_cast<uint32_t>(kCornerRadius);
        DrawPanel(app, {bgRect, kViewportColor, 0.0f});
    }


    const float vpAspect = layout.viewport.extent.height > 0
        ? static_cast<float>(layout.viewport.extent.width) / static_cast<float>(layout.viewport.extent.height)
        : 1.0f;
    float camView[16], camProj[16], mvp[16], invVP[16];
    app.camera.ViewMatrix(camView);
    app.camera.ProjectionMatrix(vpAspect, camProj);
    MatMul4(camProj, camView, mvp);
    Mat4Inverse(mvp, invVP);

 // 渐隐中心 = **摄像机位置在 XZ 的投影**，**每帧跟随摄像机**要求：
 // 旋转/平移/推拉/导入移动摄像机时，网格渐隐都要跟着动——
 // - 用 position 而非 target：target 是注视点，与脚下网格中心存在偏移，
 // Orbit/Zoom 后 target≠position → 渐隐圈错位；旧代码取 target.xz 正是
 // "渐隐有偏移 + 不跟摄像机位置" BUG 的根源
 // - 导入模型 Fit 移动相机后，下一帧即自动跟随新位置（无需单独维护状态）
    app.scene.fadeCenterXZ[0] = app.camera.position.x;
    app.scene.fadeCenterXZ[1] = app.camera.position.z;

    VkViewport vp3d{static_cast<float>(layout.viewport.offset.x),
                    static_cast<float>(layout.viewport.offset.y),
                    static_cast<float>(layout.viewport.extent.width),
                    static_cast<float>(layout.viewport.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &vp3d);
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &layout.viewport);

 // 统一渐隐半径**固定默认渲染距离 = 原来的 150 × 1.7 = 255**，
 // 导入任何模型不会导致其变化（删除 按最大模型增量维护的计算代码）。
    const float fadeRadius = kDefaultFadeRadius;

 // 网格地板统一在"物体之后、线框边线之前"绘制（见下方两处），保证：
 // ① 实体模式"立方体遮挡网格"；② 线框模式白线盖在网格之上（不透明）。
    if (app.vk.vertexBuffer3D != VK_NULL_HANDLE && app.vk.indexBuffer3D != VK_NULL_HANDLE) {
 // 颜色位深模式→ 选对应管线（0=无颜色 1=16bit 2=8bit 3=4bit 4=1bit）
        const int cmode = app.scene.vertexColorMode < 0 ? 0 : (app.scene.vertexColorMode > 4 ? 4 : app.scene.vertexColorMode);
 // 实体模式 = unified3d FILL 管线组；**线框模式 = line3d + 唯一边Blender 边框，
 // 无三角对角线，不再用 polygonMode=LINE 的三角边线**
        VkPipeline solidPipe = app.vk.pipelineSolidNoColor;
        if (cmode == 1) solidPipe = app.vk.pipelineSolid;
        else if (cmode == 2) solidPipe = app.vk.pipelineSolid8;
        else if (cmode == 3) solidPipe = app.vk.pipelineSolid4;
        else if (cmode == 4) solidPipe = app.vk.pipelineSolid1;
 const float hasColor = (cmode == 0) ? 0.0f : (cmode == 4 ? 2.0f : 1.0f); // 2=1bit 灰度广播
        for (const auto& obj : app.scene.objects) {
    {   // [诊断] 首帧物体渲染状态（只打一次）
        static bool s_rendered = false;
        if (!s_rendered) {
            s_rendered = true;
            const int cmode0 = app.scene.vertexColorMode;
            VkbLog(("[render]   缓冲 v3d=" + std::to_string(app.vk.vertexBuffer3D != VK_NULL_HANDLE) +
                   " i3d=" + std::to_string(app.vk.indexBuffer3D != VK_NULL_HANDLE) +
                   " wv3d=" + std::to_string(app.vk.wireVtxBuffer3D != VK_NULL_HANDLE) +
                   " v3dCap=" + std::to_string(app.vk.vertexBuffer3DCapacity) +
                   " i3dCap=" + std::to_string(app.vk.indexBuffer3DCapacity) +
                   " wv3dCap=" + std::to_string(app.vk.wireVtxBuffer3DCapacity)).c_str());
            VkbLog(("[render]   缓冲 v3d=" + std::to_string(app.vk.vertexBuffer3D != VK_NULL_HANDLE) +
                   " i3d=" + std::to_string(app.vk.indexBuffer3D != VK_NULL_HANDLE) +
                   " wv3d=" + std::to_string(app.vk.wireVtxBuffer3D != VK_NULL_HANDLE) +
                   " v3dCap=" + std::to_string(app.vk.vertexBuffer3DCapacity) +
                   " i3dCap=" + std::to_string(app.vk.indexBuffer3DCapacity) +
                   " wv3dCap=" + std::to_string(app.vk.wireVtxBuffer3DCapacity)).c_str());
            VkbLog(("[render] 首帧 renderMode=" + std::to_string(app.ui.renderMode) +
                   " 物体数=" + std::to_string(app.scene.objects.size()) +
                   " 顶点色模式=" + std::to_string(cmode0) +
                   " noColorPipe=" + std::to_string(app.vk.pipelineSolidNoColor != VK_NULL_HANDLE) +
                   " solidPipe=" + std::to_string(app.vk.pipelineSolid != VK_NULL_HANDLE) +
                   " line3dPipe=" + std::to_string(app.vk.pipelineLine3d != VK_NULL_HANDLE)).c_str());
            for (const auto& oo : app.scene.objects) {
                VkbLog(("[render]   物体=" + std::string(oo.name.begin(), oo.name.end()) +
                       " sv=" + std::to_string(oo.solidVerts.size()) +
                       " si=" + std::to_string(oo.solidIndices.size()) +
                       " wv=" + std::to_string(oo.wireVerts.size()) +
                       " wi=" + std::to_string(oo.wireIndices.size()) +
                       " vo=" + std::to_string(oo.vertexOffset) +
                       " sio=" + std::to_string(oo.solidIndexOffset)).c_str());
            }
        }
    }
 // 线框模式：实体几何在此跳过，统一在"网格之后"的线框 pass 绘制（保证白线盖在网格上、不透明）
            if (app.ui.renderMode == 1) continue;
            const uint32_t n = static_cast<uint32_t>(obj.solidIndices.size());
            if (n == 0) continue;
            g_stage = "DrawFrame:物体层";
            vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, solidPipe);
            VkDeviceSize vertexOffsetWire = 0;
            vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer3D, &vertexOffsetWire);
            VkDeviceSize indexOffsetWire = 0;
            vkCmdBindIndexBuffer(app.vk.commandBuffer, app.vk.indexBuffer3D, indexOffsetWire,
                                 VK_INDEX_TYPE_UINT32);
            float model[16] = {};
 BuildModelMatrix(obj, model); // T·R·S（旋转/缩放）
            float mvpm[16];
            MatMul4(mvp, model, mvpm);
            Push3D push3d{};
            std::memcpy(push3d.mvp, mvpm, 64);
    {   // [数据] 顶点与 push 首帧检查
        static bool s_checked = false;
        if (!s_checked) {
            s_checked = true;
            VkbLog(("[数据] mvp0=" + std::to_string(mvp[0]) + " mvp5=" + std::to_string(mvp[5]) +
                   " mvp10=" + std::to_string(mvp[10]) + " mvp15=" + std::to_string(mvp[15])).c_str());
            VkbLog(("[数据] model0=" + std::to_string(model[0]) + " model5=" + std::to_string(model[5]) +
                   " model10=" + std::to_string(model[10]) + " model15=" + std::to_string(model[15])).c_str());
            VkbLog(("[数据] push3d.mode=" + std::to_string(push3d.mode) +
                   " gridR=" + std::to_string(push3d.gridRadius) +
                   " modelR=" + std::to_string(push3d.modelRadius) +
                   " hasColor=" + std::to_string(push3d.hasColor) +
                   " fadeDis=" + std::to_string(push3d.fadeDisable) +
                   " camXZ=" + std::to_string(push3d.camXZ[0]) + "," + std::to_string(push3d.camXZ[1]) +
                   " objXZ=" + std::to_string(push3d.objXZ[0]) + "," + std::to_string(push3d.objXZ[1])).c_str());
            if (!obj.solidVerts.empty()) {
                const VertexSolid& v0 = obj.solidVerts[0];
                const VertexSolid& v1 = obj.solidVerts[obj.solidVerts.size()-1];
                VkbLog(("[数据] v0.pos=" + std::to_string(v0.pos[0]) + "," + std::to_string(v0.pos[1]) + "," + std::to_string(v0.pos[2]) +
                       " vN.pos=" + std::to_string(v1.pos[0]) + "," + std::to_string(v1.pos[1]) + "," + std::to_string(v1.pos[2])).c_str());
            }
            if (!obj.solidIndices.empty()) {
                VkbLog(("[数据] idx0=" + std::to_string(obj.solidIndices[0]) +
                       " idx1=" + std::to_string(obj.solidIndices[1]) +
                       " idx2=" + std::to_string(obj.solidIndices[2]) +
                       " idxN=" + std::to_string(obj.solidIndices[obj.solidIndices.size()-1])).c_str());
            }
        }
    }
    {   // [数据] 顶点与 push 首帧检查
        static bool s_checked = false;
        if (!s_checked) {
            s_checked = true;
            VkbLog(("[数据] mvp0=" + std::to_string(mvp[0]) + " mvp5=" + std::to_string(mvp[5]) +
                   " mvp10=" + std::to_string(mvp[10]) + " mvp15=" + std::to_string(mvp[15])).c_str());
            VkbLog(("[数据] model0=" + std::to_string(model[0]) + " model5=" + std::to_string(model[5]) +
                   " model10=" + std::to_string(model[10]) + " model15=" + std::to_string(model[15])).c_str());
            VkbLog(("[数据] push3d.mode=" + std::to_string(push3d.mode) +
                   " gridR=" + std::to_string(push3d.gridRadius) +
                   " modelR=" + std::to_string(push3d.modelRadius) +
                   " hasColor=" + std::to_string(push3d.hasColor) +
                   " fadeDis=" + std::to_string(push3d.fadeDisable) +
                   " camXZ=" + std::to_string(push3d.camXZ[0]) + "," + std::to_string(push3d.camXZ[1]) +
                   " objXZ=" + std::to_string(push3d.objXZ[0]) + "," + std::to_string(push3d.objXZ[1])).c_str());
            if (!obj.solidVerts.empty()) {
                const VertexSolid& v0 = obj.solidVerts[0];
                const VertexSolid& v1 = obj.solidVerts[obj.solidVerts.size()-1];
                VkbLog(("[数据] v0.pos=" + std::to_string(v0.pos[0]) + "," + std::to_string(v0.pos[1]) + "," + std::to_string(v0.pos[2]) +
                       " vN.pos=" + std::to_string(v1.pos[0]) + "," + std::to_string(v1.pos[1]) + "," + std::to_string(v1.pos[2])).c_str());
            }
            if (!obj.solidIndices.empty()) {
                VkbLog(("[数据] idx0=" + std::to_string(obj.solidIndices[0]) +
                       " idx1=" + std::to_string(obj.solidIndices[1]) +
                       " idx2=" + std::to_string(obj.solidIndices[2]) +
                       " idxN=" + std::to_string(obj.solidIndices[obj.solidIndices.size()-1])).c_str());
            }
        }
    }
 push3d.mode = 1.0f; // 实体光照（线框模式走 line3d 分支，不走这里）
            push3d.gridRadius = fadeRadius;
 push3d.modelRadius = kDefaultFadeRadius; // 固定渲染距离，不随模型变化
 push3d.hasColor = hasColor; // 无颜色模式 shader 用常量色（修复"立方体消失"）
 // 渐隐中心 = fadeCenterXZ（摄像机位置投影，每帧跟随）
            push3d.camXZ[0] = app.scene.fadeCenterXZ[0];
            push3d.camXZ[1] = app.scene.fadeCenterXZ[1];
            push3d.objXZ[0] = obj.tx;
            push3d.objXZ[1] = obj.tz;
 // MC 模型（名字含"我的世界"）禁用远处渐隐 → 完全不透明
            push3d.fadeDisable =
                (obj.name.find(L"我的世界") != std::wstring::npos) ? 1.0f : 0.0f;
            vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutSolid,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(Push3D), &push3d);
 // solidIndices 为局部索引(0..sv-1)，vertexBuffer3D 现只含 solid 顶点
 // 已移除 wire 冗余副本，firstVertex 必须=obj.vertexOffset 起点。
 // 旧代码 +obj.wireVerts.size 是 前的布局残留——会越界读 GPU 缓冲→设备丢失→卡死。
    {   // [实体] 绘制确认（节流）
        static uint64_t s_drawNo = 0;
        ++s_drawNo;
        if (s_drawNo <= 3 || (s_drawNo % 300) == 0) {
            VkbLog(("[实体] draw#" + std::to_string(s_drawNo) +
                   " n=" + std::to_string(n) +
                   " sio=" + std::to_string(obj.solidIndexOffset) +
                   " vo=" + std::to_string(obj.vertexOffset) +
                   " pipe=" + std::to_string(reinterpret_cast<uintptr_t>(solidPipe)) +
                   " v3d=" + std::to_string(app.vk.vertexBuffer3D != VK_NULL_HANDLE) +
                   " i3d=" + std::to_string(app.vk.indexBuffer3D != VK_NULL_HANDLE)).c_str());
        }
    }
            vkCmdDrawIndexed(app.vk.commandBuffer, n, 1, obj.solidIndexOffset,
                             static_cast<int32_t>(obj.vertexOffset), 0);
        }
    }

 // ===== 选中物体高亮/线框预览，Blender 风格=====
    if (app.vk.pipelineLine3d != VK_NULL_HANDLE && app.scene.selectedObject >= 0 &&
        app.scene.selectedObject < static_cast<int>(app.scene.objects.size())) {
 SceneObject& sel = app.scene.objects[app.scene.selectedObject]; // 非 const（DrawSelectionOutline 延迟构建特征边）
        if (app.ui.renderMode != 1 && app.scene.wireframeSel && !sel.wireIndices.empty()) {
 // Tab：线框预览——画 wireIndices（模型矩阵含平移）
            float model[16] = {};
 BuildModelMatrix(sel, model); // T·R·S（旋转/缩放）
            float mvpm[16];
            MatMul4(mvp, model, mvpm);
            vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineLine3d);
            VkDeviceSize off = 0;
 vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.wireVtxBuffer3D, &off); // 专用 40B 缓冲
            vkCmdBindIndexBuffer(app.vk.commandBuffer, app.vk.indexBuffer3D, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, 64, mvpm);
            vkCmdDrawIndexed(app.vk.commandBuffer, static_cast<uint32_t>(sel.wireIndices.size()), 1,
                             sel.wireIndexOffset, sel.wireVtxOffset, 0);
        } else {
 // 选中描边改为屏幕空间后处理（mask + Sobel，见 DrawFrame 的 mask/outline pass）；
 // 传统 render pass 路径无后处理，保留旧 3D silhouette 黄线兜底
            if (!app.vk.useDynamicRendering) {
                DrawSelectionOutline(app, sel, app.scene.selectedObject, mvp);
            }
    }
    }

 // 多选高亮——其他框选物体画黄色 AABB 线框（世界坐标，直接乘 mvp）
    if (!app.ui.multiSel.empty() && app.vk.pipelineLine3d != VK_NULL_HANDLE) {
        constexpr int kBoxVerts = 24;
        if (EnsureHostVtxBuffer(app, app.vk.gizmoVtxBuffer, app.vk.gizmoVtxMem, kBoxVerts * 40)) {
            const int edges[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{3,7},{2,6}};
            for (int idx : app.ui.multiSel) {
                if (idx == app.scene.selectedObject || idx < 0 || idx >= static_cast<int>(app.scene.objects.size())) continue;
                const SceneObject& mo = app.scene.objects[idx];
                void* mapped = nullptr;
                if (vkMapMemory(app.vk.device, app.vk.gizmoVtxMem, 0, kBoxVerts * 40, 0, &mapped) != VK_SUCCESS) break;
                VertexSolid* v = static_cast<VertexSolid*>(mapped);
                const float minx = mo.boundsMin[0] + mo.tx, miny = mo.boundsMin[1] + mo.ty, minz = mo.boundsMin[2] + mo.tz;
                const float maxx = mo.boundsMax[0] + mo.tx, maxy = mo.boundsMax[1] + mo.ty, maxz = mo.boundsMax[2] + mo.tz;
                const float c[8][3] = {
                    {minx, miny, minz}, {maxx, miny, minz}, {minx, maxy, minz}, {maxx, maxy, minz},
                    {minx, miny, maxz}, {maxx, miny, maxz}, {minx, maxy, maxz}, {maxx, maxy, maxz},
                };
                int vi = 0;
                for (int e = 0; e < 12; ++e)
                    for (int k = 0; k < 2; ++k) {
                        VertexSolid& vv = v[vi++];
                        const int ci = edges[e][k];
                        vv.pos[0] = c[ci][0]; vv.pos[1] = c[ci][1]; vv.pos[2] = c[ci][2];
                        vv.normal[0] = 0; vv.normal[1] = 1; vv.normal[2] = 0;
 vv.color[0] = 1.0f; vv.color[1] = 0.84f; vv.color[2] = 0.1f; vv.color[3] = 1.0f; // 黄
                    }
                vkUnmapMemory(app.vk.device, app.vk.gizmoVtxMem);
                vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineLine3d);
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.gizmoVtxBuffer, &off);
                vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
                vkCmdDraw(app.vk.commandBuffer, kBoxVerts, 1, 0, 0);
            }
        }
    }

 // ===== 法线渲染，默认关闭，保留接口=====
 // 用面法线（solidVerts.normal）画青色法线线：顶点 → 顶点 + normal×len
    if (g_showNormals && app.vk.pipelineAxis != VK_NULL_HANDLE) {
        g_stage = "DrawFrame:法线";
        const float normalLen = 0.3f;
        const float vw = static_cast<float>(layout.viewport.extent.width);
        const float vh = static_cast<float>(layout.viewport.extent.height);
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineAxis);
        for (const auto& obj : app.scene.objects) {
            for (const auto& v : obj.solidVerts) {
                Axis3DPush p{};
                std::memcpy(p.mvp, mvp, 64);
                p.pointA[0] = v.pos[0] + obj.tx; p.pointA[1] = v.pos[1] + obj.ty; p.pointA[2] = v.pos[2] + obj.tz; p.pointA[3] = 1.0f;
                p.pointB[0] = p.pointA[0] + v.normal[0] * normalLen;
                p.pointB[1] = p.pointA[1] + v.normal[1] * normalLen;
                p.pointB[2] = p.pointA[2] + v.normal[2] * normalLen;
                p.pointB[3] = 1.0f;
 p.params[0] = 1.0f; // 线宽 px
                p.params[1] = vw;
                p.params[2] = vh;
 p.color[0] = 0.20f; p.color[1] = 0.85f; p.color[2] = 1.00f; p.color[3] = 1.0f; // 青蓝
                vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutAxis,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(Axis3DPush), &p);
                vkCmdDraw(app.vk.commandBuffer, 4, 1, 0, 0);
            }
        }
    }

    DrawGridFloor(app, mvp, invVP, fadeRadius);

 // ===== 线框模式边线/270/272 修正：网格之后绘制 → 白线盖在网格之上、不透明 =====
    if (app.ui.renderMode == 1 && app.vk.vertexBuffer3D != VK_NULL_HANDLE &&
        app.vk.indexBuffer3D != VK_NULL_HANDLE) {
        g_stage = "DrawFrame:线框边线";
 VkPipeline wirePipe = app.vk.pipelineLine3dWide; // 2px 加粗，缺失回退 1px
        if (wirePipe == VK_NULL_HANDLE) wirePipe = app.vk.pipelineLine3d;
        if (wirePipe != VK_NULL_HANDLE) {
            for (const auto& obj : app.scene.objects) {
 // 选中物体跳过硬白线框——由黄色外轮廓高亮（避免白线覆盖）
                if (app.scene.selectedObject >= 0 &&
                    &obj == &app.scene.objects[static_cast<size_t>(app.scene.selectedObject)]) continue;
                const uint32_t wn = static_cast<uint32_t>(obj.wireIndices.size());
                if (wn == 0) continue;
                float model[16] = {};
 BuildModelMatrix(obj, model); // T·R·S（旋转/缩放）
                float mvpm[16];
                MatMul4(mvp, model, mvpm);
                vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wirePipe);
                VkDeviceSize woff = 0;
                vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.wireVtxBuffer3D, &woff);
                vkCmdBindIndexBuffer(app.vk.commandBuffer, app.vk.indexBuffer3D, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, 64, mvpm);
                vkCmdDrawIndexed(app.vk.commandBuffer, wn, 1, obj.wireIndexOffset, obj.wireVtxOffset, 0);
            }
        }
    }

    if (app.ui.axisVisible && app.vk.pipelineAxis != VK_NULL_HANDLE && app.vk.pipelineAxisOccluded != VK_NULL_HANDLE) {
        g_stage = "DrawFrame:坐标轴";
        const float axisHalfWidthPx = 2.0f;
        const float vw = static_cast<float>(layout.viewport.extent.width);
        const float vh = static_cast<float>(layout.viewport.extent.height);
        const float axisLen = 1.5f;
        const float axisCol[3][4] = {
            {0.95f, 0.30f, 0.30f, 1.0f},
            {0.30f, 0.95f, 0.40f, 1.0f},
            {0.35f, 0.55f, 1.00f, 1.0f},
        };
        const float kOccludedDim = 0.35f;
        auto drawAxis = [&](VkPipeline pipeline, float dim) {
            vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            for (int i = 0; i < 3; ++i) {
                Axis3DPush pushAxis{};
                std::memcpy(pushAxis.mvp, mvp, 64);
                pushAxis.pointA[i] = -axisLen;
                pushAxis.pointB[i] = axisLen;
                pushAxis.params[0] = axisHalfWidthPx;
                pushAxis.params[1] = vw;
                pushAxis.params[2] = vh;
                for (int c = 0; c < 3; ++c) pushAxis.color[c] = axisCol[i][c] * dim;
                pushAxis.color[3] = 1.0f;
                vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutAxis,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(Axis3DPush), &pushAxis);
                vkCmdDraw(app.vk.commandBuffer, 4, 1, 0, 0);
            }
        };
        drawAxis(app.vk.pipelineAxisOccluded, kOccludedDim);
        drawAxis(app.vk.pipelineAxis, 1.0f);
    }

 // ===== 变换 gizmo移动三向标 / 旋转三色环 / 缩放轴+方块，按 gizmoMode 切换=====
    if (app.ui.gizmoMode == 1) DrawRotateGizmo(app, mvp, layout.viewport);
    else if (app.ui.gizmoMode == 2) DrawScaleGizmo(app, mvp, layout.viewport);
    else DrawMoveGizmo(app, mvp, layout.viewport);

    DrawViewportGizmoIndicator(app, camView, layout, viewport, fullArea, vertexOffset);

 // ==================== 选中物体屏幕投影描边（mask + Sobel 后处理）====================
    const bool selValid = (app.scene.selectedObject >= 0 &&
                           app.scene.selectedObject < static_cast<int>(app.scene.objects.size()));
 // 选中物体实体 → maskRT（白色区域 = Sobel 检测输入）；mask pass 为独立 rendering
 // 3D pass 统一结束：fxaa 模式在下方块内 End；非 fxaa 在此补 End（2D 原在同一 pass，需拆出）
    if (app.vk.useDynamicRendering && !fxaa) {
        g_pfnCmdEndRendering(app.vk.commandBuffer);
    DrawSelectionMaskPass(app, mvp, fullArea, layout, viewport, selValid);
    }

 // ==== FXAA 模式：3D 已渲染到中间纹理 → 结束 3D pass → FXAA 后处理 → swapchain ====
    if (fxaa) {
        g_pfnCmdEndRendering(app.vk.commandBuffer);
        ImageBarrier(app, app.vk.fxaaImage,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    DrawSelectionMaskPass(app, mvp, fullArea, layout, viewport, selValid);
        VkRenderingAttachmentInfo colorAttachment2{};
        colorAttachment2.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment2.imageView = app.vk.swapchainImageViews[imageIndex];
        colorAttachment2.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment2.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment2.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment2.clearValue = clearValue;
        VkRenderingInfo renderInfo2{};
        renderInfo2.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo2.renderArea = fullArea;
        renderInfo2.layerCount = 1;
        renderInfo2.colorAttachmentCount = 1;
        renderInfo2.pColorAttachments = &colorAttachment2;
        g_pfnCmdBeginRendering(app.vk.commandBuffer, &renderInfo2);

        vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &fullArea);
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.fxaaPipeline);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);
        vkCmdBindDescriptorSets(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                app.vk.fxaaPipelineLayout, 0, 1, &app.vk.fxaaDescriptorSet, 0, nullptr);
        const float invScreen[2] = {1.0f / static_cast<float>(app.vk.swapchainExtent.width),
                                    1.0f / static_cast<float>(app.vk.swapchainExtent.height)};
        vkCmdPushConstants(app.vk.commandBuffer, app.vk.fxaaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(invScreen), invScreen);
        vkCmdDraw(app.vk.commandBuffer, 6, 1, 0, 0);
    }

 // 非 fxaa 模式——3D pass 已提前结束，为 2D 面板开启合成 rendering（LOAD swapchain）
    if (app.vk.useDynamicRendering && !fxaa) {
        VkRenderingAttachmentInfo colorUI{};
        colorUI.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorUI.imageView = app.vk.swapchainImageViews[imageIndex];
        colorUI.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorUI.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorUI.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo uiInfo{};
        uiInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        uiInfo.renderArea = fullArea;
        uiInfo.layerCount = 1;
        uiInfo.colorAttachmentCount = 1;
        uiInfo.pColorAttachments = &colorUI;
        g_pfnCmdBeginRendering(app.vk.commandBuffer, &uiInfo);
    }

 // 选中描边——Sobel(mask) 黄线叠加（fxaa：renderInfo2 内；非 fxaa：UI rendering 内；blend 保留底下画面）
    if (app.vk.useDynamicRendering && app.vk.outlinePipeline != VK_NULL_HANDLE &&
        app.vk.outlineDescriptorSet != VK_NULL_HANDLE && selValid) {
        vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &fullArea);
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.outlinePipeline);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);
        vkCmdBindDescriptorSets(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                app.vk.outlinePipelineLayout, 0, 1, &app.vk.outlineDescriptorSet, 0, nullptr);
        const float invScreen[2] = {1.0f / static_cast<float>(app.vk.swapchainExtent.width),
                                    1.0f / static_cast<float>(app.vk.swapchainExtent.height)};
        vkCmdPushConstants(app.vk.commandBuffer, app.vk.outlinePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(invScreen), invScreen);
        vkCmdDraw(app.vk.commandBuffer, 6, 1, 0, 0);
    }

    g_stage = "DrawFrame:缩放距离条+坐标";
    UpdateNavHud(app);
    DrawCoordHud(app, layout);

    g_stage = "DrawFrame:逻辑栏/2D";
    DrawLogicBar(app, layout);

    EndFrameAndPresent(app, imageIndex);
}
