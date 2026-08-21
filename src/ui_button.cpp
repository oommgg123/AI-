// ============================================================================
//   共享 GDI 按钮渲染管线实现（见 ui_button.h）
//   与主窗口 Vulkan 按钮共用同一套 UiButton / ButtonStateMachine / ButtonTheme，
//   保证每个按钮都有一致的悬停/按下/释放动画与完整交互流程。
// ============================================================================
#include "ui_button.h"

namespace {
// 非 hover 时的默认描边色（与主窗口 main.cpp 的 kBorderColor 一致）
const float kDefBorder[4] = {0.35f, 0.35f, 0.38f, 1.0f};

inline int Clamp255(float v) {
    int i = static_cast<int>(v * 255.0f + 0.5f);
    if (i < 0) i = 0;
    if (i > 255) i = 255;
    return i;
}
}  // namespace

void UpdateButtonColor(UiButton& b, const ButtonTheme& theme) {
    const ButtonState s = b.machine.state;
    const float* target = (s == ButtonState::Pressed)  ? theme.pressed  :
                          (s == ButtonState::Released) ? theme.released : theme.normal;
    for (int i = 0; i < 4; ++i) b.color[i] += (target[i] - b.color[i]) * theme.animSpeed;
    const float* borderTarget = (s == ButtonState::Hover) ? theme.hoverBorder : kDefBorder;
    for (int i = 0; i < 4; ++i) b.border[i] += (borderTarget[i] - b.border[i]) * theme.animSpeed;
}

bool PointInButton(const UiButton& b, float x, float y) {
    return x >= static_cast<float>(b.rect.offset.x) &&
           x <  static_cast<float>(b.rect.offset.x + static_cast<int32_t>(b.rect.extent.width)) &&
           y >= static_cast<float>(b.rect.offset.y) &&
           y <  static_cast<float>(b.rect.offset.y + static_cast<int32_t>(b.rect.extent.height));
}

void DrawGdiButton(HDC dc, const UiButton& b, const ButtonTheme& /*theme*/,
                   const wchar_t* label, int textPadLeft, HFONT labelFont, COLORREF textColor,
                   bool borderOnly) {
    const RECT r{
        static_cast<int>(b.rect.offset.x),
        static_cast<int>(b.rect.offset.y),
        static_cast<int>(b.rect.offset.x + b.rect.extent.width),
        static_cast<int>(b.rect.offset.y + b.rect.extent.height)};
    const int rr = static_cast<int>(b.radius);
    const COLORREF bcol = RGB(Clamp255(b.border[0]), Clamp255(b.border[1]), Clamp255(b.border[2]));

    HPEN pen = CreatePen(PS_SOLID, borderOnly ? 1 : 2, bcol);
    SelectObject(dc, pen);
    if (borderOnly) {
        // 纯描边（1px 白圈等）：不填充，只画轮廓
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, rr, rr);
    } else {
        const COLORREF fill = RGB(Clamp255(b.color[0]), Clamp255(b.color[1]), Clamp255(b.color[2]));
        HBRUSH br = CreateSolidBrush(fill);
        SelectObject(dc, br);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, rr, rr);
        DeleteObject(br);
    }
    DeleteObject(pen);

    if (label) {
        SelectObject(dc, labelFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor);
        RECT tr = r;
        tr.left += textPadLeft;
        DrawTextW(dc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}
