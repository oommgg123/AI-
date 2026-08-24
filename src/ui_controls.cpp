// ============================================================================
//  ui_controls.cpp —— 顺位布局 / 控件管理器实现
//  见 ui_controls.h 设计说明。核心：分组 + 顺位 + 自定义间距 + 横/竖向 flow
//  + 空间控件(spacer) + 子组容器 + 同组同序冲突级联后移 + 顶层 border 锚点布局。
// ============================================================================
#include "ui_controls.h"
#include <algorithm>

namespace ui {

void ControlManager::reset() { items_.clear(); }

void ControlManager::add(const Ctrl& c) { items_.push_back(c); }

// 同 group 内同 order → 级联后移（被撞的往后排）
void ControlManager::resolveConflicts() {
    std::sort(items_.begin(), items_.end(), [](const Ctrl& a, const Ctrl& b) {
        if (a.group != b.group) return a.group < b.group;
        return a.order < b.order;
    });
    std::string cur;
    int prevOrder = -1;
    for (auto& it : items_) {
        if (it.group != cur) { cur = it.group; prevOrder = -1; }
        if (it.order <= prevOrder) {
            it.order = prevOrder + 1;
            ++it.level;
        }
        prevOrder = it.order;
    }
}

std::vector<Ctrl*> ControlManager::groupItems(const std::string& group) {
    std::vector<Ctrl*> out;
    for (auto& c : items_) if (c.group == group) out.push_back(&c);
    std::sort(out.begin(), out.end(), [](Ctrl* a, Ctrl* b) { return a->order < b->order; });
    return out;
}

bool ControlManager::isTopLevel(const std::string& group) const {
    for (const auto& c : items_) if (c.childGroup == group) return false;
    return true;
}

void ControlManager::layoutGroup(const std::string& group, RECT area) {
    std::vector<Ctrl*> its = groupItems(group);
    if (its.empty()) return;

    // ---- border 锚点布局（顶层 group：上/下/左/右/填充）----
    bool hasAnchor = false;
    for (auto* c : its) if (c->anchor != Anchor::None) { hasAnchor = true; break; }
    if (hasAnchor) {
        int topH = 0, botH = 0, leftW = 0, rightW = 0;
        for (auto* c : its) {
            const int v = (c->fixed >= 0) ? c->fixed : 0;
            switch (c->anchor) {
                case Anchor::Top:    topH    = v; break;
                case Anchor::Bottom: botH    = v; break;
                case Anchor::Left:   leftW   = v; break;
                case Anchor::Right:  rightW  = v; break;
                default: break;
            }
        }
        const int innerX = area.left + leftW;
        const int innerY = area.top + topH;
        int innerW = (area.right - area.left) - leftW - rightW;
        int innerH = (area.bottom - area.top) - topH - botH;
        if (innerW < 0) innerW = 0;
        if (innerH < 0) innerH = 0;
        for (auto* c : its) {
            RECT r{};
            switch (c->anchor) {
                case Anchor::Top:    r = {area.left, area.top, area.right, area.top + topH}; break;
                case Anchor::Bottom: r = {area.left, area.bottom - botH, area.right, area.bottom}; break;
                case Anchor::Left:   r = {area.left, innerY, area.left + leftW, innerY + innerH}; break;
                case Anchor::Right:  r = {area.right - rightW, innerY, area.right, innerY + innerH}; break;
                case Anchor::Fill:
                case Anchor::None:
                default:             r = {innerX, innerY, innerX + innerW, innerY + innerH}; break;
            }
            c->rect = r;
            if (!c->childGroup.empty()) layoutGroup(c->childGroup, r);
        }
        return;
    }

    // ---- flow 流式布局（顺位：横/竖向）----
    const bool vert = (its[0]->axis == Axis::Vertical);
    const int mainStart   = vert ? area.top : area.left;
    const int mainEnd     = vert ? area.bottom : area.right;
    const int crossStart  = vert ? area.left : area.top;
    const int crossExtent = vert ? (area.right - area.left) : (area.bottom - area.top);

    int fixedTotal = 0, gapTotal = 0, weightTotal = 0;
    for (auto* c : its) {
        gapTotal += c->gap;
        if (c->fixed >= 0) fixedTotal += c->fixed;
        else weightTotal += std::max(1, c->weight);
    }
    int mainExtent = mainEnd - mainStart;
    int free = mainExtent - (fixedTotal + gapTotal);
    if (free < 0) free = 0;

    int pos = mainStart;
    for (auto* c : its) {
        pos += c->gap;                       // 本控件之前的自定义间距
        int size;
        if (c->fixed >= 0) size = c->fixed;  // 固定尺寸
        else size = (weightTotal > 0) ? (free * std::max(1, c->weight) / weightTotal) : 0; // 弹性
        RECT r{};
        if (vert) {
            int w = crossExtent;
            if (c->crossFixed >= 0) w = c->crossFixed;   // 交叉轴：左对齐固定宽
            r = {crossStart, pos, crossStart + w, pos + size};
        } else {
            int h = crossExtent;
            if (c->crossFixed >= 0) h = c->crossFixed;   // 交叉轴：顶对齐固定高
            r = {pos, crossStart, pos + size, crossStart + h};
        }
        c->rect = r;
        if (!c->childGroup.empty()) layoutGroup(c->childGroup, r); // 子组容器内再 flow
        pos += size;
    }
}

void ControlManager::compute(RECT client) {
    clientArea_ = client;
    if (items_.empty()) return;
    resolveConflicts();
    // 顶层 group（未被任何容器引用）用 client 布局；其余由容器递归
    for (const auto& c : items_) {
        if (isTopLevel(c.group)) layoutGroup(c.group, client);
    }
}

const Ctrl* ControlManager::find(const std::string& group, int id) const {
    for (const auto& c : items_)
        if (c.group == group && c.id == id) return &c;
    return nullptr;
}

const Ctrl* ControlManager::findGroup(const std::string& group) const {
    for (const auto& c : items_)
        if (c.childGroup == group) return &c;
    return nullptr;
}

std::string ControlManager::dump() const {
    std::string s;
    for (const auto& c : items_) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "[%s] id=%d order=%d lvl=%d axis=%d anchor=%d gap=%d fixed=%d cross=%d wt=%d spacer=%d child=%s"
                 " rect=(%d,%d,%d,%d)\n",
                 c.group.c_str(), c.id, c.order, c.level, (int)c.axis, (int)c.anchor,
                 c.gap, c.fixed, c.crossFixed, c.weight, (int)c.spacer,
                 c.childGroup.c_str(), (int)c.rect.left, (int)c.rect.top, (int)c.rect.right, (int)c.rect.bottom);
        s += buf;
    }
    return s;
}

} // namespace ui
