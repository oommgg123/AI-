#pragma once
// Round277：左上角 3 球按钮图标（嵌入 PNG，运行时 WIC 内存解码）
// RCDATA 资源 ID；运行时 FindResourceW + LockResource 取字节，再走 WIC 解码到 RGBA
#define ID_BTN_ICON_1 100
#define ID_BTN_ICON_2 101
#define ID_BTN_ICON_3 102
// Round297：顶栏变换按钮图标（4=移动 5=旋转 6=缩放）
#define ID_BTN_ICON_4 103
#define ID_BTN_ICON_5 104
#define ID_BTN_ICON_6 105
