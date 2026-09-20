#pragma once

#include <QPixmap>

// 工具栏自绘图标（参考 MaxWhiteboard 线稿图标风格：默认灰 #999999 / 激活橙 #FF7D00）
namespace BoardIcons {

enum class Glyph {
    Lasso,      // 套索：虚线圈选 + 节点
    Hand,       // 抓手：手掌
    Undo,       // 撤销：逆时针弯箭头
    Redo,       // 重做：顺时针弯箭头
    ZoomIn,     // 放大：放大镜 +
    ZoomOut,    // 缩小：放大镜 -
    ZoomReset,  // 100%：四角取景框
    Interact,   // 互动：广播信号波纹
    Settings,   // 设置：齿轮
    More,       // 更多：横排三点
    Save,       // 保存：向下箭头 + 底线（存入文件）
    Open,       // 打开：文件夹（从文件读取）
    Exit,        // 退出：电源符号（退出程序）
    Highlighter, // 荧光笔：斜置粗头笔 + 底部荧光线（笔设置面板类型切换用）
    Shape,       // 图形：圆 + 矩形叠加（图形工具入口）
    MindMap,     // 思维导图：中心节点 + 环向节点与连线
    Table,       // 表格：网格（外框 + 中横线 + 中竖线）
    Text,        // 文字：字母 T 线稿（文字工具入口）
    Widget,      // 小工具：秒表线稿（圆盘 + 顶冠 + 指针；小工具工具入口）
    Other,       // 其他：2x2 圆角方块网格（图形/导图/表格/文字/小工具汇总入口）
    Mouse,       // 鼠标：箭头光标线稿（纯鼠标工具，不拦截画布事件）
    Clear,       // 清屏：板擦 + 扫除散点（清除整页）
    Copy,        // 复制：双叠矩形（复制页面）
    Rect,        // 矩形：单矩形线框（圆盘外环图形选项）
    Circle,      // 圆形：单圆线框
    Ellipse,     // 椭圆：扁椭圆线框
    Triangle,    // 三角形：单三角线框
    ColorWheel,  // 色轮：多色相环（自定义取色入口；固定彩色绘制）
};

// 返回 48x48 逻辑尺寸（2x 采样）的图标：active=true 为激活橙色，false 为默认灰色
QPixmap pixmap(Glyph glyph, bool active);

}  // namespace BoardIcons
