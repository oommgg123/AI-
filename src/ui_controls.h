// ============================================================================
//  ui_controls.h —— 顺位布局 / 控件管理器（专用有序布局）
//  设计（用户 2026-08-23 排版优化）：
//   - 每个控件创建时上报「所属部分(group) + 该部分内顺位(order)」
//   - 支持自定义间距(gap)、横向/竖向流向(axis)、空间控件(spacer 预留范围/偏移/缩放)
//   - 子组容器(childGroup)：本控件内部另起一组 flow（如滑块行 = 标签 + 轨道）
//   - 同 group 内同 order 冲突 → 级联后移（被撞的往后排）
//   - 顶层 group 可带锚点(anchor)做 border 布局（上/下/左/右/填充），用于主视口
// ============================================================================
#pragma once
#include <windows.h>
#include <vector>
#include <string>

namespace ui {

enum class Axis { Horizontal, Vertical };
enum class Anchor { None, Top, Bottom, Left, Right, Fill };

struct Ctrl {
    std::string group;      // 所属部分（如 "settings" / "viewport"）
    int         order = 0;  // 该部分内顺位（从 0 起）
    Axis        axis  = Axis::Vertical;  // 本控件若为容器，内部流向
    Anchor      anchor = Anchor::None;   // 顶层 group 用：上/下/左/右/填充（border 布局）
    int         gap    = 0;  // 自定义间距：主轴方向、本控件之前的间隙
    int         fixed  = -1; // 主轴固定尺寸(px)；-1 = 按 weight 弹性分配
    int         crossFixed = -1; // 交叉轴固定尺寸(px)；-1 = 填满该组交叉范围
    int         weight = 0;  // 弹性权重（fixed==-1 时生效）
    int         id     = 0;  // 调用方回查标识
    bool        spacer = false; // 空间控件（仅占位/预留范围/偏移/缩放）
    std::string childGroup;  // 容器→子部分名（内部另起 flow，rect 填此控件范围）
    RECT        rect{0, 0, 0, 0}; // 计算结果
    int         level  = 0;  // 冲突后移计数（调试用）
};

class ControlManager {
public:
    void reset();                       // 清空注册表（重建用）
    void add(const Ctrl& c);            // 注册一个控件（同 group+order 冲突→级联后移）
    void compute(RECT client);          // 计算所有 group 的 rect（顶层 group 用 client）
    const Ctrl* find(const std::string& group, int id) const; // 取某控件 rect
    const Ctrl* findGroup(const std::string& group) const;     // 取容器（childGroup==group）的 rect
    std::string dump() const;           // 调试：导出注册表文本

private:
    std::vector<Ctrl> items_;
    void resolveConflicts();                            // 同 group 同 order 级联后移
    void layoutGroup(const std::string& group, RECT area); // 递归布局一组
    std::vector<Ctrl*> groupItems(const std::string& group); // 取某 group 的控件
    bool isTopLevel(const std::string& group) const;   // 是否未被任何容器引用
    RECT clientArea_;                                  // 最近一次 compute 的 client
};

} // namespace ui
