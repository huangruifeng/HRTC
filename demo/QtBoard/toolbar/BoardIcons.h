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
};

// 返回 48x48 逻辑尺寸（2x 采样）的图标：active=true 为激活橙色，false 为默认灰色
QPixmap pixmap(Glyph glyph, bool active);

}  // namespace BoardIcons
