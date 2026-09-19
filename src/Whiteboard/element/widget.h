#pragma once
#include "Whiteboard/element/element.h"

namespace whiteboard {

// 小工具元素（秒表 / 计时器 / 计算器 / 算盘 / 骰子 / 大转盘 / 点名器）：苹果风格卡片。
// 数据层仅占位——类型、位置、缩放、计时器时长、骰子参数与转盘/点名器设置；
// 运行状态（走时/暂停/归零/计算器数值/珠位/骰子结果/旋转角度/滚动进度/抽中
// 结果/已抽记录）为渲染端本地运行时，不落数据（无互动同步能力）。
// 整体变换烘焙写回位置与等比缩放（scale 限制 0.5 ~ 3.0），不支持旋转。
class WidgetElement : public Element {
public:
    WidgetElement();

    std::string GetType() const override { return "Widget"; }

    // chrono id + 默认值（秒表 / 5 分钟时长 / 1 倍缩放 / 6 面 2 颗骰子）
    void Reset();

    int kind = 0;           // 0=秒表 1=计时器 2=计算器 3=算盘 4=骰子 5=大转盘 6=点名器
    int x = 0;              // 卡片左上角（页面绝对坐标）
    int y = 0;
    int durationSec = 300;  // 计时器时长（秒；其他类型忽略；卡片内滚轮设置态提交）
    float scale = 1.0f;     // 等比缩放（0.5 ~ 3.0；卡片基础尺寸 280x156）
    int diceSides = 6;      // 骰子面数（4 / 6 / 8 / 12 / 20；其他类型忽略）
    int diceCount = 2;      // 骰子颗数（1 ~ 10；其他类型忽略）
    // 大转盘 / 点名器（kind 5/6）：候选项文本（UTF-8，每行一项；「名称*3」后缀
    // 表示权重 3）+ 去重模式（抽中自动移出）+ 点名器一次抽取人数（1 ~ 5）
    std::string options;
    bool dedup = false;
    int pickCount = 1;
};

}
