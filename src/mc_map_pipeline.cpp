// ============================================================================
//   地图窗口专属地图渲染管线实现（见 mc_map_pipeline.h）
//   Round233：2D 地图改用 MCA Selector 自带颜色对照表渲染（不再依赖错位的 ss1 纹理图集）
//   我们的方式 = C++ 高性能像素操作 + GDI 双缓冲 + McMapPipeline 架构
//   MCA 数据   = colors 颜色表(1099 项) + tint 规则 + 取顶非透明 + 高度表 + 坡度光照
//   渲染：取最顶非透明方块 → McBlockColor() 查色（含 tint）→ 锐利坡度光照 → BGRA
// ============================================================================
#include "mc_map_pipeline.h"
#include "mc_blocks.h"     // McBlockColor / McBlockIsTransparent

#include <algorithm>       // std::max / std::min
#include <cstdint>

namespace {

// 取该方块列的【最顶非透明方块】（MCA：跳过空气与 transparent 集合）；
// 整列无方块返回 -1，同时输出其世界 Y 到 outTopY。
int TopSolidBlockAt(const McBlockGrid& grid, int bx, int bz, int* outTopY) {
    for (int y = grid.sy - 1; y >= 0; --y) {
        const int v = grid.at(bx, y, bz);
        if (v < 0) continue;                        // 空气
        if (McBlockIsTransparent(v)) continue;      // barrier/light/structure_void 等跳过
        if (outTopY) *outTopY = y;
        return v;
    }
    if (outTopY) *outTopY = -1;
    return -1;
}

inline float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace


bool McMapPipeline::Build(const McBlockGrid& grid, const McAtlas& atlas,
                          int outW, int outH) {
    if (outW <= 0 || outH <= 0) {
        width = height = 0;
        bgra.clear();
        return false;
    }
    (void)atlas;   // MCA 颜色表渲染不再采样图集（保留参数以维持架构接口）
    bgra.assign((size_t)outW * outH * 4, 0);
    width = outW;
    height = outH;
    const int sx = grid.sx, sz = grid.sz;
    if (sx <= 0 || sz <= 0) return false;

    // ---- 阶段 1：方块粒度高度表（每列最顶非透明方块世界 Y，空列 = -1）----
    std::vector<int16_t> heights((size_t)sx * sz, -1);
    for (int bz = 0; bz < sz; ++bz)
        for (int bx = 0; bx < sx; ++bx) {
            int topY = -1;
            TopSolidBlockAt(grid, bx, bz, &topY);
            heights[(size_t)bz * sx + bx] = (int16_t)topY;
        }

    // ---- 阶段 2：坡度光照系数表（MCA shade 思想，【锐利】模式）----
    // 高度相对东/南相邻列升高 → 受光变亮；降低 → 变暗。
    // 系数 + 范围都加大：clamp(1+(dx+dz)*0.10, 0.40, 1.60)。
    std::vector<float> lights((size_t)sx * sz, 1.0f);
    for (int bz = 0; bz < sz; ++bz)
        for (int bx = 0; bx < sx; ++bx) {
            const int idx = (size_t)bz * sx + bx;
            const int h = heights[idx];
            if (h < 0) { lights[idx] = 1.0f; continue; }
            int hR = (bx + 1 < sx) ? heights[(size_t)bz * sx + bx + 1] : h;
            int hD = (bz + 1 < sz) ? heights[(size_t)(bz + 1) * sx + bx] : h;
            if (hR < 0) hR = h;
            if (hD < 0) hD = h;
            const float dx = (float)(hR - h), dz = (float)(hD - h);
            lights[idx] = ClampF(1.0f + (dx + dz) * 0.10f, 0.40f, 1.60f);
        }

    // ---- 阶段 3：MCA 颜色表查色渲染 + 锐利坡度光照 ----
    for (int py = 0; py < outH; ++py) {
        int bz = (int)((int64_t)py * sz / outH);
        if (bz < 0) bz = 0; else if (bz >= sz) bz = sz - 1;

        for (int px = 0; px < outW; ++px) {
            int bx = (int)((int64_t)px * sx / outW);
            if (bx < 0) bx = 0; else if (bx >= sx) bx = sx - 1;

            // 光照系数：【锐利】模式——直接用所属列的 L（每方块列一种亮度）
            const int i00 = (size_t)bz * sx + bx;
            const float light = lights[i00];

            // MCA 颜色表查色（含 tint；TNT 等方块直接得正确颜色，不依赖 ss1 纹理图集）
            uint8_t r = 16, g = 16, b = 16;
            int topY = -1;
            const int block = TopSolidBlockAt(grid, bx, bz, &topY);
            if (block >= 0) {
                const uint32_t rgb = McBlockColor(block);
                r = (uint8_t)(rgb >> 16);
                g = (uint8_t)(rgb >> 8);
                b = (uint8_t)(rgb);
                // 锐利坡度光照（每方块列一种亮度）
                r = (uint8_t)std::min(255, (int)(r * light + 0.5f));
                g = (uint8_t)std::min(255, (int)(g * light + 0.5f));
                b = (uint8_t)std::min(255, (int)(b * light + 0.5f));
            }
            uint8_t* d = bgra.data() + ((size_t)py * outW + px) * 4;
            d[0] = b; d[1] = g; d[2] = r; d[3] = 255;
        }
    }
    return true;
}

void McMapPipeline::SetImage(int w, int h, std::vector<uint8_t>&& pixels) {
    width = w;
    height = h;
    bgra = std::move(pixels);
}

void McMapPipeline::Draw(HDC dc, int dstX, int dstY) const {
    if (!valid()) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth  = width;
    bi.bmiHeader.biHeight = -height;   // 自顶向下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, dstX, dstY, width, height,
                      0, 0, 0, height, bgra.data(), &bi, DIB_RGB_COLORS);
}
