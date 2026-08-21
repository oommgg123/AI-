// ============================================================================
//   地图窗口专属地图渲染管线（Round221）
//   - 职责：把方块网格（俯视）渲染成 480×480 地图位图，并支持动态更新：
//        Build(grid, atlas, w, h) —— 数据变化时再次调用即可重建
//        SetImage(w, h, pixels)   —— 外部直接注入位图（如未来 db 读取）
//   - 渲染策略（色彩丰富）：每个方块列取最顶非空气方块，把它的【顶部材质贴图】
//     平铺到该方块对应的地图区域（如草方块显示草方块顶部，水面显示水纹理），
//     而非单像素采样 —— 方块表面纹理细节全部保留。
//   - 绘制：Draw(dc, x, y) 输出到 GDI 双缓冲 DC（BGRA 32bpp，DIB 直接可用）。
//     窗口层负责画边框（1px 白色直角边，走按钮渲染管线）。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>
#include <windows.h>

#include "mc_blocks.h"   // McBlockGrid / McAtlas / McRectFor

struct McMapPipeline {
    int width = 0, height = 0;       // 地图位图尺寸
    std::vector<uint8_t> bgra;       // BGRA 内存序（GDI 32bpp DIB 直接使用）

    bool valid() const {
        return width > 0 && height > 0 &&
               bgra.size() == (size_t)width * height * 4;
    }

    // 从方块网格 + 图集重建地图位图（动态更新入口：世界数据变化时再次调用）。
    // 每个方块列取最顶方块 → 平铺其顶面贴图。失败时清空位图并返回 false。
    bool Build(const McBlockGrid& grid, const McAtlas& atlas, int outW, int outH);

    // 外部注入现成位图（动态更新入口：未来 db 读取 / 网络数据等直接替换）
    void SetImage(int w, int h, std::vector<uint8_t>&& pixels);

    // 绘制到 GDI DC（不画边框；边框由调用方用按钮渲染管线画）
    void Draw(HDC dc, int dstX, int dstY) const;
};
