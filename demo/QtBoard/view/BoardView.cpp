#include "BoardView.h"

#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsRectItem>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextOption>
#include <QTimer>
#include <QVariantList>
#include <QWheelEvent>
#include <QtGlobal>
#include <QtMath>

#include <cmath>
#include <functional>

#include "BoardUtil.h"

namespace {
// 黑板墨绿背景（单一来源：BoardView::boardDefaultColor）
const QColor kBoardBackground = BoardView::boardDefaultColor();

// ---------- 数据层元素 -> QPainterPath（另存为文件内局部函数，避免污染头文件） ----------

// 与 paintThumbElement 内联笔一致（strokePen 为 BoardView 私有，此地独立一份）
QPen elementPen(uint32_t color, int width) {
    return QPen(colorToQColor(color), qMax(1, width),
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QPainterPath strokePath(const whiteboard::Stroke& s) {
    QPainterPath path;
    for (size_t i = 0; i < s.points.size(); ++i) {
        const whiteboard::Point& p = s.points[i];
        if (i == 0)
            path.moveTo(p.x, p.y);
        else
            path.lineTo(p.x, p.y);
    }
    return path;
}

// 图形：多子路径（closed = 首尾闭合描边）
QPainterPath graphicPath(const whiteboard::GraphicElement& g) {
    QPainterPath path;
    for (const auto& sp : g.subpaths) {
        if (sp.points.empty())
            continue;
        path.moveTo(sp.points[0].x, sp.points[0].y);
        for (size_t i = 1; i < sp.points.size(); ++i)
            path.lineTo(sp.points[i].x, sp.points[i].y);
        if (sp.closed)
            path.closeSubpath();
    }
    return path;
}

// 文字统一字体（内联编辑与字形渲染共用，保证所见即所得）：
// 显式族名避免平台默认字体在 QTextEdit 与 addText 两条解析路径结果不一致
QFont textElementFont(int pixelSize) {
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(qMax(1, pixelSize));
    return font;
}

// 文字：逐行 addText 字形路径（锚点 = 左上角，'\n' 硬换行）。
// 数据层不存字体度量——本函数与 data(1) 包围盒、缩略图共用同一字体来源。
QPainterPath textPath(const whiteboard::TextElement& t) {
    QPainterPath path;
    if (t.text.empty())
        return path;
    QFont font = textElementFont(t.fontSize);
    const QFontMetricsF fm(font);
    const qreal lineSpacing = fm.lineSpacing();
    const QStringList lines = QString::fromUtf8(t.text.c_str()).split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i)
        path.addText(t.x, t.y + fm.ascent() + i * lineSpacing, font, lines[i]);
    return path;
}

// 文字内联编辑器：Enter 提交 / Shift+Enter 换行 / Esc 取消 / 失焦提交。
// 文件内类用 std::function 回调（无需 Q_OBJECT/moc）。
class InlineTextEditor : public QTextEdit {
public:
    std::function<void()> onSubmit;
    std::function<void()> onCancel;
    bool closing = false;  // 提交/取消清理中：抑制 focusOut 重复触发

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            if (!closing && onCancel)
                onCancel();
            e->accept();
            return;
        }
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
            !(e->modifiers() & Qt::ShiftModifier)) {
            if (!closing && onSubmit)
                onSubmit();
            e->accept();
            return;
        }
        QTextEdit::keyPressEvent(e);
    }

    void focusOutEvent(QFocusEvent* e) override {
        QTextEdit::focusOutEvent(e);
        if (!closing && onSubmit)
            onSubmit();
    }
};

// 转盘/点名器选项编辑弹窗：多行编辑器（Enter 换行 / Ctrl+Enter 提交 / Esc 取消 / 失焦提交）。
// 与 InlineTextEditor 差异：Enter 为换行语义，Ctrl+Enter 才提交（多行名单编辑）
class OptionsEditor : public QTextEdit {
public:
    std::function<void()> onSubmit;
    std::function<void()> onCancel;
    bool closing = false;  // 提交/取消清理中：抑制 focusOut 重复触发

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            if (!closing && onCancel)
                onCancel();
            e->accept();
            return;
        }
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
            (e->modifiers() & Qt::ControlModifier)) {
            if (!closing && onSubmit)
                onSubmit();
            e->accept();
            return;
        }
        QTextEdit::keyPressEvent(e);  // Enter 等其余按键：原样（换行）
    }

    void focusOutEvent(QFocusEvent* e) override {
        QTextEdit::focusOutEvent(e);
        if (!closing && onSubmit)
            onSubmit();
    }
};

// ---------- 小工具卡片（秒表/计时器，苹果风格；文件内图元） ----------

// 卡片基础几何（基础坐标系 280x156；实际显示尺寸 = 基础尺寸 x scale，绘制时统一缩放）
constexpr qreal kWidgetCardW = 280.0;      // 卡片宽（1 倍）
constexpr qreal kWidgetCardH = 156.0;      // 卡片高（1 倍）
constexpr qreal kWidgetCardRadius = 26.0;  // 卡片圆角
constexpr qreal kWidgetBtnRadius = 26.0;   // 圆形按钮半径
constexpr qreal kWidgetBtnY = 116.0;                   // 按钮圆心距卡片顶
constexpr qreal kWidgetBtnLX = 88.0;                   // 左按钮（重置）圆心距卡片左
constexpr qreal kWidgetBtnRX = kWidgetCardW - 88.0;    // 右按钮（开始/暂停）圆心距卡片左
constexpr qreal kWidgetMinScale = 0.5;     // 卡片缩放下限（140x78）
constexpr qreal kWidgetMaxScale = 3.0;     // 卡片缩放上限（840x468）

// 计时器设置态滚轮几何（基础坐标系）：三列（时/分/秒），上/中/下三行可见，
// 中央行橙色高亮大号，两端渐小渐暗；滚动回卷（时 0~23，分/秒 0~59）
constexpr qreal kWidgetWheelColW = 64.0;                       // 列宽（同时为命中区宽）
constexpr qreal kWidgetWheelColX[3] = { 64.0, 140.0, 216.0 };  // 三列中心 x
constexpr qreal kWidgetWheelRowY[3] = { 20.0, 48.0, 76.0 };    // 三行中心 y（上/中/下）
constexpr qreal kWidgetWheelTop = 6.0;                         // 滚轮区域上缘
constexpr qreal kWidgetWheelBottom = 90.0;                     // 滚轮区域下缘

// 苹果风配色：深色卡 + 白数字（归零橙）+ 灰/绿/红圆钮
const QColor kWidgetCardColor(28, 28, 30);    // #1C1C1E
const QColor kWidgetDigits(255, 255, 255);    // 数字白
const QColor kWidgetFinished(255, 159, 10);   // #FF9F0A 计时器归零
const QColor kWidgetBtnGray(72, 72, 74);      // #48484A 重置钮
const QColor kWidgetBtnGreen(48, 209, 88);    // #30D158 开始钮
const QColor kWidgetBtnRed(255, 69, 58);      // #FF453A 暂停钮

// ---------- 计算器（kind=2，基础坐标系）：显示屏 + 4 列 x 5 行键盘 ----------
// 键位：C ⌫ ÷ × / 7 8 9 − / 4 5 6 + / 1 2 3 =(跨两行) / ± 0 .
constexpr qreal kCalcScreenX = 16.0;                 // 显示屏（右对齐；不消费事件 = 拖动把手）
constexpr qreal kCalcScreenY = 6.0;
constexpr qreal kCalcScreenW = kWidgetCardW - 32.0;
constexpr qreal kCalcScreenH = 34.0;
constexpr qreal kCalcKeyX0 = 16.0;                   // 键盘左上（y 44~150 均布 5 行）
constexpr qreal kCalcKeyY0 = 44.0;
constexpr qreal kCalcKeyW = 59.0;
constexpr qreal kCalcKeyH = 18.0;
constexpr qreal kCalcKeyGap = 4.0;
const QColor kCalcNumKey(0x3A, 0x3A, 0x3C);   // 数字键深灰
const QColor kCalcFnKey(0x5A, 0x5A, 0x5F);    // 功能键（C / ⌫ / ±）浅灰
const QColor kCalcOpKey(0xFF, 0x9F, 0x0A);    // 运算符橙（÷ × − + =）

// 计算器键几何：索引 0~18（见 kCalcKeyLabel 顺序；索引 15 = "=" 纵跨行 3~4）
QRectF calcKeyRect(int index) {
    static const int keyRow[19] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4 };
    static const int keyCol[19] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2 };
    const qreal x = kCalcKeyX0 + keyCol[index] * (kCalcKeyW + kCalcKeyGap);
    const qreal y = kCalcKeyY0 + keyRow[index] * (kCalcKeyH + kCalcKeyGap);
    const qreal h = (index == 15) ? (kCalcKeyH * 2 + kCalcKeyGap) : kCalcKeyH;
    return QRectF(x, y, kCalcKeyW, h);
}

QString calcKeyLabel(int index) {
    static const char* labels[19] = { "C", "⌫", "÷", "×", "7", "8", "9", "−",
                                      "4", "5", "6", "+", "1", "2", "3", "=", "±", "0", "." };
    return QString::fromUtf8(labels[index]);
}

// ---------- 算盘（kind=3，基础坐标系）：9 档，上 1 珠（=5）+ 下 4 珠（=1） ----------
constexpr int kAbacusCols = 9;
constexpr qreal kAbacusColX0 = 23.0;        // 第 0 档中心 x（档间距 25 → 23..223）
constexpr qreal kAbacusColDX = 25.0;
constexpr qreal kAbacusTop = 12.0;          // 档柱上端
constexpr qreal kAbacusBottom = 150.0;      // 档柱下端
constexpr qreal kAbacusBeamY = 52.0;        // 横梁（高 8）
constexpr qreal kAbacusBeadW = 17.0;        // 珠宽
constexpr qreal kAbacusBeadH = 15.0;        // 珠高
constexpr qreal kAbacusHighUpY = 24.0;      // 上珠离梁（未拨下）中心 y
constexpr qreal kAbacusHighDownY = 43.0;    // 上珠贴梁（拨下 = 启用 5）中心 y
constexpr qreal kAbacusLowTopY = 70.0;      // 下珠贴梁聚集：第 1 颗中心 y（间距 16）
constexpr qreal kAbacusLowDY = 16.0;
constexpr qreal kAbacusLowBottomY = 140.0;  // 下珠底部聚集：最底槽中心 y
constexpr qreal kAbacusClearX = 252.0;      // 清盘圆钮中心（横梁右端外侧）
constexpr qreal kAbacusClearY = 106.0;
constexpr qreal kAbacusClearR = 13.0;
const QColor kWidgetBeadColor(201, 160, 99);  // 珠色（原木色）
const QColor kWidgetColColor(0x4A, 0x4A, 0x4E);  // 档柱色

// 下珠槽中心 y：low = 当前拨起数（0~4），b = 珠槽索引（0 = 最靠梁）
qreal abacusLowBeadY(int low, int b) {
    if (b < low)
        return kAbacusLowTopY + b * kAbacusLowDY;            // 拨起：贴梁聚集
    return kAbacusLowBottomY - (3 - b) * kAbacusLowDY;       // 未拨：底部聚集
}

// ---------- 骰子（kind=4，基础坐标系）：设置态 / 滚动态 / 结果态 ----------
const int kDiceSidesOption[5] = { 4, 6, 8, 12, 20 };
constexpr qreal kDiceSideBtnY = 10.0;       // 面数按钮行（5 个 44x26，间距 8，居中）
constexpr qreal kDiceSideBtnW = 44.0;
constexpr qreal kDiceSideBtnH = 26.0;
constexpr qreal kDiceSideBtnGap = 8.0;
constexpr qreal kDiceCountBtnY = 44.0;      // 数量行：− [n] +（居中）
constexpr qreal kDiceCountBtnW = 32.0;
constexpr qreal kDiceCountBtnH = 28.0;
constexpr qreal kDiceOkY = 82.0;            // 确定横条（164x36，居中，绿色）
constexpr qreal kDiceOkW = 164.0;
constexpr qreal kDiceOkH = 36.0;
constexpr qreal kDiceCountRowW = 128.0;     // 数量行总宽（− 32 + 数值 64 + + 32，居中）
constexpr qreal kDiceCountRowX = (kWidgetCardW - kDiceCountRowW) / 2.0;
constexpr qreal kDiceCountNumW = 64.0;
constexpr qreal kDiceGridX = 12.0;          // 滚动/结果：骰子网格区（最多 5 列 x 2 行）
constexpr qreal kDiceGridY = 4.0;
constexpr qreal kDiceGridW = 256.0;
constexpr qreal kDiceGridH = 84.0;
const QColor kDiceFaceColor(0xF2, 0xF2, 0xF4);  // 骰面白

// ---------- 大转盘（kind=5，基础坐标系）：圆形转盘 + 右侧操作面板 ----------
constexpr qreal kWheelCx = 74.0;          // 转盘圆心
constexpr qreal kWheelCy = 78.0;
constexpr qreal kWheelR = 62.0;           // 转盘半径
constexpr qreal kWheelPanelX = 144.0;     // 右侧面板左缘（144 ~ 268）
constexpr qreal kWheelPanelW = 124.0;
constexpr qreal kWheelResultY = 14.0;     // 结果文本区（顶）
constexpr qreal kWheelResultH = 42.0;
constexpr qreal kWheelStartY = 64.0;      // 开始钮（绿条，中）
constexpr qreal kWheelStartH = 36.0;
constexpr qreal kWheelRowBtnY = 110.0;    // 三小按钮行（编辑 / 去重 / 重置）
constexpr qreal kWheelRowBtnH = 26.0;
constexpr qreal kWheelRowBtnW = 38.0;
constexpr qreal kWheelRowBtnGap = 5.0;
const QColor kWheelResultGold(0xFF, 0xD3, 0x5C);  // 抽中结果金色
const QColor kWheelIdleGray(0x8A, 0x8A, 0x8E);    // 无结果/提示灰
// 扇区 6 色循环调色板（暖色系）
const QColor kWheelPalette[6] = {
    QColor(0xE8, 0x82, 0x3C), QColor(0xF2, 0xB7, 0x56), QColor(0x7F, 0xB0, 0x69),
    QColor(0x5B, 0x9B, 0xD5), QColor(0xB0, 0x7C, 0xC6), QColor(0xE0, 0x6C, 0x75),
};

// ---------- 点名器（kind=6，基础坐标系）：顶行工具 + 名字显示区 + 底部圆钮 ----------
constexpr qreal kNameEditX = 14.0;        // 编辑名单钮
constexpr qreal kNameEditY = 10.0;
constexpr qreal kNameEditW = 44.0;
constexpr qreal kNameEditH = 24.0;
constexpr qreal kNameCountMinusX = 62.0;  // 人数 − [n] +
constexpr qreal kNameCountY = 10.0;
constexpr qreal kNameCountBtnW = 24.0;
constexpr qreal kNameCountBtnH = 24.0;
constexpr qreal kNameCountNumX = 86.0;
constexpr qreal kNameCountNumW = 32.0;
constexpr qreal kNameCountPlusX = 118.0;
constexpr qreal kNameStatusX = 146.0;     // 状态文本（已点 m/共 n）
constexpr qreal kNameStatusW = 62.0;
constexpr qreal kNameDedupX = 208.0;      // 去重开关
constexpr qreal kNameDedupW = 58.0;
constexpr qreal kNameBoardX = 14.0;       // 名字显示区（圆角深底）
constexpr qreal kNameBoardY = 40.0;
constexpr qreal kNameBoardW = 252.0;
constexpr qreal kNameBoardH = 48.0;

// 选项文本解析（转盘/点名器共用）：每行一项（忽略空行）；「名称*N」（行内最后一个
// * 后为 2~99 纯整数）提取权重（名称 = 前缀，允许名称本身含 *），否则整行权重 1
void parseOptions(const QString& text, QStringList& names, QVector<double>& weights)
{
    names.clear();
    weights.clear();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        double weight = 1.0;
        const int star = line.lastIndexOf(QLatin1Char('*'));
        if (star > 0 && star < line.length() - 1) {
            bool ok = false;
            const int w = line.mid(star + 1).toInt(&ok);
            if (ok && w >= 2 && w <= 99) {
                const QString namePart = line.left(star).trimmed();
                if (!namePart.isEmpty()) {
                    line = namePart;
                    weight = w;
                }
            }
        }
        names.append(line);
        weights.append(weight);
    }
}

// 加权随机抽取：返回候选索引（exclude 非空时排除其中名字，用于去重）；
// -1 = 无可抽项
int weightedPick(const QStringList& names, const QVector<double>& weights,
                 const QSet<QString>& exclude)
{
    QVector<int> candidates;
    double total = 0.0;
    for (int i = 0; i < names.size(); ++i) {
        if (exclude.contains(names[i]))
            continue;
        candidates.append(i);
        total += (i < weights.size() && weights[i] > 0.0) ? weights[i] : 1.0;
    }
    if (candidates.isEmpty() || total <= 0.0)
        return -1;
    double r = QRandomGenerator::global()->generateDouble() * total;
    for (int idx : candidates) {
        const double w = (idx < weights.size() && weights[idx] > 0.0) ? weights[idx] : 1.0;
        r -= w;
        if (r <= 0.0)
            return idx;
    }
    return candidates.last();
}

// 转盘扇区角度（从顶部顺时针累计，度）：按权重比例分配（与概率一致）
void wheelSectorAngles(const QVector<double>& weights, QVector<double>& startDeg,
                       QVector<double>& sweepDeg)
{
    const int n = weights.size();
    startDeg.resize(n);
    sweepDeg.resize(n);
    double total = 0.0;
    for (double w : weights)
        total += (w > 0.0) ? w : 1.0;
    if (total <= 0.0)
        total = 1.0;
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = (weights[i] > 0.0) ? weights[i] : 1.0;
        const double sweep = w / total * 360.0;
        startDeg[i] = acc;
        sweepDeg[i] = sweep;
        acc += sweep;
    }
}

// 缓出插值（cubic）：t∈[0,1] → 0..1（先快后慢，旋转定格手感）
double easeOutCubic(double t)
{
    t = qBound(0.0, t, 1.0);
    const double u = 1.0 - t;
    return 1.0 - u * u * u;
}

// 归一化骰子面数（非法值回退 6）
int normalizeDiceSides(int sides) {
    for (int opt : kDiceSidesOption) {
        if (sides == opt)
            return sides;
    }
    return 6;
}

// 数字字体：等宽（宽度恒定，走时不抖动）
QFont widgetDigitsFont() {
    QFont font(QStringLiteral("Consolas"));
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(46);
    font.setWeight(QFont::DemiBold);
    return font;
}

// 计时器剩余文本：≥1 小时用 H:MM:SS，否则 MM:SS（向上取整：起点显示完整时长，
// 归零 00:00）
QString widgetTimerText(qint64 remainMs) {
    const qint64 sec = remainMs <= 0 ? 0 : (remainMs + 999) / 1000;
    if (sec >= 3600) {
        return QStringLiteral("%1:%2:%3")
            .arg(sec / 3600)
            .arg((sec / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(sec % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(sec / 60, 2, 10, QLatin1Char('0'))
        .arg(sec % 60, 2, 10, QLatin1Char('0'));
}

// 初始/静态显示文本：秒表 00:00.00；计时器按设定时长（≥1h 用 H:MM:SS）
QString widgetInitialText(int kind, int durationSec) {
    if (kind == 0)
        return QStringLiteral("00:00.00");
    return widgetTimerText(static_cast<qint64>(qMax(0, durationSec)) * 1000);
}

// 秒表走时文本 mm:ss.cc（百分秒）
QString widgetStopwatchText(qint64 elapsedMs) {
    const qint64 cs = elapsedMs / 10;
    return QStringLiteral("%1:%2.%3")
        .arg(cs / 6000, 2, 10, QLatin1Char('0'))
        .arg((cs / 100) % 60, 2, 10, QLatin1Char('0'))
        .arg(cs % 100, 2, 10, QLatin1Char('0'));
}

// 卡片圆角矩形路径（页面绝对坐标，按 scale 缩放；命中/框选/选择框走既有管线）
QPainterPath widgetCardPath(qreal x, qreal y, qreal scale) {
    QPainterPath path;
    path.addRoundedRect(QRectF(x, y, kWidgetCardW * scale, kWidgetCardH * scale),
                        kWidgetCardRadius * scale, kWidgetCardRadius * scale);
    return path;
}

// 重置按钮 glyph：白环形箭头（缺口右侧，箭头置于缺口下缘）；r = 环半径（默认大钮）
void paintResetGlyph(QPainter* p, const QPointF& c, qreal r = 8.0) {
    p->save();
    const qreal k = r / 8.0;  // 相对基准几何的缩放
    QPen pen(Qt::white, 2.4 * k);
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 60 * 16, 300 * 16);
    p->setPen(Qt::NoPen);
    p->setBrush(Qt::white);
    QPolygonF tri;
    tri << QPointF(c.x() + 8.4 * k, c.y() + 0.6 * k)
        << QPointF(c.x() + 3.6 * k, c.y() - 0.6 * k)
        << QPointF(c.x() + 6.8 * k, c.y() + 5.0 * k);
    p->drawPolygon(tri);
    p->restore();
}

// 开始/暂停 glyph：白色 ▶ 三角 / ∥ 双圆角竖条
void paintRunGlyph(QPainter* p, const QPointF& c, bool running) {
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(Qt::white);
    if (!running) {
        QPolygonF tri;
        tri << QPointF(c.x() - 6, c.y() - 9)
            << QPointF(c.x() - 6, c.y() + 9)
            << QPointF(c.x() + 10, c.y());
        p->drawPolygon(tri);
    } else {
        const qreal w = 6.0, h = 18.0, gap = 4.0;
        p->drawRoundedRect(QRectF(c.x() - gap / 2 - w, c.y() - h / 2, w, h), 2, 2);
        p->drawRoundedRect(QRectF(c.x() + gap / 2, c.y() - h / 2, w, h), 2, 2);
    }
    p->restore();
}

// 小工具卡片图元：深色圆角卡 + 大号等宽数字 + 两个圆形按钮。QGraphicsPathItem
// 默认单 pen/brush 无法表达多色卡片，故覆写 paint 整卡自绘；path 仍为卡片圆角
// 矩形（页面绝对坐标，尺寸随 scale），命中/框选/选择框继续走既有管线。
// 绘制统一在基础坐标系（280x156）进行：paint 内平移到卡片左上角并 scale(s,s)；
// 命中判定把局部坐标映射回基础坐标系后按基础几何（按钮圆/滚轮列）判定。
// 计时器支持设置态：卡片内三列时分秒滚轮（上/中/下三行），滚动调整时长。
class WidgetCardItem : public QGraphicsPathItem {
public:
    QRectF cardRect;        // 卡片矩形（页面绝对坐标 = 局部坐标，无变换；宽高 = 基础尺寸 x scale）
    QString displayText;    // 倒计时显示态文本（秒表 mm:ss.cc / 计时器 mm:ss 或 H:MM:SS）
    bool running = false;   // 走时中（按钮显示红 ∥）
    bool finished = false;  // 计时器归零（数字变橙）
    bool setupMode = false; // 计时器设置态（显示时分秒滚轮）
    int wheelH = 0;         // 滚轮值：时 0~23（设置态）
    int wheelM = 5;         // 分 0~59
    int wheelS = 0;         // 秒 0~59
    int kind = 0;           // 类型：0=秒表 1=计时器 2=计算器 3=算盘 4=骰子（buildWidgetItem 赋值）
    // 计算器（kind=2）：显示文本 + 错误标记（红字）
    QString calcDisplay = QStringLiteral("0");
    bool calcError = false;
    // 算盘（kind=3）：上珠 0/1（1=贴梁启用）+ 下珠拨起数 0~4
    int abacusHigh[9] = {};
    int abacusLow[9] = {};
    // 骰子（kind=4）：参数 + 滚动/结果（values=当前显示面，jitter=滚动抖动角）
    int diceSides = 6;
    int diceCount = 1;
    bool diceRolling = false;
    int diceValues[10] = {};
    int diceJitter[10] = {};
    // 大转盘（kind=5）：原始选项文本（编辑弹窗载入）+ 解析名单/权重/已抽 + 状态
    QString wheelOptionsText;
    QStringList wheelNames;
    QVector<double> wheelWeights;
    QSet<QString> wheelDrawn;
    double wheelAngle = 0.0;
    bool wheelSpinning = false;
    QString wheelResult;
    QString wheelHint;  // 无法启动时的灰字提示（成功启动/重置/编辑后清空）
    bool wheelDedup = false;
    // 点名器（kind=6）：原始名单文本 + 解析名单/已抽 + 滚动/结果/人数/去重
    QString nameOptionsText;
    QStringList nameNames;
    QSet<QString> nameDrawn;
    bool nameRolling = false;
    QStringList nameRollTexts;  // 滚动中每格显示文本
    QStringList nameResults;    // 定格结果（1~5 个名字）
    QString nameHint;           // 无法启动时的灰字提示（成功启动/重置/编辑后清空）
    int namePickCount = 1;
    bool nameDedup = false;

    // 缩放系数（基础尺寸 280x156 → 实际卡片尺寸的比例）
    qreal scaleFactor() const { return cardRect.width() / kWidgetCardW; }

    // 页面局部坐标 → 基础坐标系（280x156）
    QPointF toBase(const QPointF& local) const {
        const qreal s = scaleFactor();
        return s > 0 ? (local - cardRect.topLeft()) / s : QPointF();
    }

    void setRuntimeState(const QString& text, bool isRunning, bool isFinished) {
        if (!setupMode && displayText == text && running == isRunning && finished == isFinished)
            return;
        setupMode = false;
        displayText = text;
        running = isRunning;
        finished = isFinished;
        update();
    }

    // 设置态（滚轮显示）：值与外部运行时同步
    void setSetupState(int h, int m, int s) {
        if (setupMode && wheelH == h && wheelM == m && wheelS == s)
            return;
        setupMode = true;
        wheelH = h;
        wheelM = m;
        wheelS = s;
        update();
    }

    // 计算器显示同步（kind=2）：显示文本 + 错误标记
    void setCalcState(const QString& text, bool error) {
        if (calcDisplay == text && calcError == error)
            return;
        calcDisplay = text;
        calcError = error;
        update();
    }

    // 算盘珠位同步（kind=3）：上珠 0/1 数组 + 下珠拨起数 0~4 数组
    void setAbacusState(const int high[9], const int low[9]) {
        bool same = true;
        for (int i = 0; i < 9 && same; ++i)
            same = (abacusHigh[i] == high[i] && abacusLow[i] == low[i]);
        if (same)
            return;
        for (int i = 0; i < 9; ++i) {
            abacusHigh[i] = high[i];
            abacusLow[i] = low[i];
        }
        update();
    }

    // 骰子状态同步（kind=4）：设置态显示参数选择；滚动/结果态显示骰面网格
    void setDiceState(int sides, int count, bool setup, bool rolling,
                      const int values[10], const int jitter[10]) {
        diceSides = sides;
        diceCount = count;
        setupMode = setup;
        diceRolling = rolling;
        for (int i = 0; i < 10; ++i) {
            diceValues[i] = values[i];
            diceJitter[i] = jitter[i];
        }
        update();
    }

    // 转盘状态同步（kind=5）：选项文本（编辑弹窗载入）/解析名单/权重/已抽集合/
    // 当前角度/旋转中/结果/去重开关
    void setWheelState(const QString& optionsText, const QStringList& names,
                       const QVector<double>& weights, const QSet<QString>& drawn,
                       double angle, bool spinning, const QString& result, bool dedup,
                       const QString& hint) {
        wheelOptionsText = optionsText;
        wheelNames = names;
        wheelWeights = weights;
        wheelDrawn = drawn;
        wheelAngle = angle;
        wheelSpinning = spinning;
        wheelResult = result;
        wheelDedup = dedup;
        wheelHint = hint;
        update();
    }

    // 点名器状态同步（kind=6）：名单文本/解析名单/已抽集合/滚动中/滚动格文本/
    // 定格结果/人数/去重开关
    void setNameState(const QString& optionsText, const QStringList& names,
                      const QSet<QString>& drawn, bool rolling, const QStringList& rollTexts,
                      const QStringList& results, int pickCount, bool dedup,
                      const QString& hint) {
        nameOptionsText = optionsText;
        nameNames = names;
        nameDrawn = drawn;
        nameRolling = rolling;
        nameRollTexts = rollTexts;
        nameResults = results;
        namePickCount = pickCount;
        nameDedup = dedup;
        nameHint = hint;
        update();
    }

    // 统一控件命中（入参页面局部坐标；映射回基础坐标判定，2px 容差）。控件码：
    // 0/1 = 底部左/右圆钮（仅显示时命中）；100+i = 计算器键 i（0~18）；
    // 200+col*6+t = 算盘珠：t=0 上珠 / t=1..4 下珠槽 b=t-1；300 = 算盘清盘钮；
    // 400+j = 骰子面数按钮 j（0~4）；410/411 = 数量 −/+；412 = 骰子确定；
    // 500 = 转盘开始；501~503 = 转盘编辑/去重/重置；601 = 点名器编辑；
    // 602 = 点名器去重；603/604 = 点名器人数 −/+。
    // -1 = 无控件（卡片其余区域由调用方决定拖动/消费策略）。
    int controlAt(const QPointF& local) const {
        const QPointF base = toBase(local);
        const qreal tol = 2.0;
        // 底部圆钮：秒表/计时器恒显示；骰子仅结果态显示（设置/滚动态隐藏）；点名器恒显示
        if (kind <= 1 || (kind == 4 && !setupMode && !diceRolling) || kind == 6) {
            const qreal r = kWidgetBtnRadius + tol;
            const QPointF dl = base - QPointF(kWidgetBtnLX, kWidgetBtnY);
            if (dl.x() * dl.x() + dl.y() * dl.y() <= r * r)
                return 0;
            const QPointF dr = base - QPointF(kWidgetBtnRX, kWidgetBtnY);
            if (dr.x() * dr.x() + dr.y() * dr.y() <= r * r)
                return 1;
        }
        if (kind == 2) {
            for (int i = 0; i < 19; ++i) {
                if (calcKeyRect(i).adjusted(-tol, -tol, tol, tol).contains(base))
                    return 100 + i;
            }
            return -1;
        }
        if (kind == 3) {
            const QPointF dc = base - QPointF(kAbacusClearX, kAbacusClearY);
            if (dc.x() * dc.x() + dc.y() * dc.y() <= (kAbacusClearR + tol) * (kAbacusClearR + tol))
                return 300;
            for (int col = 0; col < kAbacusCols; ++col) {
                const qreal cx = kAbacusColX0 + col * kAbacusColDX;
                const qreal hy = abacusHigh[col] > 0 ? kAbacusHighDownY : kAbacusHighUpY;
                if (QRectF(cx - kAbacusBeadW / 2 - tol, hy - kAbacusBeadH / 2 - tol,
                           kAbacusBeadW + 2 * tol, kAbacusBeadH + 2 * tol).contains(base))
                    return 200 + col * 6;  // 上珠
                for (int b = 0; b < 4; ++b) {
                    const qreal by = abacusLowBeadY(abacusLow[col], b);
                    if (QRectF(cx - kAbacusBeadW / 2 - tol, by - kAbacusBeadH / 2 - tol,
                               kAbacusBeadW + 2 * tol, kAbacusBeadH + 2 * tol).contains(base))
                        return 200 + col * 6 + 1 + b;  // 下珠槽 b
                }
            }
            return -1;
        }
        if (kind == 4 && setupMode) {
            const qreal total = 5 * kDiceSideBtnW + 4 * kDiceSideBtnGap;
            const qreal x0 = (kWidgetCardW - total) / 2.0;
            for (int j = 0; j < 5; ++j) {
                if (QRectF(x0 + j * (kDiceSideBtnW + kDiceSideBtnGap), kDiceSideBtnY,
                           kDiceSideBtnW, kDiceSideBtnH)
                        .adjusted(-tol, -tol, tol, tol)
                        .contains(base))
                    return 400 + j;
            }
            if (QRectF(kDiceCountRowX - tol, kDiceCountBtnY - tol, kDiceCountBtnW + 2 * tol,
                       kDiceCountBtnH + 2 * tol).contains(base))
                return 410;  // 数量 −
            if (QRectF(kDiceCountRowX + kDiceCountBtnW + kDiceCountNumW - tol,
                       kDiceCountBtnY - tol, kDiceCountBtnW + 2 * tol,
                       kDiceCountBtnH + 2 * tol).contains(base))
                return 411;  // 数量 +
            if (QRectF((kWidgetCardW - kDiceOkW) / 2.0 - tol, kDiceOkY - tol,
                       kDiceOkW + 2 * tol, kDiceOkH + 2 * tol).contains(base))
                return 412;  // 确定
        }
        if (kind == 5) {
            if (QRectF(kWheelPanelX - tol, kWheelStartY - tol, kWheelPanelW + 2 * tol,
                       kWheelStartH + 2 * tol).contains(base))
                return 500;  // 开始
            for (int b = 0; b < 3; ++b) {
                const QRectF r(kWheelPanelX + b * (kWheelRowBtnW + kWheelRowBtnGap),
                               kWheelRowBtnY, kWheelRowBtnW, kWheelRowBtnH);
                if (r.adjusted(-tol, -tol, tol, tol).contains(base))
                    return 501 + b;  // 编辑 / 去重 / 重置
            }
            return -1;
        }
        if (kind == 6) {
            if (QRectF(kNameEditX - tol, kNameEditY - tol, kNameEditW + 2 * tol,
                       kNameEditH + 2 * tol).contains(base))
                return 601;  // 编辑名单
            if (QRectF(kNameDedupX - tol, kNameCountY - tol, kNameDedupW + 2 * tol,
                       kNameCountBtnH + 2 * tol).contains(base))
                return 602;  // 去重开关
            if (QRectF(kNameCountMinusX - tol, kNameCountY - tol, kNameCountBtnW + 2 * tol,
                       kNameCountBtnH + 2 * tol).contains(base))
                return 603;  // 人数 −
            if (QRectF(kNameCountPlusX - tol, kNameCountY - tol, kNameCountBtnW + 2 * tol,
                       kNameCountBtnH + 2 * tol).contains(base))
                return 604;  // 人数 +
            return -1;
        }
        return -1;
    }

    // 设置态滚轮列命中（仅计时器 kind=1）：0=时 / 1=分 / 2=秒 / -1=无
    int wheelColumnAt(const QPointF& local) const {
        if (kind != 1 || !setupMode)
            return -1;
        const QPointF base = toBase(local);
        if (base.y() < kWidgetWheelTop || base.y() > kWidgetWheelBottom)
            return -1;
        for (int col = 0; col < 3; ++col) {
            if (qAbs(base.x() - kWidgetWheelColX[col]) <= kWidgetWheelColW / 2.0)
                return col;
        }
        return -1;
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::TextAntialiasing, true);
        // 统一到基础坐标系绘制（平移到卡片左上角 + 等比缩放；阴影/圆角随缩放）
        const qreal s = scaleFactor();
        p->translate(cardRect.topLeft());
        p->scale(s, s);
        // 卡片底
        p->setPen(Qt::NoPen);
        p->setBrush(kWidgetCardColor);
        p->drawRoundedRect(QRectF(0, 0, kWidgetCardW, kWidgetCardH),
                           kWidgetCardRadius, kWidgetCardRadius);
        if (kind == 2) {
            paintCalculator(p);
        } else if (kind == 3) {
            paintAbacus(p);
        } else if (kind == 4) {
            paintDice(p);
        } else if (kind == 5) {
            paintWheel(p);
        } else if (kind == 6) {
            paintNamePicker(p);
        } else if (setupMode) {
            paintWheelSetup(p);
        } else {
            paintCountdown(p);
        }
        // 底部圆钮：秒表/计时器恒显示；骰子仅结果态显示（设置/滚动态无）；点名器恒显示
        if (kind <= 1 || (kind == 4 && !setupMode && !diceRolling) || kind == 6) {
            // 左：重置（灰钮 + 白环形箭头；骰子结果态 = 回设置态）
            const QPointF resetC(kWidgetBtnLX, kWidgetBtnY);
            p->setPen(Qt::NoPen);
            p->setBrush(kWidgetBtnGray);
            p->drawEllipse(resetC, kWidgetBtnRadius, kWidgetBtnRadius);
            paintResetGlyph(p, resetC);
            // 右：开始/暂停（绿 ▶ / 红 ∥；骰子结果态 = 再掷；点名器滚动中灰显禁用）
            const QPointF runC(kWidgetBtnRX, kWidgetBtnY);
            if (kind == 6)
                p->setBrush(nameRolling ? QColor(0x48, 0x48, 0x4A) : kWidgetBtnGreen);
            else
                p->setBrush(running ? kWidgetBtnRed : kWidgetBtnGreen);
            p->drawEllipse(runC, kWidgetBtnRadius, kWidgetBtnRadius);
            paintRunGlyph(p, runC, kind == 6 ? false : running);
        }
        p->restore();
    }

private:
    // 倒计时显示态：大号数字（等宽；计时器归零变橙）
    void paintCountdown(QPainter* p) {
        p->setFont(widgetDigitsFont());
        p->setPen(finished ? kWidgetFinished : kWidgetDigits);
        p->drawText(QRectF(16, 14, kWidgetCardW - 32, 62),
                    Qt::AlignHCenter | Qt::AlignVCenter, displayText);
    }

    // 设置态：三列（时/分/秒）滚轮（上/中/下三行；中央行橙色高亮大号，两端渐小
    // 渐暗；回卷显示）+ 冒号分隔
    void paintWheelSetup(QPainter* p) {
        const int values[3] = { wheelH, wheelM, wheelS };
        const int spans[3] = { 24, 60, 60 };  // 时 0~23 / 分 0~59 / 秒 0~59
        for (int col = 0; col < 3; ++col) {
            for (int row = 0; row < 3; ++row) {
                int v = values[col] + (row - 1);
                v = ((v % spans[col]) + spans[col]) % spans[col];  // 回卷
                const bool center = (row == 1);
                QFont f = widgetDigitsFont();
                f.setPixelSize(center ? 30 : 13);
                p->setFont(f);
                p->setPen(center ? QColor(0xFF, 0xB0, 0x60) : QColor(0x8A, 0x8A, 0x8E));
                p->drawText(QRectF(kWidgetWheelColX[col] - kWidgetWheelColW / 2.0,
                                   kWidgetWheelRowY[row] - 14.0,
                                   kWidgetWheelColW, 28.0),
                            Qt::AlignCenter,
                            QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')));
            }
        }
        // 冒号（时/分、分/秒之间，中行）
        QFont cf = widgetDigitsFont();
        cf.setPixelSize(26);
        p->setFont(cf);
        p->setPen(QColor(0xFF, 0xB0, 0x60));
        for (int i = 0; i < 2; ++i) {
            const qreal cx = (kWidgetWheelColX[i] + kWidgetWheelColX[i + 1]) / 2.0;
            p->drawText(QRectF(cx - 10.0, kWidgetWheelRowY[1] - 16.0, 20.0, 32.0),
                        Qt::AlignCenter, QStringLiteral(":"));
        }
    }

    // 计算器（kind=2）：显示屏（右对齐；不消费事件 = 拖动把手）+ 19 键键盘
    void paintCalculator(QPainter* p) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x2A, 0x2A, 0x2C));
        p->drawRoundedRect(QRectF(kCalcScreenX, kCalcScreenY, kCalcScreenW, kCalcScreenH), 8, 8);
        QFont sf = widgetDigitsFont();
        sf.setPixelSize(22);
        p->setFont(sf);
        p->setPen(calcError ? kWidgetBtnRed : kWidgetDigits);
        p->drawText(QRectF(kCalcScreenX + 10, kCalcScreenY, kCalcScreenW - 20, kCalcScreenH),
                    Qt::AlignRight | Qt::AlignVCenter, calcDisplay);
        QFont kf = widgetDigitsFont();
        kf.setPixelSize(11);
        for (int i = 0; i < 19; ++i) {
            const QRectF r = calcKeyRect(i);
            QColor c = kCalcNumKey;
            if (i == 2 || i == 3 || i == 7 || i == 11 || i == 15)
                c = kCalcOpKey;  // ÷ × − + =
            else if (i == 0 || i == 1 || i == 16)
                c = kCalcFnKey;  // C ⌫ ±
            p->setPen(Qt::NoPen);
            p->setBrush(c);
            p->drawRoundedRect(r, 6, 6);
            p->setFont(kf);
            p->setPen(Qt::white);
            p->drawText(r, Qt::AlignCenter, calcKeyLabel(i));
        }
    }

    // 算盘（kind=3）：木框底板 + 9 档柱 + 横梁 + 珠（上 1 下 4）+ 清盘钮
    void paintAbacus(QPainter* p) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x8A, 0x62, 0x38));
        p->drawRoundedRect(QRectF(8, 4, kWidgetCardW - 16, kWidgetCardH - 8), 12, 12);
        p->setBrush(QColor(0xEA, 0xDC, 0xB8));
        p->drawRoundedRect(QRectF(13, 9, kWidgetCardW - 26, kWidgetCardH - 18), 9, 9);
        for (int col = 0; col < kAbacusCols; ++col) {
            const qreal cx = kAbacusColX0 + col * kAbacusColDX;
            p->setPen(QPen(kWidgetColColor, 2));
            p->drawLine(QPointF(cx, kAbacusTop), QPointF(cx, kAbacusBottom));
        }
        p->setPen(Qt::NoPen);
        p->setBrush(kWidgetColColor);
        p->drawRect(QRectF(16, kAbacusBeamY - 4, kWidgetCardW - 32, 8));
        for (int col = 0; col < kAbacusCols; ++col) {
            const qreal cx = kAbacusColX0 + col * kAbacusColDX;
            drawBead(p, cx, abacusHigh[col] > 0 ? kAbacusHighDownY : kAbacusHighUpY);
            for (int b = 0; b < 4; ++b)
                drawBead(p, cx, abacusLowBeadY(abacusLow[col], b));
        }
        p->setBrush(kWidgetBtnRed);
        p->drawEllipse(QPointF(kAbacusClearX, kAbacusClearY), kAbacusClearR, kAbacusClearR);
        paintResetGlyph(p, QPointF(kAbacusClearX, kAbacusClearY), 6.5);
    }

    void drawBead(QPainter* p, qreal cx, qreal cy) {
        p->setPen(Qt::NoPen);
        p->setBrush(kWidgetBeadColor);
        p->drawEllipse(QPointF(cx, cy), kAbacusBeadW / 2.0, kAbacusBeadH / 2.0);
    }

    // 骰子（kind=4）：设置态参数选择 / 滚动与结果态骰子网格 + 合计
    void paintDice(QPainter* p) {
        if (setupMode) {
            paintDiceSetup(p);
            return;
        }
        const int n = qBound(1, diceCount, 10);
        const int cols = (n <= 5) ? n : (n + 1) / 2;  // >5 颗：2 行，列 = ceil(n/2)
        const int rows = (n <= 5) ? 1 : 2;
        const qreal cellW = kDiceGridW / cols;
        const qreal cellH = kDiceGridH / rows;
        const qreal side = qBound<qreal>(20.0, qMin(cellW, cellH) - 6.0, 64.0);
        for (int idx = 0; idx < n; ++idx) {
            const qreal cx = kDiceGridX + ((idx % cols) + 0.5) * cellW;
            const qreal cy = kDiceGridY + ((idx / cols) + 0.5) * cellH;
            paintDie(p, cx, cy, side, diceValues[idx], diceRolling ? diceJitter[idx] : 0);
        }
        if (!diceRolling) {
            int total = 0;
            for (int i = 0; i < n; ++i)
                total += diceValues[i];
            QFont f(QStringLiteral("Microsoft YaHei UI"));
            f.setPixelSize(13);
            p->setFont(f);
            p->setPen(QColor(0xC8, 0xC8, 0xCC));
            p->drawText(QRectF(kWidgetBtnLX + kWidgetBtnRadius, 104,
                               kWidgetBtnRX - kWidgetBtnLX - 2 * kWidgetBtnRadius, 24),
                        Qt::AlignCenter, QStringLiteral("合计 %1").arg(total));
        }
    }

    // 单颗骰子：白圆角块 + 面值（d6 画 1~6 点阵，其余面数画数字）+ 抖动旋转
    void paintDie(QPainter* p, qreal cx, qreal cy, qreal side, int value, int jitterDeg) {
        p->save();
        p->translate(cx, cy);
        if (jitterDeg != 0)
            p->rotate(jitterDeg);
        p->setPen(Qt::NoPen);
        p->setBrush(kDiceFaceColor);
        p->drawRoundedRect(QRectF(-side / 2, -side / 2, side, side), side * 0.18, side * 0.18);
        const int v = qBound(1, value <= 0 ? 1 : value, diceSides);
        if (diceSides == 6) {
            // 3x3 点阵位（索引：0 左上 ... 8 右下；值 0 占位不用）
            static const bool pipOn[7][9] = {
                { false, false, false, false, false, false, false, false, false },
                { false, false, false, false, true, false, false, false, false },
                { true, false, false, false, false, false, false, false, true },
                { true, false, false, false, true, false, false, false, true },
                { true, false, true, false, false, false, true, false, true },
                { true, false, true, false, true, false, true, false, true },
                { true, false, true, true, false, true, true, false, true },
            };
            p->setBrush(QColor(0x2A, 0x2A, 0x2C));
            const qreal off = side * 0.24;
            const qreal pr = qMax<qreal>(1.2, side * 0.075);
            for (int i = 0; i < 9; ++i) {
                if (!pipOn[v][i])
                    continue;
                p->drawEllipse(QPointF(((i % 3) - 1) * off, ((i / 3) - 1) * off), pr, pr);
            }
        } else {
            QFont f = widgetDigitsFont();
            f.setPixelSize(qMax(8, qRound(side * 0.42)));
            p->setFont(f);
            p->setPen(QColor(0x2A, 0x2A, 0x2C));
            p->drawText(QRectF(-side / 2, -side / 2, side, side), Qt::AlignCenter,
                        QString::number(v));
        }
        p->restore();
    }

    // 骰子设置态：面数 5 按钮（橙色高亮选中）+ 数量 − [n] +（1~10）+ 绿色确定横条
    void paintDiceSetup(QPainter* p) {
        QFont f(QStringLiteral("Microsoft YaHei UI"));
        const qreal total = 5 * kDiceSideBtnW + 4 * kDiceSideBtnGap;
        const qreal x0 = (kWidgetCardW - total) / 2.0;
        f.setPixelSize(13);
        for (int j = 0; j < 5; ++j) {
            const QRectF r(x0 + j * (kDiceSideBtnW + kDiceSideBtnGap), kDiceSideBtnY,
                           kDiceSideBtnW, kDiceSideBtnH);
            p->setPen(Qt::NoPen);
            p->setBrush(diceSides == kDiceSidesOption[j] ? kCalcOpKey : QColor(0x48, 0x48, 0x4A));
            p->drawRoundedRect(r, 7, 7);
            p->setFont(f);
            p->setPen(Qt::white);
            p->drawText(r, Qt::AlignCenter, QString::number(kDiceSidesOption[j]));
        }
        const QRectF minusR(kDiceCountRowX, kDiceCountBtnY, kDiceCountBtnW, kDiceCountBtnH);
        const QRectF plusR(kDiceCountRowX + kDiceCountBtnW + kDiceCountNumW, kDiceCountBtnY,
                           kDiceCountBtnW, kDiceCountBtnH);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x48, 0x48, 0x4A));
        p->drawRoundedRect(minusR, 7, 7);
        p->drawRoundedRect(plusR, 7, 7);
        f.setPixelSize(16);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(minusR, Qt::AlignCenter, QStringLiteral("−"));
        p->drawText(plusR, Qt::AlignCenter, QStringLiteral("+"));
        QFont nf = widgetDigitsFont();
        nf.setPixelSize(20);
        p->setFont(nf);
        p->setPen(kWidgetDigits);
        p->drawText(QRectF(kDiceCountRowX + kDiceCountBtnW, kDiceCountBtnY, kDiceCountNumW,
                           kDiceCountBtnH),
                    Qt::AlignCenter, QString::number(qBound(1, diceCount, 10)));
        const QRectF okR((kWidgetCardW - kDiceOkW) / 2.0, kDiceOkY, kDiceOkW, kDiceOkH);
        p->setPen(Qt::NoPen);
        p->setBrush(kWidgetBtnGreen);
        p->drawRoundedRect(okR, 10, 10);
        f.setPixelSize(17);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(okR, Qt::AlignCenter, QStringLiteral("确定"));
    }

    // ---------- 大转盘（kind=5）：圆形转盘（旋转坐标系）+ 顶部固定指针 + 右侧面板 ----------

    // 扇区文字：沿半径排布（midDeg = 扇区中线，从顶部顺时针）；左半区（>180°）追加
    // 旋转 180° 并把文字绘入负半轴区域保持正立；超长右省略
    void drawWheelLabel(QPainter* p, qreal midDeg, const QString& text, bool dim) {
        QFont f(QStringLiteral("Microsoft YaHei UI"));
        f.setPixelSize(10);
        const QString shown = QFontMetricsF(f).elidedText(text, Qt::ElideRight, kWheelR - 19.0);
        p->save();
        p->rotate(midDeg - 90.0);  // 扇区中线转到竖向
        p->setFont(f);
        p->setPen(dim ? QColor(255, 255, 255, 120) : QColor(255, 255, 255, 235));
        if (midDeg > 180.0) {
            p->rotate(180.0);  // 左半区翻转：文字从外缘向圆心读，保持正立
            p->drawText(QRectF(-kWheelR + 5.0, -8.0, kWheelR - 19.0, 16.0),
                        Qt::AlignLeft | Qt::AlignVCenter, shown);
        } else {
            p->drawText(QRectF(14.0, -8.0, kWheelR - 19.0, 16.0),
                        Qt::AlignLeft | Qt::AlignVCenter, shown);
        }
        p->restore();
    }

    // 转盘：扇区按权重比例（第 i 扇区内起于 startDeg[i]，扫过 sweepDeg[i]）；
    // 去重已抽扇区叠暗；右侧结果区（顶）/ 开始钮（中）/ 编辑·去重·重置（底）
    void paintWheel(QPainter* p) {
        const QPointF c(kWheelCx, kWheelCy);
        if (wheelNames.isEmpty()) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x2A, 0x2A, 0x2C));
            p->drawEllipse(c, kWheelR, kWheelR);
            QFont f(QStringLiteral("Microsoft YaHei UI"));
            f.setPixelSize(11);
            p->setFont(f);
            p->setPen(kWheelIdleGray);
            p->drawText(QRectF(c.x() - kWheelR, c.y() - 10.0, 2 * kWheelR, 20.0),
                        Qt::AlignCenter, QStringLiteral("点「编辑」添加选项"));
        } else {
            QVector<double> startDeg, sweepDeg;
            wheelSectorAngles(wheelWeights, startDeg, sweepDeg);
            p->save();
            p->translate(c);
            p->rotate(wheelAngle);
            const QRectF disc(-kWheelR, -kWheelR, 2 * kWheelR, 2 * kWheelR);
            for (int i = 0; i < wheelNames.size(); ++i) {
                const bool dimmed = wheelDedup && wheelDrawn.contains(wheelNames[i]);
                if (wheelNames.size() == 1) {
                    // 单项：整圆（规避 arcTo 360° 特例）
                    p->setPen(Qt::NoPen);
                    p->setBrush(kWheelPalette[0]);
                    p->drawEllipse(QPointF(0, 0), kWheelR, kWheelR);
                    if (dimmed) {
                        p->setBrush(QColor(0, 0, 0, 130));
                        p->drawEllipse(QPointF(0, 0), kWheelR, kWheelR);
                    }
                    drawWheelLabel(p, 90.0, wheelNames[0], dimmed);
                } else {
                    QPainterPath path;
                    path.moveTo(0, 0);
                    // Qt 弧角：0°=3 点钟、正角逆时针；本盘角度以顶部（90°）起顺时针，
                    // 故起始角 = 90 - startDeg，扫过为负（顺时针）
                    path.arcTo(disc, 90.0 - startDeg[i], -sweepDeg[i]);
                    path.closeSubpath();
                    p->setPen(QPen(QColor(28, 28, 30, 210), 1.4));
                    p->setBrush(kWheelPalette[i % 6]);
                    p->drawPath(path);
                    if (dimmed) {
                        p->setPen(Qt::NoPen);
                        p->setBrush(QColor(0, 0, 0, 130));
                        p->drawPath(path);
                    }
                    drawWheelLabel(p, startDeg[i] + sweepDeg[i] / 2.0, wheelNames[i], dimmed);
                }
            }
            p->restore();
        }
        // 中心圆帽（盖住扇区汇聚角）
        p->setPen(Qt::NoPen);
        p->setBrush(kWidgetCardColor);
        p->drawEllipse(c, 12.0, 12.0);
        // 顶部固定指针（不随旋转）：红色倒三角
        p->setBrush(kWidgetBtnRed);
        QPolygonF tri;
        tri << QPointF(kWheelCx - 8.0, kWheelCy - kWheelR - 6.0)
            << QPointF(kWheelCx + 8.0, kWheelCy - kWheelR - 6.0)
            << QPointF(kWheelCx, kWheelCy - kWheelR + 8.0);
        p->drawPolygon(tri);
        // 右侧面板：结果区 / 开始钮 / 三小按钮
        QFont f(QStringLiteral("Microsoft YaHei UI"));
        const QRectF resultR(kWheelPanelX, kWheelResultY, kWheelPanelW, kWheelResultH);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x24, 0x24, 0x26));
        p->drawRoundedRect(resultR, 9, 9);
        if (wheelSpinning) {
            f.setPixelSize(13);
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            p->setPen(kWheelIdleGray);
            p->drawText(resultR, Qt::AlignCenter, QStringLiteral("转动中…"));
        } else if (!wheelHint.isEmpty()) {
            f.setPixelSize(11);
            p->setFont(f);
            p->setPen(kWheelIdleGray);
            p->drawText(resultR.adjusted(6, 0, -6, 0), Qt::AlignCenter, wheelHint);
        } else if (!wheelResult.isEmpty()) {
            f.setPixelSize(13);
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            p->setPen(kWheelResultGold);
            p->drawText(resultR.adjusted(6, 0, -6, 0), Qt::AlignCenter,
                        QFontMetricsF(f).elidedText(wheelResult, Qt::ElideRight,
                                                    kWheelPanelW - 12.0));
        } else {
            f.setPixelSize(11);
            p->setFont(f);
            p->setPen(kWheelIdleGray);
            p->drawText(resultR, Qt::AlignCenter, QStringLiteral("结果"));
        }
        const QRectF startR(kWheelPanelX, kWheelStartY, kWheelPanelW, kWheelStartH);
        p->setPen(Qt::NoPen);
        p->setBrush(wheelSpinning ? QColor(0x48, 0x48, 0x4A) : kWidgetBtnGreen);
        p->drawRoundedRect(startR, 10, 10);
        f.setPixelSize(16);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(startR, Qt::AlignCenter, QStringLiteral("开始"));
        static const char* wheelRowLabels[3] = { "编辑", "去重", "重置" };
        f.setPixelSize(11);
        f.setWeight(QFont::Normal);
        for (int b = 0; b < 3; ++b) {
            const QRectF r(kWheelPanelX + b * (kWheelRowBtnW + kWheelRowBtnGap), kWheelRowBtnY,
                           kWheelRowBtnW, kWheelRowBtnH);
            p->setPen(Qt::NoPen);
            p->setBrush((b == 1 && wheelDedup) ? kCalcOpKey : QColor(0x48, 0x48, 0x4A));
            p->drawRoundedRect(r, 7, 7);
            p->setFont(f);
            p->setPen(Qt::white);
            p->drawText(r, Qt::AlignCenter, QString::fromUtf8(wheelRowLabels[b]));
        }
    }

    // ---------- 点名器（kind=6）：顶行工具 + 名字显示区 + 底部圆钮 ----------

    // 顶行：编辑名单 / 人数 − n + / 状态（已点 m/共 n）/ 去重开关；名字区：
    // 滚动时各格高速切换，定格后金色放大（淡金底条；多人网格，N≤3 单行，4~5 双行）
    void paintNamePicker(QPainter* p) {
        QFont f(QStringLiteral("Microsoft YaHei UI"));
        const QRectF editR(kNameEditX, kNameEditY, kNameEditW, kNameEditH);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x3A, 0x3A, 0x3C));
        p->drawRoundedRect(editR, 7, 7);
        f.setPixelSize(12);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(editR, Qt::AlignCenter, QStringLiteral("编辑"));
        const QRectF minusR(kNameCountMinusX, kNameCountY, kNameCountBtnW, kNameCountBtnH);
        const QRectF plusR(kNameCountPlusX, kNameCountY, kNameCountBtnW, kNameCountBtnH);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x48, 0x48, 0x4A));
        p->drawRoundedRect(minusR, 7, 7);
        p->drawRoundedRect(plusR, 7, 7);
        f.setPixelSize(15);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(minusR, Qt::AlignCenter, QStringLiteral("−"));
        p->drawText(plusR, Qt::AlignCenter, QStringLiteral("+"));
        QFont nf = widgetDigitsFont();
        nf.setPixelSize(17);
        p->setFont(nf);
        p->setPen(kWidgetDigits);
        p->drawText(QRectF(kNameCountNumX, kNameCountY, kNameCountNumW, kNameCountBtnH),
                    Qt::AlignCenter, QString::number(qBound(1, namePickCount, 5)));
        f.setPixelSize(10);
        p->setFont(f);
        p->setPen(kWheelIdleGray);
        p->drawText(QRectF(kNameStatusX, kNameCountY, kNameStatusW, kNameCountBtnH), Qt::AlignCenter,
                    QStringLiteral("已点 %1/共 %2").arg(nameDrawn.size()).arg(nameNames.size()));
        const QRectF dedupR(kNameDedupX, kNameCountY, kNameDedupW, kNameCountBtnH);
        p->setPen(Qt::NoPen);
        p->setBrush(nameDedup ? kCalcOpKey : QColor(0x48, 0x48, 0x4A));
        p->drawRoundedRect(dedupR, 7, 7);
        f.setPixelSize(11);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(dedupR, Qt::AlignCenter, QStringLiteral("去重"));
        // 名字显示区（圆角深底）
        const QRectF boardR(kNameBoardX, kNameBoardY, kNameBoardW, kNameBoardH);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x24, 0x24, 0x26));
        p->drawRoundedRect(boardR, 10, 10);
        f.setPixelSize(11);
        p->setFont(f);
        if (nameNames.isEmpty()) {
            p->setPen(kWheelIdleGray);
            p->drawText(boardR, Qt::AlignCenter, QStringLiteral("名单为空，点编辑添加"));
            return;
        }
        if (!nameRolling && !nameHint.isEmpty()) {
            p->setPen(kWheelIdleGray);
            p->drawText(boardR, Qt::AlignCenter, nameHint);
            return;
        }
        if (nameRolling || !nameResults.isEmpty()) {
            // 网格：N≤3 单行 N 列；4 = 2x2；5 = 两行（3+2，第二行居中）
            const QStringList texts = nameRolling ? nameRollTexts : nameResults;
            const int n = qBound(1, texts.size(), 5);
            int rows = 1, cols = n;
            if (n == 4) { rows = 2; cols = 2; }
            else if (n >= 5) { rows = 2; cols = 3; }
            const qreal cellW = kNameBoardW / cols;
            const qreal cellH = kNameBoardH / rows;
            QFont cf(QStringLiteral("Microsoft YaHei UI"));
            cf.setPixelSize(n == 1 ? (nameRolling ? 30 : 34) : (n <= 3 ? 21 : 16));
            cf.setWeight(nameRolling ? QFont::Normal : QFont::DemiBold);
            const QFontMetricsF fm(cf);
            for (int i = 0; i < n && i < texts.size(); ++i) {
                const int row = (n == 5) ? (i / 3) : (i / cols);
                const int col = (n == 5) ? (i % 3) : (i % cols);
                qreal x = kNameBoardX + col * cellW;
                const qreal y = kNameBoardY + row * cellH;
                if (n == 5 && row == 1)
                    x += cellW / 2.0;  // 第二行 2 格居中
                const QString shown = fm.elidedText(texts[i], Qt::ElideRight, cellW - 10.0);
                if (!nameRolling) {
                    // 淡金底条（定格高亮）
                    const qreal barW = qMin(cellW - 8.0, fm.horizontalAdvance(shown) + 18.0);
                    const qreal barH = (n == 1) ? 40.0 : qMin(cellH - 4.0, 32.0);
                    const QRectF bar(x + (cellW - barW) / 2.0, y + (cellH - barH) / 2.0,
                                     barW, barH);
                    p->setPen(Qt::NoPen);
                    p->setBrush(QColor(0xFF, 0xD3, 0x5C, 34));
                    p->drawRoundedRect(bar, 8, 8);
                }
                p->setFont(cf);
                p->setPen(nameRolling ? QColor(0xC8, 0xC8, 0xCC) : kWheelResultGold);
                p->drawText(QRectF(x, y, cellW, cellH), Qt::AlignCenter, shown);
            }
            return;
        }
        p->setPen(kWheelIdleGray);
        p->drawText(boardR, Qt::AlignCenter, QStringLiteral("点开始点名"));
    }
};

// 表格网格：外框 + 行/列等分线（未旋转坐标；旋转由调用方施加）
QPainterPath tableGridPath(const whiteboard::Rect& b, int rows, int cols) {
    QPainterPath path;
    if (rows < 1 || cols < 1 || b.width <= 0 || b.height <= 0)
        return path;
    path.addRect(b.x, b.y, b.width, b.height);
    for (int i = 1; i < rows; ++i) {
        const qreal y = b.y + b.height * i / static_cast<qreal>(rows);
        path.moveTo(b.x, y);
        path.lineTo(b.x + b.width, y);
    }
    for (int j = 1; j < cols; ++j) {
        const qreal x = b.x + b.width * j / static_cast<qreal>(cols);
        path.moveTo(x, b.y);
        path.lineTo(x, b.y + b.height);
    }
    return path;
}

// 表格网格（按布局）：外框 + 按列宽/行高的行列分隔线（未旋转坐标；旋转由调用方施加）
QPainterPath tableGridPathFromLayout(const whiteboard::Point& origin,
                                     const whiteboard::TableElement::Layout& layout) {
    QPainterPath path;
    if (layout.totalW <= 0 || layout.totalH <= 0)
        return path;
    path.addRect(origin.x, origin.y, layout.totalW, layout.totalH);
    double x = origin.x;
    for (size_t j = 0; j + 1 < layout.colW.size(); ++j) {
        x += layout.colW[j];
        path.moveTo(x, origin.y);
        path.lineTo(x, origin.y + layout.totalH);
    }
    double y = origin.y;
    for (size_t i = 0; i + 1 < layout.rowH.size(); ++i) {
        y += layout.rowH[i];
        path.moveTo(origin.x, y);
        path.lineTo(origin.x + layout.totalW, y);
    }
    return path;
}

// 表格格帧：格局部坐标 → 页面坐标（平移格原点后绕布局中心旋转 rotation）。
// 格内子元素图元变换 = 自身局部变换 * 本帧；与数据层入格换算（PageToCellLocal）互为逆映射。
QTransform tableCellFrame(const whiteboard::TableElement& t,
                          const whiteboard::TableElement::Layout& layout, int cellIndex) {
    double ox = 0.0;
    double oy = 0.0;
    layout.CellOrigin(cellIndex, ox, oy);
    const double halfW = layout.totalW / 2.0;
    const double halfH = layout.totalH / 2.0;
    return QTransform()
            .translate(t.origin.x + halfW, t.origin.y + halfH)
            .rotate(t.rotation)
            .translate(ox - halfW, oy - halfH);
}

// 思维导图：按值化布局结果生成绘制路径（连线 + 圆角节点框 + 折叠钮 + 聚焦 +/× 钮）。
// 不依赖数据层树指针（数据线程可安全改树）；withButtons=false 用于缩略图。
QPainterPath mindMapPathFromLayout(const whiteboard::MindLayout& layout,
                                   const QString& focusNodeId, bool withButtons) {
    namespace mld = whiteboard::mind_layout_detail;
    QPainterPath path;
    for (const auto& link : layout.links) {
        path.moveTo(link.a.x, link.a.y);
        path.lineTo(link.b.x, link.b.y);
    }
    for (const auto& box : layout.boxes) {
        const qreal radius = box.depth == 0 ? 12.0 : 8.0;
        path.addRoundedRect(QRectF(box.frame.x, box.frame.y, box.frame.width, box.frame.height),
                            radius, radius);
    }
    if (!withButtons)
        return path;
    // 折叠钮：圆 + 符号（展开画 "-" 横线 / 折叠画 "+" 十字）
    const qreal r = mld::kBtnRadius;
    const qreal s = 4.0;  // 符号半长
    for (const auto& box : layout.boxes) {
        if (!box.hasFold)
            continue;
        const qreal cx = box.foldBtn.x;
        const qreal cy = box.foldBtn.y;
        path.addEllipse(QPointF(cx, cy), r, r);
        path.moveTo(cx - s, cy);
        path.lineTo(cx + s, cy);
        if (box.collapsed) {
            path.moveTo(cx, cy - s);
            path.lineTo(cx, cy + s);
        }
    }
    // 聚焦节点：外扩 3px 高亮框 + "+"钮（顶边上方）+ "×"钮（底边下方，非根才画）
    if (!focusNodeId.isEmpty()) {
        for (const auto& box : layout.boxes) {
            if (QString::fromStdString(box.nodeId) != focusNodeId)
                continue;
            const QRectF hl(box.frame.x - 3, box.frame.y - 3,
                            box.frame.width + 6, box.frame.height + 6);
            path.addRoundedRect(hl, 10, 10);
            const whiteboard::Point add = whiteboard::MindAddBtnPos(box);
            path.addEllipse(QPointF(add.x, add.y), r, r);
            path.moveTo(add.x - s, add.y);
            path.lineTo(add.x + s, add.y);
            path.moveTo(add.x, add.y - s);
            path.lineTo(add.x, add.y + s);
            if (box.depth > 0) {
                const whiteboard::Point del = whiteboard::MindDelBtnPos(box);
                path.addEllipse(QPointF(del.x, del.y), r, r);
                path.moveTo(del.x - s, del.y - s);
                path.lineTo(del.x + s, del.y + s);
                path.moveTo(del.x - s, del.y + s);
                path.lineTo(del.x + s, del.y - s);
            }
            break;
        }
    }
    return path;
}

// 缩略图：递归收集元素视觉包围盒（表格含旋转后四角 AABB 与全部子元素）
void collectThumbBounds(const whiteboard::Element& e, QRectF& least, bool& hasContent) {
    if (auto* stroke = dynamic_cast<const whiteboard::Stroke*>(&e)) {
        if (stroke->points.empty())
            return;  // 空点集笔画包围盒为毒值，不参与并集
        const whiteboard::Rect br = stroke->bounding.ToRect();
        const QRectF r(br.x, br.y, br.width, br.height);
        least = hasContent ? least.united(r) : r;
        hasContent = true;
        return;
    }
    if (auto* g = dynamic_cast<const whiteboard::GraphicElement*>(&e)) {
        if (g->subpaths.empty())
            return;
        const whiteboard::Rect br = g->bounding.ToRect();
        const QRectF r(br.x, br.y, br.width, br.height);
        least = hasContent ? least.united(r) : r;
        hasContent = true;
        return;
    }
    if (auto* t = dynamic_cast<const whiteboard::TableElement*>(&e)) {
        const whiteboard::TableElement::Layout layout = t->ComputeLayout();
        const QRectF r(t->origin.x, t->origin.y, layout.totalW, layout.totalH);
        QTransform tf;
        if (t->rotation != 0.0f) {
            const QPointF c = r.center();
            tf = QTransform().translate(c.x(), c.y()).rotate(t->rotation).translate(-c.x(), -c.y());
        }
        const QRectF vr = tf.isIdentity() ? r : tf.mapRect(r);
        least = hasContent ? least.united(vr) : vr;
        hasContent = true;
        // 子元素为格局部坐标：经 自身变换 × 格帧 × 表旋转 映射到页面后并入
        for (size_t ci = 0; ci < t->cells.size(); ++ci) {
            const QTransform frame =
                tableCellFrame(*t, layout, static_cast<int>(ci)) * tf;
            for (const auto& child : t->cells[ci]) {
                if (!child)
                    continue;
                QRectF cr;
                bool ok = false;
                if (auto* st = dynamic_cast<const whiteboard::Stroke*>(child.get())) {
                    if (!st->points.empty()) {
                        const whiteboard::Rect br = st->bounding.ToRect();
                        cr = frame.mapRect(QRectF(br.x, br.y, br.width, br.height));
                        ok = true;
                    }
                } else if (auto* g = dynamic_cast<const whiteboard::GraphicElement*>(child.get())) {
                    if (!g->subpaths.empty()) {
                        const whiteboard::Rect br = g->bounding.ToRect();
                        cr = frame.mapRect(QRectF(br.x, br.y, br.width, br.height));
                        ok = true;
                    }
                } else if (auto* tx = dynamic_cast<const whiteboard::TextElement*>(child.get())) {
                    QRectF lr(tx->bounds.x, tx->bounds.y, tx->bounds.width, tx->bounds.height);
                    if (lr.width() <= 0 || lr.height() <= 0) {
                        const QPainterPath p = textPath(*tx);  // bounds 未维护时按字形兜底
                        if (p.isEmpty())
                            continue;
                        lr = p.boundingRect();
                    }
                    QTransform childTf;
                    if (tx->rotation != 0.0f) {
                        childTf = QTransform().translate(tx->x, tx->y).rotate(tx->rotation)
                                      .translate(-tx->x, -tx->y);
                    }
                    cr = (childTf * frame).mapRect(lr);
                    ok = true;
                }
                if (ok) {
                    least = hasContent ? least.united(cr) : cr;
                    hasContent = true;
                }
            }
        }
        return;
    }
    if (auto* mm = dynamic_cast<const whiteboard::MindMapElement*>(&e)) {
        const whiteboard::MindLayout layout = whiteboard::ComputeMindMapLayout(*mm);
        if (layout.boxes.empty())
            return;
        const QRectF r(layout.bounds.x, layout.bounds.y, layout.bounds.width, layout.bounds.height);
        QTransform tf;
        if (mm->rotation != 0.0f) {
            tf = QTransform().translate(mm->root.x, mm->root.y).rotate(mm->rotation)
                     .translate(-mm->root.x, -mm->root.y);
        }
        const QRectF vr = tf.isIdentity() ? r : tf.mapRect(r);
        least = hasContent ? least.united(vr) : vr;
        hasContent = true;
        return;
    }
    if (auto* text = dynamic_cast<const whiteboard::TextElement*>(&e)) {
        const QPainterPath p = textPath(*text);
        if (p.isEmpty())
            return;  // 空文本不参与并集
        QRectF r = p.boundingRect();
        if (text->rotation != 0.0f) {
            const QTransform tf = QTransform().translate(text->x, text->y).rotate(text->rotation)
                                      .translate(-text->x, -text->y);
            r = tf.mapRect(r);
        }
        least = hasContent ? least.united(r) : r;
        hasContent = true;
        return;
    }
    if (auto* wdg = dynamic_cast<const whiteboard::WidgetElement*>(&e)) {
        const qreal s = qBound<qreal>(kWidgetMinScale, wdg->scale, kWidgetMaxScale);
        const QRectF r(wdg->x, wdg->y, kWidgetCardW * s, kWidgetCardH * s);
        least = hasContent ? least.united(r) : r;
        hasContent = true;
        return;
    }
}

// 缩略图：递归绘制元素（表格网格先旋转再绘制，子元素坐标已是视觉坐标不再变换）
void paintThumbElement(QPainter& p, const whiteboard::Element& e) {
    if (auto* stroke = dynamic_cast<const whiteboard::Stroke*>(&e)) {
        if (stroke->points.empty())
            return;
        p.setBrush(Qt::NoBrush);
        p.setPen(elementPen(stroke->color, stroke->width));
        p.drawPath(strokePath(*stroke));
        return;
    }
    if (auto* g = dynamic_cast<const whiteboard::GraphicElement*>(&e)) {
        if (g->subpaths.empty())
            return;
        p.setBrush(Qt::NoBrush);
        p.setPen(elementPen(g->color, g->width));
        p.drawPath(graphicPath(*g));
        return;
    }
    if (auto* t = dynamic_cast<const whiteboard::TableElement*>(&e)) {
        const whiteboard::TableElement::Layout layout = t->ComputeLayout();
        const QRectF r(t->origin.x, t->origin.y, layout.totalW, layout.totalH);
        QTransform tfTable;
        if (t->rotation != 0.0f) {
            const QPointF c = r.center();
            tfTable = QTransform().translate(c.x(), c.y()).rotate(t->rotation)
                          .translate(-c.x(), -c.y());
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(elementPen(t->color, t->width));
        const QPainterPath grid = tableGridPathFromLayout(t->origin, layout);
        p.drawPath(tfTable.isIdentity() ? grid : tfTable.map(grid));
        // 子元素为格局部坐标：手动映射到页面（自身变换 → 格帧 → 表旋转），
        // 与渲染路径同构
        for (size_t ci = 0; ci < t->cells.size(); ++ci) {
            const QTransform frame =
                tableCellFrame(*t, layout, static_cast<int>(ci)) * tfTable;
            for (const auto& child : t->cells[ci]) {
                if (!child)
                    continue;
                if (auto* st = dynamic_cast<const whiteboard::Stroke*>(child.get())) {
                    if (st->points.empty())
                        continue;
                    p.setBrush(Qt::NoBrush);
                    p.setPen(elementPen(st->color, st->width));
                    p.drawPath(frame.map(strokePath(*st)));
                } else if (auto* g = dynamic_cast<const whiteboard::GraphicElement*>(child.get())) {
                    if (g->subpaths.empty())
                        continue;
                    p.setBrush(Qt::NoBrush);
                    p.setPen(elementPen(g->color, g->width));
                    p.drawPath(frame.map(graphicPath(*g)));
                } else if (auto* tx = dynamic_cast<const whiteboard::TextElement*>(child.get())) {
                    const QPainterPath path = textPath(*tx);
                    if (path.isEmpty())
                        continue;
                    QTransform childTf;
                    if (tx->rotation != 0.0f) {
                        childTf = QTransform().translate(tx->x, tx->y).rotate(tx->rotation)
                                      .translate(-tx->x, -tx->y);
                    }
                    p.setPen(Qt::NoPen);
                    p.setBrush(colorToQColor(tx->color));
                    p.drawPath((childTf * frame).map(path));
                }
            }
        }
        return;
    }
    if (auto* mm = dynamic_cast<const whiteboard::MindMapElement*>(&e)) {
        const whiteboard::MindLayout layout = whiteboard::ComputeMindMapLayout(*mm);
        if (layout.boxes.empty())
            return;
        p.save();
        if (mm->rotation != 0.0f) {
            p.translate(mm->root.x, mm->root.y);
            p.rotate(mm->rotation);
            p.translate(-mm->root.x, -mm->root.y);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(elementPen(mm->color, mm->width));
        p.drawPath(mindMapPathFromLayout(layout, QString(), false));
        p.restore();
        return;
    }
    if (auto* text = dynamic_cast<const whiteboard::TextElement*>(&e)) {
        const QPainterPath path = textPath(*text);
        if (path.isEmpty())
            return;
        p.save();
        if (text->rotation != 0.0f) {
            p.translate(text->x, text->y);
            p.rotate(text->rotation);
            p.translate(-text->x, -text->y);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(colorToQColor(text->color));
        p.drawPath(path);
        p.restore();
        return;
    }
    if (auto* wdg = dynamic_cast<const whiteboard::WidgetElement*>(&e)) {
        // 小工具：深色圆角卡 + 按类型示意（0/1 静态短文本；2 计算器屏+键盘；
        // 3 算盘横梁+珠；4 两枚迷你骰子；5 圆盘+指针；6 金条+首名；均按 scale 缩放）
        const qreal s = qBound<qreal>(kWidgetMinScale, wdg->scale, kWidgetMaxScale);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(kWidgetCardColor);
        p.drawRoundedRect(QRectF(wdg->x, wdg->y, kWidgetCardW * s, kWidgetCardH * s),
                          kWidgetCardRadius * s, kWidgetCardRadius * s);
        const int wkind = wdg->kind;
        if (wkind == 2) {
            // 显示屏条 + 4x5 键盘格线
            p.setBrush(QColor(0x2A, 0x2A, 0x2C));
            p.drawRoundedRect(QRectF(wdg->x + kCalcScreenX * s, wdg->y + kCalcScreenY * s,
                                     kCalcScreenW * s, kCalcScreenH * s), 6 * s, 6 * s);
            p.setBrush(QColor(0x3A, 0x3A, 0x3C));
            for (int i = 0; i < 19; ++i) {
                const QRectF r = calcKeyRect(i);
                p.drawRoundedRect(QRectF(wdg->x + r.x() * s, wdg->y + r.y() * s,
                                         r.width() * s, r.height() * s), 3 * s, 3 * s);
            }
        } else if (wkind == 3) {
            // 横梁 + 珠点（9 档每档上 1 下 4，未拨位）
            p.setBrush(kWidgetColColor);
            p.drawRect(QRectF(wdg->x + 16 * s, wdg->y + (kAbacusBeamY - 4) * s,
                              (kWidgetCardW - 32) * s, 8 * s));
            p.setBrush(kWidgetBeadColor);
            for (int col = 0; col < kAbacusCols; ++col) {
                const qreal cx = wdg->x + (kAbacusColX0 + col * kAbacusColDX) * s;
                p.drawEllipse(QPointF(cx, wdg->y + kAbacusHighUpY * s),
                              kAbacusBeadW / 2 * s, kAbacusBeadH / 2 * s);
                for (int b = 0; b < 4; ++b)
                    p.drawEllipse(QPointF(cx, wdg->y + abacusLowBeadY(0, b) * s),
                                  kAbacusBeadW / 2 * s, kAbacusBeadH / 2 * s);
            }
        } else if (wkind == 4) {
            // 两枚迷你骰子
            p.setBrush(kDiceFaceColor);
            const qreal side = 30 * s;
            p.drawRoundedRect(QRectF(wdg->x + kWidgetCardW * s / 2 - side - 8 * s,
                                     wdg->y + kWidgetCardH * s / 2 - side / 2,
                                     side, side), side * 0.18, side * 0.18);
            p.drawRoundedRect(QRectF(wdg->x + kWidgetCardW * s / 2 + 8 * s,
                                     wdg->y + kWidgetCardH * s / 2 - side / 2,
                                     side, side), side * 0.18, side * 0.18);
        } else if (wkind == 5) {
            // 大转盘：圆盘 + 十字分界线 + 顶部指针
            const QPointF c(wdg->x + kWheelCx * s, wdg->y + kWheelCy * s);
            p.setBrush(QColor(0x3A, 0x3A, 0x3C));
            p.drawEllipse(c, kWheelR * s, kWheelR * s);
            p.setPen(QPen(QColor(0x8A, 0x8A, 0x8E), qMax(1.0, 1.2 * s)));
            p.drawLine(QPointF(c.x() - kWheelR * s, c.y()), QPointF(c.x() + kWheelR * s, c.y()));
            p.drawLine(QPointF(c.x(), c.y() - kWheelR * s), QPointF(c.x(), c.y() + kWheelR * s));
            p.setPen(Qt::NoPen);
            p.setBrush(kWidgetBtnRed);
            QPolygonF tri;
            tri << QPointF(c.x() - 8 * s, c.y() - kWheelR * s - 6 * s)
                << QPointF(c.x() + 8 * s, c.y() - kWheelR * s - 6 * s)
                << QPointF(c.x(), c.y() - kWheelR * s + 8 * s);
            p.drawPolygon(tri);
        } else if (wkind == 6) {
            // 点名器：淡金高亮圆角条 + 首个名字
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xFF, 0xD3, 0x5C, 40));
            p.drawRoundedRect(QRectF(wdg->x + (kWidgetCardW - 150) * s / 2.0,
                                     wdg->y + (kWidgetCardH / 2 - 24) * s,
                                     150 * s, 48 * s), 10 * s, 10 * s);
            QStringList names;
            QVector<double> weights;
            parseOptions(QString::fromStdString(wdg->options), names, weights);
            if (!names.isEmpty()) {
                QFont font(QStringLiteral("Microsoft YaHei UI"));
                font.setPixelSize(qMax(6, qRound(24 * s)));
                font.setWeight(QFont::DemiBold);
                p.setFont(font);
                p.setPen(kWheelResultGold);
                p.drawText(QRectF(wdg->x, wdg->y + kWidgetCardH * s / 2 - 24 * s,
                                  kWidgetCardW * s, 48 * s),
                           Qt::AlignCenter, names.first());
            }
        } else {
            QFont font = widgetDigitsFont();
            font.setPixelSize(qMax(6, qRound(22 * s)));
            p.setFont(font);
            p.setPen(kWidgetDigits);
            p.drawText(QRectF(wdg->x, wdg->y + 18 * s, kWidgetCardW * s, 44 * s),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       widgetInitialText(wkind, wdg->durationSec));
        }
        p.restore();
        return;
    }
}
}  // namespace

BoardView::BoardView(QWidget* parent) : QGraphicsView(parent) {
    setScene(&scene_);
    // 无限画布：超大矩形场景（近似无限），平移/绘制不受边界限制
    scene_.setSceneRect(QRectF(-kSceneHalfExtent, -kSceneHalfExtent,
                               kSceneHalfExtent * 2, kSceneHalfExtent * 2));
    scene_.setBackgroundBrush(kBoardBackground);

    setRenderHint(QPainter::Antialiasing, true);
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 小工具走时定时器（33ms）：仅在存在运行中的秒表/计时器时启动，全停自停
    widgetTick_ = new QTimer(this);
    widgetTick_->setInterval(33);
    connect(widgetTick_, &QTimer::timeout, this, &BoardView::updateWidgetTick);

    // 数据层回调：在数据线程触发，投递回 UI 线程（this 销毁后 Qt 自动丢弃）
    data_.SetCallbacks(
        [this](std::vector<std::string> removed,
               std::vector<std::shared_ptr<whiteboard::Element>> added,
               std::vector<whiteboard::EraserPlacement> placements) {
            onElementsChanged(std::move(removed), std::move(added), std::move(placements));
        },
        [this](uint64_t token, const std::string& id,
               const std::string& parentId, int cellIndex) {
            onStrokeCommitted(token, id, parentId, cellIndex);
        },
        [this]() {
            onCleared();
        },
        [this]() {
            onPageChanged();
        },
        [this](std::string strokeId, uint32_t color, int width,
               std::vector<whiteboard::Point> points) {
            onStrokePreview(std::move(strokeId), color, width, std::move(points));
        },
        [this](uint32_t tool, std::string sessionId,
               std::vector<whiteboard::Point> points,
               std::vector<std::string> elementIds) {
            onToolPreview(tool, std::move(sessionId), std::move(points),
                          std::move(elementIds));
        },
        [this]() {
            onSynced();
        });

    refreshPageIds();
    fitView();
}

BoardView::~BoardView() {
    // 先移除回调并等待数据线程在途任务完成，避免回调触碰已析构对象
    data_.RemoveCallbacks();
}

// 黑板背景图：拉伸铺满视口（参考 BoardSlideControl 的 ImageBrush 背景）；空 = 墨绿纯色
void BoardView::setBoardBackground(const QPixmap& pixmap) {
    boardBg_ = pixmap;
    viewport()->update();
}

// 背景固定于视口：重置变换后按视口矩形拉伸绘制，不随内容平移/缩放
void BoardView::drawBackground(QPainter* painter, const QRectF& rect) {
    if (boardBg_.isNull()) {
        painter->fillRect(rect, kBoardBackground);
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->resetTransform();
    painter->drawPixmap(viewport()->rect(), boardBg_);
    painter->restore();
}

void BoardView::setTool(Tool tool) {
    if (tool_ != tool) {
        cancelToolPreview();  // 切换工具时丢弃未提交的图形/表格拖动预览
        if (textEditor_)
            commitTextEditing();  // 切换工具时提交正在编辑的文字
    }
    tool_ = tool;
    if (tool_ == Tool::Eraser) {
        ensureEraserItem();
        setCursor(Qt::BlankCursor);  // 橡皮预览框即光标
    } else {
        if (eraserPreview_)
            eraserPreview_->setVisible(false);
        switch (tool_) {
            case Tool::Select:
            case Tool::Lasso:
                setCursor(Qt::ArrowCursor);
                break;
            case Tool::Pan:
                setCursor(Qt::OpenHandCursor);
                break;
            case Tool::Text:
                setCursor(Qt::IBeamCursor);
                break;
            default:
                setCursor(Qt::CrossCursor);
                break;
        }
    }
    // 抓手工具交给 QGraphicsView 的 ScrollHandDrag 处理左键
    if (tool_ == Tool::Pan)
        setDragMode(QGraphicsView::ScrollHandDrag);
    else
        setDragMode(QGraphicsView::NoDrag);
    if (tool_ != Tool::Select && tool_ != Tool::Lasso)
        clearSelection();
    emit toolChanged(tool_);
}

void BoardView::undo() {
    data_.Undo();  // 成功时 onPageChanged 回调驱动全量刷新
}

void BoardView::redo() {
    data_.Redo();
}

void BoardView::setPenColor(uint32_t color) {
    penColor_ = color;
    data_.SetColor(color);
}

void BoardView::setPenWidth(int width) {
    penWidth_ = width;
    data_.SetPenWidth(width);
}

void BoardView::setTableSize(int rows, int cols) {
    tableRows_ = qBound(2, rows, 8);
    tableCols_ = qBound(2, cols, 8);
}

void BoardView::setTextFontSize(int size) {
    textFontSize_ = qBound(8, size, 200);
}

void BoardView::setTextColor(uint32_t color) {
    textColor_ = color;
}

void BoardView::setWidgetKind(int kind) {
    widgetKind_ = qBound(0, kind, 6);  // 0 秒表 / 1 计时器 / 2 计算器 / 3 算盘 / 4 骰子 / 5 大转盘 / 6 点名器
}

void BoardView::clearBoard() {
    data_.Clear();
}

// ---------- 页面管理 ----------

void BoardView::refreshPageIds() {
    const std::vector<std::string> ids = data_.GetPageIds();
    pageIds_.clear();
    for (const std::string& s : ids)
        pageIds_.append(QString::fromStdString(s));
    currentPageId_ = QString::fromStdString(data_.GetCurrentPageId());
}

void BoardView::createPage() {
    if (pageIds_.size() >= kMaxPages)
        return;  // 页数上限：最多 20 页（参考 MaxWhiteboard）
    data_.CreatePage();
    refreshPageIds();
    reloadPage();
    emit pagesChanged();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
}

bool BoardView::deletePage(const QString& pageId) {
    if (!data_.DeletePage(pageId.toStdString()))
        return false;
    refreshPageIds();
    reloadPage();
    emit pagesChanged();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
    return true;
}

void BoardView::selectPage(const QString& pageId) {
    if (pageId == currentPageId_)
        return;
    data_.SelectPage(pageId.toStdString());
    currentPageId_ = pageId;
    reloadPage();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
}

// 全量重建当前页（切换/新建/删除页面、撤销/重做后调用）
void BoardView::reloadPage() {
    if (textEditor_)
        cancelTextEditing();  // 先清编辑框：scene_.clear() 会删除代理，防悬空
    if (optionsEditor_)
        cancelWidgetOptionsEditing();  // 同理：选项弹窗代理随 scene_.clear() 删除
    clearSelection();
    scene_.clear();  // 删除所有图元
    elementItems_.clear();
    cellOwner_.clear();
    tableChildren_.clear();
    tableLayouts_.clear();  // 表格布局缓存随页面全量重建失效
    mindLayouts_.clear();  // 布局缓存随页面全量重建失效
    pendingItems_.clear();
    remotePreview_.clear();
    previewItem_ = nullptr;
    toolDrawPreview_ = nullptr;  // 已被 scene_.clear() 删除，防悬空
    textEditFrame_ = nullptr;    // 已被 scene_.clear() 删除，防悬空（编辑框已先行取消）
    optionsEditFrame_ = nullptr; // 已被 scene_.clear() 删除，防悬空（弹窗已先行取消）
    optionsEditHint_ = nullptr;  // 同上
    toolDrawing_ = false;
    eraserPreview_ = nullptr;  // 懒重建
    rubberBand_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    lassoPreview_ = nullptr;   // 已被 scene_.clear() 删除，防悬空
    remoteEraserPreview_ = nullptr;    // 已被 scene_.clear() 删除，防悬空
    remoteEraserMask_ = nullptr;       // 已被 scene_.clear() 删除，防悬空
    remoteLassoPreview_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    remoteSelectionPreview_ = nullptr; // 已被 scene_.clear() 删除，防悬空
    remoteEraserSessionId_.clear();
    remoteEraserLastRect_ = QRectF();

    whiteboard::Page page;
    data_.GetPage(page);
    for (auto& e : page.elements)
        buildElementItem(*e);
    if (tool_ == Tool::Eraser)
        ensureEraserItem();
}

// 全量导出所有页面（页数面板缩略图用；数据线程深拷贝，同步返回）
std::vector<whiteboard::Page> BoardView::allPages() {
    std::vector<whiteboard::Page> pages;
    data_.GetAllPages(pages);
    return pages;
}

// 单页渲染为缩略图（复刻参考 DisplaySlideManagerView 的 Viewbox 算法）：
// 1. 视口 = 元素并集包围盒；空页或被画板区(1920x1080 外扩 5)包含时取画板区；
// 2. 否则按中轴补齐到画板尺寸，再四周外扩 1/64；
// 3. 以 Stretch=Fill 语义（横纵独立缩放）填充目标尺寸，背景为黑板色。
QPixmap BoardView::renderPageThumbnail(const whiteboard::Page& page, const QSize& size,
                                       const QPixmap& background) {
    QPixmap pixmap(size);
    if (background.isNull()) {
        pixmap.fill(kBoardBackground);
    } else {
        // 背景图铺底：拉伸铺满缩略图（与画布观感一致）
        pixmap.fill(Qt::transparent);
        QPainter bgPainter(&pixmap);
        bgPainter.setRenderHint(QPainter::SmoothPixmapTransform);
        bgPainter.drawPixmap(QRect(QPoint(0, 0), size), background);
    }

    // 画板基准区：1080p 画板外扩 5
    const QRectF slideBounds(-5.0, -5.0, kViewWidth + 10.0, kViewHeight + 10.0);

    // 元素并集包围盒（无元素为空）
    QRectF least;
    bool hasContent = false;
    for (const auto& e : page.elements)
        collectThumbBounds(*e, least, hasContent);

    if (!hasContent || slideBounds.contains(least)) {
        least = slideBounds;
    } else {
        // 中轴补齐到画板尺寸（宽度/高度不足时向两侧各扩差值一半）
        if (least.width() < kViewWidth) {
            const qreal dw = (kViewWidth - least.width()) / 2.0;
            least.adjust(-dw, 0, dw, 0);
        }
        if (least.height() < kViewHeight) {
            const qreal dh = (kViewHeight - least.height()) / 2.0;
            least.adjust(0, -dh, 0, dh);
        }
        // 四周外扩 1/64（参考 Inflate(Width/64, Height/64)，两侧合计 1/32）
        least.adjust(-least.width() / 64.0, -least.height() / 64.0,
                     least.width() / 64.0, least.height() / 64.0);
    }

    if (least.width() <= 0 || least.height() <= 0)
        return pixmap;

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // 注意顺序：Qt 后调用的变换先作用于点（CTM 右乘）。
    // 必须先 scale 再 translate：点先减去 least 左上角移到原点，再缩放到目标尺寸；
    // 反序（先 translate 后 scale）会把平移量按未缩放坐标叠加，
    // 内容偏离画板区（|least.left/top| 较大）时全部被画出缩略图外（空白）。
    p.scale(size.width() / least.width(), size.height() / least.height());
    p.translate(-least.left(), -least.top());
    for (const auto& e : page.elements)
        paintThumbElement(p, *e);
    return pixmap;
}

// ---------- 视图缩放（1080p 等比适配 + 用户缩放因子，最小级别适配全部内容） ----------

// 1080p 基准区域等比适配当前窗口，再叠加用户缩放因子（上限 300%）。
void BoardView::fitView() {
    userScale_ = qBound(kMinFitScale, userScale_, kMaxScale);
    fitContentMode_ = false;
    resetTransform();
    fitInView(QRectF(0, 0, kViewWidth, kViewHeight), Qt::KeepAspectRatio);
    scale(userScale_, userScale_);
    emit zoomChanged(qRound(userScale_ * 100.0));
}

// 计算"看全所有内容"所需的最小缩放（基于 1080p 基准视图换算）：
// 无内容返回 50%；否则按内容包围盒尺寸求值，clamp 到 [5%, 100%]。
qreal BoardView::computeContentFitScale() const {
    QRectF content;
    bool hasContent = false;
    for (auto it = elementItems_.constBegin(); it != elementItems_.constEnd(); ++it) {
        if (!*it)
            continue;
        content = hasContent ? content.united((*it)->sceneBoundingRect())
                             : (*it)->sceneBoundingRect();
        hasContent = true;
    }
    for (auto it = remotePreview_.constBegin(); it != remotePreview_.constEnd(); ++it) {
        if (!*it)
            continue;
        content = hasContent ? content.united((*it)->sceneBoundingRect())
                             : (*it)->sceneBoundingRect();
        hasContent = true;
    }
    if (!hasContent)
        return kMinScale;

    const QSize vp = viewport()->size();
    if (vp.width() <= 0 || vp.height() <= 0)
        return kMinScale;

    // 1080p 基准视图的缩放（KeepAspectRatio）
    const qreal baseScale = qMin(vp.width() / kViewWidth, vp.height() / kViewHeight);
    if (baseScale <= 0 || content.width() <= 0 || content.height() <= 0)
        return kMinScale;

    const qreal fit = qMin(vp.width() / (content.width() * baseScale),
                           vp.height() / (content.height() * baseScale));
    return qBound(kMinFitScale, fit, 1.0);
}

// 缩到最小：视图居中显示全部内容（变换 = fitInView(内容)）。
void BoardView::fitContentView() {
    fitContentMode_ = true;
    fitContentRect_ = QRectF();
    for (auto it = elementItems_.constBegin(); it != elementItems_.constEnd(); ++it) {
        if (!*it)
            continue;
        fitContentRect_ = fitContentRect_.isNull() ? (*it)->sceneBoundingRect()
                                                   : fitContentRect_.united((*it)->sceneBoundingRect());
    }
    for (auto it = remotePreview_.constBegin(); it != remotePreview_.constEnd(); ++it) {
        if (!*it)
            continue;
        fitContentRect_ = fitContentRect_.isNull() ? (*it)->sceneBoundingRect()
                                                   : fitContentRect_.united((*it)->sceneBoundingRect());
    }
    const bool empty = fitContentRect_.isNull();
    if (empty)
        fitContentRect_ = QRectF(0, 0, kViewWidth, kViewHeight);

    resetTransform();
    fitInView(fitContentRect_, Qt::KeepAspectRatio);
    if (empty)
        scale(userScale_, userScale_);  // 无内容：视觉与名义比例一致（避免 60%→100% 跳变）
    emit zoomChanged(qRound(userScale_ * 100.0));
}

// 滚轮缩放：每次 ±10%（步进 0.1），保持鼠标位置为锚点；
// 缩到最小级别时视图自动居中显示全部白板。
// 计时器卡片设置态优先：光标悬于卡片时分秒滚轮区 → 调整时长（不缩放视图）
void BoardView::wheelEvent(QWheelEvent* event) {
    if ((tool_ == Tool::Select || tool_ == Tool::Lasso || tool_ == Tool::Widget) &&
        event->angleDelta().y() != 0 &&
        widgetWheelAdjust(mapToScene(event->pos()), event->angleDelta().y() > 0 ? -1 : 1)) {
        event->accept();
        return;
    }
    const bool up = event->angleDelta().y() > 0;
    qreal target = userScale_ + (up ? kScaleStep : -kScaleStep);
    target = qBound(kMinFitScale, target, kMaxScale);

    if (up) {
        if (qFuzzyCompare(target, userScale_)) {
            event->accept();
            return;
        }
        const qreal factor = target / userScale_;
        userScale_ = target;
        if (fitContentMode_) {
            // 从最小级别放大：在当前 fit 变换上按鼠标锚点继续放大
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            setTransformationAnchor(QGraphicsView::AnchorViewCenter);
            fitContentMode_ = false;
        } else {
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        }
        emit zoomChanged(qRound(userScale_ * 100.0));
        event->accept();
        return;
    }

    // 缩小：先检查是否会到达最小级别。
    // 内容超出视口时最小级别 = fitScale（看全全部内容）；
    // 内容较少（fitScale 已达 100% 上限，内容本就全部可见）时最小级别 = 50%，继续按步进缩小。
    const qreal fitScale = computeContentFitScale();
    const qreal minScale = (fitScale < 1.0) ? fitScale : kMinScale;
    if (target <= minScale + 1e-6) {
        if (fitContentMode_ && qFuzzyCompare(userScale_, minScale)) {
            event->accept();
            return;  // 已是最小级别
        }
        if (fitScale < 1.0) {
            userScale_ = fitScale;
            fitContentView();  // 内容超出视口：缩到最小并居中显示全部白板
        } else {
            const qreal clamped = qMax(kMinScale, target);
            const qreal factor = clamped / userScale_;
            userScale_ = clamped;
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            setTransformationAnchor(QGraphicsView::AnchorViewCenter);
            fitContentMode_ = false;
            emit zoomChanged(qRound(userScale_ * 100.0));
        }
    } else {
        const qreal factor = target / userScale_;
        userScale_ = target;
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        scale(factor, factor);
        setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        fitContentMode_ = false;
        emit zoomChanged(qRound(userScale_ * 100.0));
    }
    event->accept();
}

void BoardView::zoomIn() {
    if (fitContentMode_)
        userScale_ = computeContentFitScale();  // 从最小级别恢复名义值
    userScale_ = qBound(kMinFitScale, userScale_ + kScaleStep, kMaxScale);
    fitView();
}

void BoardView::zoomOut() {
    if (fitContentMode_)
        userScale_ = computeContentFitScale();
    const qreal fitScale = computeContentFitScale();
    const qreal minScale = (fitScale < 1.0) ? fitScale : kMinScale;
    if (userScale_ - kScaleStep <= minScale + 1e-6) {
        if (fitScale < 1.0) {
            userScale_ = fitScale;
            fitContentView();  // 内容超出视口：缩到最小显示全部白板
        } else {
            userScale_ = kMinScale;  // 内容较少（本就全部可见）：最小 50%
            fitView();
        }
        return;
    }
    userScale_ = userScale_ - kScaleStep;
    fitView();
}

void BoardView::resetZoom() {
    userScale_ = 1.0;
    fitView();
}

// ---------- 绘制基础设施 ----------

void BoardView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    // 等比适配新窗口尺寸：最小级别（看全所有内容）状态保持，否则保持用户缩放因子
    if (fitContentMode_)
        fitContentView();
    else
        fitView();
}

// ---------- 中键平移（任意工具下可用；抓手工具左键走 ScrollHandDrag） ----------

void BoardView::beginPan(const QPoint& viewPos) {
    panning_ = true;
    lastPanPos_ = viewPos;
    setCursor(Qt::ClosedHandCursor);
}

void BoardView::updatePan(const QPoint& viewPos) {
    if (!panning_)
        return;
    const QPoint d = viewPos - lastPanPos_;
    lastPanPos_ = viewPos;
    // 平移视图内容：滚动条始终隐藏，但 value 依然有效
    QScrollBar* h = horizontalScrollBar();
    QScrollBar* v = verticalScrollBar();
    h->setValue(h->value() - d.x());
    v->setValue(v->value() - d.y());
}

void BoardView::endPan() {
    if (!panning_)
        return;
    panning_ = false;
    setCursor(tool_ == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

QPen BoardView::strokePen(uint32_t color, int width) {
    return QPen(colorToQColor(color), qMax(1, width),
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QGraphicsPathItem* BoardView::createPreviewItem(const QPointF& start) {
    QPainterPath path;
    path.moveTo(start);
    auto* item = new QGraphicsPathItem();
    item->setPath(path);
    item->setPen(strokePen(penColor_, penWidth_));
    item->setBrush(Qt::NoBrush);
    scene_.addItem(item);
    return item;
}

QGraphicsPathItem* BoardView::buildStrokeItem(const whiteboard::Stroke& stroke,
                                              const std::string& id) {
    auto* item = new QGraphicsPathItem();
    item->setPath(strokePath(stroke));
    item->setPen(strokePen(stroke.color, stroke.width));
    item->setBrush(Qt::NoBrush);
    item->setData(0, QString::fromStdString(id));
    // 外接矩形直接复用数据层已算好的 stroke.bounding，UI 不再从 path 重算
    const whiteboard::Rect br = stroke.bounding.ToRect();
    QRectF rect(br.x, br.y, br.width, br.height);
    if (rect.width() <= 0)
        rect.setWidth(1);
    if (rect.height() <= 0)
        rect.setHeight(1);
    item->setData(1, rect);
    scene_.addItem(item);
    elementItems_.insert(QString::fromStdString(id), item);
    return item;
}

// 图形元素：多子路径描边；data(2)="graphic"、data(3)=closed 标记列表（供变换烘焙重建）
QGraphicsPathItem* BoardView::buildGraphicItem(const whiteboard::GraphicElement& g,
                                               const std::string& id) {
    auto* item = new QGraphicsPathItem();
    item->setPath(graphicPath(g));
    item->setPen(strokePen(g.color, g.width));
    item->setBrush(Qt::NoBrush);
    item->setData(0, QString::fromStdString(id));
    const whiteboard::Rect br = g.bounding.ToRect();
    QRectF rect(br.x, br.y, br.width, br.height);
    if (rect.width() <= 0)
        rect.setWidth(1);
    if (rect.height() <= 0)
        rect.setHeight(1);
    item->setData(1, rect);
    item->setData(2, QStringLiteral("graphic"));
    QVariantList closedList;
    for (const auto& sp : g.subpaths)
        closedList.append(sp.closed);
    item->setData(3, closedList);
    scene_.addItem(item);
    elementItems_.insert(QString::fromStdString(id), item);
    return item;
}

// 表格元素：网格图元（由 ComputeLayout 生成：data(1)=未旋转布局外框、data(4)=行列数；
// rot!=0 时施加绕布局中心旋转；布局写入 tableLayouts_ 缓存与网格 path 同源）+ 递归构建
// 格内子元素（path 为格局部坐标，item 变换 = 自身局部变换 * 格帧）
QGraphicsPathItem* BoardView::buildTableItem(const whiteboard::TableElement& t,
                                             const std::string& id) {
    const whiteboard::TableElement::Layout layout = t.ComputeLayout();
    const QString tableKey = QString::fromStdString(id);
    auto* item = new QGraphicsPathItem();
    item->setPath(tableGridPathFromLayout(t.origin, layout));
    item->setPen(strokePen(t.color, t.width));
    item->setBrush(Qt::NoBrush);
    item->setData(0, tableKey);
    QRectF boundsRect(t.origin.x, t.origin.y, layout.totalW, layout.totalH);
    if (boundsRect.width() <= 0)
        boundsRect.setWidth(1);
    if (boundsRect.height() <= 0)
        boundsRect.setHeight(1);
    item->setData(1, boundsRect);
    item->setData(2, QStringLiteral("table"));
    QVariantList dims;
    dims << t.rows << t.cols;
    item->setData(4, dims);  // 行列数（诊断信息；网格 path 即布局结果）
    item->setZValue(-1);  // 网格垫底：单元格内笔迹覆盖网格线
    if (t.rotation != 0.0f) {
        const qreal cx = t.origin.x + layout.totalW / 2.0;
        const qreal cy = t.origin.y + layout.totalH / 2.0;
        item->setTransform(QTransform().translate(cx, cy).rotate(t.rotation).translate(-cx, -cy));
    }
    scene_.addItem(item);
    elementItems_.insert(tableKey, item);
    tableLayouts_.insert(tableKey, layout);

    // 递归构建格内子元素并登记归属（tableChildren_ 语义 = 全部后代，嵌套表格递归并入）
    for (size_t ci = 0; ci < t.cells.size(); ++ci) {
        const QTransform frame = tableCellFrame(t, layout, static_cast<int>(ci));
        for (const auto& child : t.cells[ci]) {
            if (!child)
                continue;
            QGraphicsPathItem* childItem = buildElementItem(*child);
            if (!childItem)
                continue;
            // 子元素 path 为格局部坐标：叠加格帧（含表格旋转）映射到页面
            childItem->setTransform(childItem->transform() * frame);
            const QString childKey = QString::fromStdString(child->id);
            cellOwner_.insert(childKey, tableKey);
            tableChildren_[tableKey].append(childKey);
            if (tableChildren_.contains(childKey))
                tableChildren_[tableKey] += tableChildren_.value(childKey);
        }
    }
    return item;
}

// 整表重建：按数据层最新快照重建表格图元与全部格内子元素（格局部坐标 + 格帧变换）。
// 用于格内内容变更（笔迹落库/橡皮/文字编辑/远端同步）与变换烘焙后布局重排更新；
// 重建前记录选中 id，重建后按 expandToWholeTables 恢复整表选中（视觉零跳变）。
void BoardView::rebuildTable(const QString& tableId) {
    const auto snapshot = data_.GetElementSnapshot(tableId.toStdString());
    auto* table = dynamic_cast<whiteboard::TableElement*>(snapshot.get());
    if (!table)
        return;

    // 重建前选中 id 集合（含展开的整表子元素）
    QSet<QString> selectedIds;
    for (QGraphicsPathItem* it : selectedItems_) {
        const QVariant v = it ? it->data(0) : QVariant();
        if (v.isValid())
            selectedIds.insert(v.toString());
    }

    // 清理旧图元（后代 + 表格自身）与归属/布局镜像
    QStringList oldIds = tableChildren_.value(tableId);
    oldIds.append(tableId);
    bool selectionTouched = false;
    for (const QString& key : oldIds) {
        cellOwner_.remove(key);
        tableChildren_.remove(key);
        tableLayouts_.remove(key);
        mindLayouts_.remove(key);
        widgetRuntimes_.remove(key);
        QGraphicsPathItem* item = elementItems_.take(key);
        if (!item)
            continue;
        if (selectedItems_.removeAll(item) > 0)
            selectionTouched = true;
        scene_.removeItem(item);
        delete item;
    }

    // 按快照重建（buildTableItem 重新写入 tableChildren_/tableLayouts_ 等镜像）
    buildElementItem(*table);

    // 选中恢复：原先选中过整表（或任一后代）→ 重建后展开为整表选中
    bool wasSelected = selectedIds.contains(tableId);
    if (!wasSelected) {
        const QStringList kids = tableChildren_.value(tableId);
        for (const QString& kid : kids) {
            if (selectedIds.contains(kid)) {
                wasSelected = true;
                break;
            }
        }
    }
    if (wasSelected || selectionTouched) {
        QList<QGraphicsPathItem*> keep = selectedItems_;
        if (wasSelected) {
            QGraphicsPathItem* item = elementItems_.value(tableId, nullptr);
            if (item && !keep.contains(item))
                keep.append(item);
            keep = expandToWholeTables(keep);
        }
        clearSelection();
        if (!keep.isEmpty())
            selectItems(keep);
    }
}

// 思维导图元素：按树布局生成路径（连线 + 圆角节点框 + 折叠钮 + 聚焦钮）。
// data(2)="mindmap"、data(1)=布局包围盒（1px 保底）、data(5)={root.x, root.y, scaleX, scaleY}
// （变换烘焙用）；rotation≠0 时绕 root 经 item transform 渲染；布局写入 mindLayouts_ 缓存。
QGraphicsPathItem* BoardView::buildMindMapItem(const whiteboard::MindMapElement& m,
                                               const std::string& id) {
    const QString key = QString::fromStdString(id);
    const whiteboard::MindLayout layout = whiteboard::ComputeMindMapLayout(m);
    const QString focusNode = (mindFocusMap_ == key) ? mindFocusNode_ : QString();
    auto* item = new QGraphicsPathItem();
    item->setPath(mindMapPathFromLayout(layout, focusNode, true));
    item->setPen(strokePen(m.color, m.width));
    item->setBrush(Qt::NoBrush);
    item->setData(0, key);
    QRectF boundsRect(layout.bounds.x, layout.bounds.y, layout.bounds.width, layout.bounds.height);
    if (boundsRect.width() <= 0)
        boundsRect.setWidth(1);
    if (boundsRect.height() <= 0)
        boundsRect.setHeight(1);
    item->setData(1, boundsRect);
    item->setData(2, QStringLiteral("mindmap"));
    QVariantList geo;
    geo << m.root.x << m.root.y << m.scaleX << m.scaleY;
    item->setData(5, geo);  // 变换烘焙：新 root 与 scale 写回用
    if (m.rotation != 0.0f) {
        item->setTransform(QTransform().translate(m.root.x, m.root.y)
                               .rotate(m.rotation).translate(-m.root.x, -m.root.y));
    }
    scene_.addItem(item);
    elementItems_.insert(key, item);
    mindLayouts_.insert(key, layout);
    return item;
}

// 文字元素：字形路径填充（无描边，笔刷=文字色）；data(6) 存
// {text,x,y,fontSize,rotation,color}（编辑定位/变换烘焙重建用）；rotation != 0 时绕锚点旋转
QGraphicsPathItem* BoardView::buildTextItem(const whiteboard::TextElement& t,
                                            const std::string& id) {
    auto* item = new QGraphicsPathItem();
    item->setPath(textPath(t));
    item->setPen(Qt::NoPen);
    item->setBrush(colorToQColor(t.color));
    item->setData(0, QString::fromStdString(id));
    QRectF br = item->path().boundingRect();
    if (br.width() <= 0)
        br.setWidth(1);
    if (br.height() <= 0)
        br.setHeight(1);
    item->setData(1, br);
    item->setData(2, QStringLiteral("text"));
    QVariantList meta;
    meta << QString::fromUtf8(t.text.c_str()) << t.x << t.y << t.fontSize << t.rotation
         << static_cast<uint>(t.color);
    item->setData(6, meta);
    if (t.rotation != 0.0f) {
        item->setTransform(QTransform().translate(t.x, t.y).rotate(t.rotation)
                               .translate(-t.x, -t.y));
    }
    scene_.addItem(item);
    elementItems_.insert(QString::fromStdString(id), item);
    return item;
}

// 小工具元素：苹果风卡片（WidgetCardItem 自绘）。data(1)=卡片矩形、data(2)="widget"、
// data(4)={kind, x, y, durationSec, scale}；走时状态在 widgetRuntimes_（本地运行时，
// 不落数据），同 id 重建（切页往返/撤销重做）保留已有时长状态；新建 id 初始化为
// 停止态（计时器进入设置态：卡片内滚轮调时长）。
QGraphicsPathItem* BoardView::buildWidgetItem(const whiteboard::WidgetElement& w,
                                              const std::string& id) {
    const QString key = QString::fromStdString(id);
    const bool fresh = !widgetRuntimes_.contains(key);
    WidgetRuntime& rt = widgetRuntimes_[key];
    if (fresh || rt.kind != w.kind) {
        // 新建（或类型变化）：停止 + 归零；计时器/骰子进入设置态（骰子参数取数据）；
        // 计算器/算盘清零（本地运行时）
        rt.running = false;
        rt.finished = false;
        rt.startMs = 0;
        rt.accumMs = 0;
        rt.setupMode = (w.kind == 1 || w.kind == 4);
        rt.wheelH = qMax(0, w.durationSec) / 3600;
        rt.wheelM = (qMax(0, w.durationSec) / 60) % 60;
        rt.wheelS = qMax(0, w.durationSec) % 60;
        rt.calcDisplay = QStringLiteral("0");
        rt.calcAcc = 0.0;
        rt.calcOp = 0;
        rt.calcFresh = true;
        rt.calcError = false;
        for (int i = 0; i < 9; ++i) {
            rt.abacusHigh[i] = 0;
            rt.abacusLow[i] = 0;
        }
        rt.rolling = false;
        rt.rollStartMs = 0;
        rt.lastRollShuffleMs = 0;
        for (int i = 0; i < 10; ++i) {
            rt.diceFaces[i] = 0;
            rt.diceJitter[i] = 0;
        }
        // 转盘/点名器本地运行时归零（已抽集合/角度/结果/滚动；不落数据）
        rt.wheelDrawn.clear();
        rt.wheelAngle = 0.0;
        rt.wheelSpinning = false;
        rt.wheelSpinStartMs = 0;
        rt.wheelResultIdx = -1;
        rt.wheelResult.clear();
        rt.wheelHint.clear();
        rt.nameDrawn.clear();
        rt.nameRolling = false;
        rt.nameRollStartMs = 0;
        rt.lastNameShuffleMs = 0;
        rt.nameRollCursor = 0;
        rt.nameResult.clear();
        rt.nameHint.clear();
    }
    rt.kind = w.kind;
    rt.durationSec = qMax(0, w.durationSec);
    rt.diceSides = normalizeDiceSides(w.diceSides);
    rt.diceCount = qBound(1, w.diceCount, 10);
    rt.wheelDedup = w.dedup;
    rt.nameDedup = w.dedup;
    rt.pickCount = qBound(1, w.pickCount, 5);
    if (w.kind == 5 || w.kind == 6) {
        // 选项/名单每次从数据解析（撤销/重做重建后同步）；权重语法见 parseOptions
        const QString raw = QString::fromStdString(w.options);
        QStringList names;
        QVector<double> weights;
        parseOptions(raw, names, weights);
        if (w.kind == 5) {
            rt.wheelOptionsText = raw;
            rt.wheelOptions = names;
            rt.wheelWeights = weights;
        } else {
            rt.nameOptionsText = raw;
            rt.nameOptions = names;
            rt.nameWeights = weights;
        }
    }

    const qreal s = qBound<qreal>(kWidgetMinScale, w.scale, kWidgetMaxScale);
    auto* item = new WidgetCardItem();
    item->kind = w.kind;  // paint/命中按类型分派
    item->cardRect = QRectF(w.x, w.y, kWidgetCardW * s, kWidgetCardH * s);
    item->setPath(widgetCardPath(w.x, w.y, s));
    item->setPen(Qt::NoPen);
    item->setBrush(Qt::NoBrush);
    item->setData(0, key);
    item->setData(1, item->cardRect);
    item->setData(2, QStringLiteral("widget"));
    QVariantList meta;
    meta << w.kind << w.x << w.y << w.durationSec << s;
    item->setData(4, meta);  // 变换烘焙（平移 + 缩放）/ 显示文本基准
    scene_.addItem(item);
    elementItems_.insert(key, item);
    refreshWidgetItem(key);  // 初始显示：秒表 00:00.00 / 计时器滚轮设置态
    return item;
}

qint64 BoardView::widgetElapsedMs(const WidgetRuntime& rt) const {
    return rt.accumMs + (rt.running ? QDateTime::currentMSecsSinceEpoch() - rt.startMs : 0);
}

// 重算显示并刷新卡片（运行时态已定型后调用）：计时器设置态显示滚轮，
// 否则显示倒计时/走时文本
void BoardView::refreshWidgetItem(const QString& id) {
    auto* card = dynamic_cast<WidgetCardItem*>(elementItems_.value(id, nullptr));
    const auto it = widgetRuntimes_.constFind(id);
    if (!card || it == widgetRuntimes_.constEnd())
        return;
    const WidgetRuntime& rt = it.value();
    if (rt.kind == 2) {
        card->setCalcState(rt.calcDisplay, rt.calcError);
        return;
    }
    if (rt.kind == 3) {
        card->setAbacusState(rt.abacusHigh, rt.abacusLow);
        return;
    }
    if (rt.kind == 4) {
        card->setDiceState(rt.diceSides, rt.diceCount, rt.setupMode, rt.rolling,
                           rt.diceFaces, rt.diceJitter);
        return;
    }
    if (rt.kind == 5) {
        card->setWheelState(rt.wheelOptionsText, rt.wheelOptions, rt.wheelWeights, rt.wheelDrawn,
                            rt.wheelAngle, rt.wheelSpinning, rt.wheelResult, rt.wheelDedup,
                            rt.wheelHint);
        return;
    }
    if (rt.kind == 6) {
        QStringList rollTexts;
        if (rt.nameRolling && !rt.nameOptions.isEmpty()) {
            // 各格错位同步滚动（相位随格号偏移）
            const int cnt = qMin(qBound(1, rt.pickCount, 5), rt.nameOptions.size());
            for (int i = 0; i < cnt; ++i) {
                const int idx = (rt.nameRollCursor + i * 3) % rt.nameOptions.size();
                rollTexts.append(rt.nameOptions[idx]);
            }
        }
        card->setNameState(rt.nameOptionsText, rt.nameOptions, rt.nameDrawn, rt.nameRolling,
                           rollTexts, rt.nameResult, rt.pickCount, rt.nameDedup, rt.nameHint);
        return;
    }
    if (rt.kind == 1 && rt.setupMode) {
        card->setSetupState(rt.wheelH, rt.wheelM, rt.wheelS);
        return;
    }
    if (rt.kind == 1) {
        const qint64 remain = static_cast<qint64>(rt.durationSec) * 1000 - widgetElapsedMs(rt);
        card->setRuntimeState(widgetTimerText(remain), rt.running, rt.finished || remain <= 0);
    } else {
        card->setRuntimeState(widgetStopwatchText(widgetElapsedMs(rt)), rt.running, false);
    }
}

// 走时定时器 tick（33ms）：计时器归零自动停表变橙；骰子滚动每 ~60ms 换面，
// 2 秒后定格每颗真随机值；无活动实例时自停定时器
void BoardView::updateWidgetTick() {
    bool anyRunning = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = widgetRuntimes_.begin(); it != widgetRuntimes_.end(); ++it) {
        WidgetRuntime& rt = it.value();
        if (rt.kind == 4 && rt.rolling) {
            if (now - rt.rollStartMs >= 2000) {
                // 2 秒到：定格每颗真随机值（去掉抖动）
                for (int i = 0; i < rt.diceCount; ++i) {
                    rt.diceFaces[i] = QRandomGenerator::global()->bounded(rt.diceSides) + 1;
                    rt.diceJitter[i] = 0;
                }
                rt.rolling = false;
                rt.lastRollShuffleMs = 0;
                refreshWidgetItem(it.key());
                continue;  // 动画结束：不置 anyRunning
            }
            anyRunning = true;
            if (now - rt.lastRollShuffleMs >= 60) {
                rt.lastRollShuffleMs = now;
                for (int i = 0; i < rt.diceCount; ++i) {
                    rt.diceFaces[i] = QRandomGenerator::global()->bounded(rt.diceSides) + 1;
                    rt.diceJitter[i] = QRandomGenerator::global()->bounded(21) - 10;
                }
                refreshWidgetItem(it.key());
            }
            continue;
        }
        if (rt.kind == 5 && rt.wheelSpinning) {
            // 转盘：3 秒 easeOutCubic 缓出；到点定格（记录结果、去重集合加入）
            const double t = (now - rt.wheelSpinStartMs) / 3000.0;
            if (t >= 1.0) {
                rt.wheelAngle = rt.wheelSpinTo;
                rt.wheelSpinning = false;
                if (rt.wheelResultIdx >= 0 && rt.wheelResultIdx < rt.wheelOptions.size())
                    rt.wheelResult = rt.wheelOptions[rt.wheelResultIdx];
                if (rt.wheelDedup && !rt.wheelResult.isEmpty())
                    rt.wheelDrawn.insert(rt.wheelResult);
                refreshWidgetItem(it.key());
                continue;  // 动画结束：不置 anyRunning
            }
            anyRunning = true;
            rt.wheelAngle = rt.wheelSpinFrom +
                            (rt.wheelSpinTo - rt.wheelSpinFrom) * easeOutCubic(t);
            refreshWidgetItem(it.key());
            continue;
        }
        if (rt.kind == 6 && rt.nameRolling) {
            if (now - rt.nameRollStartMs >= 2000) {
                // 2 秒到：定格加权抽 N 人（去重时排除已点；结果加入已点集合）
                rt.nameRolling = false;
                rt.lastNameShuffleMs = 0;
                const QSet<QString> exclude = rt.nameDedup ? rt.nameDrawn : QSet<QString>();
                QSet<QString> pickedSet = exclude;
                QStringList picked;
                const int want = qMin(qBound(1, rt.pickCount, 5), rt.nameOptions.size());
                for (int k = 0; k < want; ++k) {
                    const int idx = weightedPick(rt.nameOptions, rt.nameWeights, pickedSet);
                    if (idx < 0)
                        break;
                    picked.append(rt.nameOptions[idx]);
                    pickedSet.insert(rt.nameOptions[idx]);  // 同一人只点一次
                }
                rt.nameResult = picked;
                for (const QString& n : picked)
                    rt.nameDrawn.insert(n);
                refreshWidgetItem(it.key());
                continue;  // 动画结束：不置 anyRunning
            }
            anyRunning = true;
            if (now - rt.lastNameShuffleMs >= 50) {
                rt.lastNameShuffleMs = now;
                rt.nameRollCursor += 1 + QRandomGenerator::global()->bounded(3);
                refreshWidgetItem(it.key());
            }
            continue;
        }
        if (!rt.running)
            continue;
        if (rt.kind == 1 && widgetElapsedMs(rt) >= static_cast<qint64>(rt.durationSec) * 1000) {
            rt.accumMs = static_cast<qint64>(rt.durationSec) * 1000;  // 冻结在归零点
            rt.running = false;
            rt.finished = true;
            refreshWidgetItem(it.key());
            continue;
        }
        anyRunning = true;
        refreshWidgetItem(it.key());
    }
    if (!anyRunning && widgetTick_)
        widgetTick_->stop();
}

// 开始/暂停：计时器设置态点开始 → 提交滚轮时长（≥1 秒，静默写回数据）进入倒计时；
// 归零后点击 → 从头重新开始
void BoardView::toggleWidgetRun(const QString& id) {
    auto it = widgetRuntimes_.find(id);
    if (it == widgetRuntimes_.end())
        return;
    WidgetRuntime& rt = it.value();
    if (rt.kind > 1)
        return;  // 仅秒表/计时器（计算器/算盘在 widgetControlPress；骰子走 diceStartRoll）
    if (rt.running) {
        rt.accumMs = widgetElapsedMs(rt);  // 暂停：累计到当前
        rt.running = false;
    } else {
        if (rt.kind == 1 && rt.setupMode) {
            // 设置态开始：提交滚轮时分秒（≥1 秒）到数据层（静默，不入撤销栈）
            const int sec = qMax(1, rt.wheelH * 3600 + rt.wheelM * 60 + rt.wheelS);
            rt.durationSec = sec;
            rt.setupMode = false;
            rt.finished = false;
            rt.accumMs = 0;
            if (auto* item = elementItems_.value(id, nullptr)) {
                QVariantList meta = item->data(4).toList();
                if (meta.size() >= 4) {
                    meta[3] = sec;
                    item->setData(4, meta);
                }
            }
            data_.UpdateWidgetDuration(id.toStdString(), sec);
        } else if (rt.kind == 1 && rt.finished) {
            rt.accumMs = 0;  // 归零后重新开始
            rt.finished = false;
        }
        rt.startMs = QDateTime::currentMSecsSinceEpoch();
        rt.running = true;
    }
    refreshWidgetItem(id);
    if (widgetTick_ && !widgetTick_->isActive())
        widgetTick_->start();
}

// 重置：停止并归零；计时器回到设置态（滚轮 = 当前时长）、秒表 00:00.00；
// 计算器 = C 清零、算盘 = 清盘归位、骰子 = 回设置态（保留参数，清空点数）
void BoardView::resetWidget(const QString& id) {
    auto it = widgetRuntimes_.find(id);
    if (it == widgetRuntimes_.end())
        return;
    WidgetRuntime& rt = it.value();
    if (rt.kind == 2) {
        rt.calcDisplay = QStringLiteral("0");
        rt.calcAcc = 0.0;
        rt.calcOp = 0;
        rt.calcFresh = true;
        rt.calcError = false;
        refreshWidgetItem(id);
        return;
    }
    if (rt.kind == 3) {
        for (int i = 0; i < 9; ++i) {
            rt.abacusHigh[i] = 0;
            rt.abacusLow[i] = 0;
        }
        refreshWidgetItem(id);
        return;
    }
    if (rt.kind == 4) {
        if (rt.rolling)
            return;  // 滚动中忽略
        rt.setupMode = true;
        rt.rolling = false;
        for (int i = 0; i < 10; ++i) {
            rt.diceFaces[i] = 0;
            rt.diceJitter[i] = 0;
        }
        refreshWidgetItem(id);
        return;
    }
    if (rt.kind == 5) {
        if (rt.wheelSpinning)
            return;  // 旋转中忽略
        rt.wheelDrawn.clear();
        rt.wheelResultIdx = -1;
        rt.wheelResult.clear();
        rt.wheelHint.clear();
        rt.wheelAngle = 0.0;
        refreshWidgetItem(id);
        return;
    }
    if (rt.kind == 6) {
        if (rt.nameRolling)
            return;  // 滚动中忽略
        rt.nameDrawn.clear();
        rt.nameResult.clear();
        rt.nameHint.clear();
        rt.nameRollCursor = 0;
        refreshWidgetItem(id);
        return;
    }
    rt.running = false;
    rt.finished = false;
    rt.startMs = 0;
    rt.accumMs = 0;
    if (rt.kind == 1) {
        rt.setupMode = true;
        rt.wheelH = rt.durationSec / 3600;
        rt.wheelM = (rt.durationSec / 60) % 60;
        rt.wheelS = rt.durationSec % 60;
    }
    refreshWidgetItem(id);
}

// 卡片控件动作分派（入参页面局部坐标；按 kind 解析控件码并执行动作）。
// 返回 true = 命中控件且已执行（调用方消费事件）；false = 非控件区（拖动/消费策略由调用方定）。
bool BoardView::widgetControlPress(const QString& id, const QPointF& local) {
    auto rtIt = widgetRuntimes_.find(id);
    auto* card = dynamic_cast<WidgetCardItem*>(elementItems_.value(id, nullptr));
    if (!card || rtIt == widgetRuntimes_.end())
        return false;
    WidgetRuntime& rt = rtIt.value();
    const int code = card->controlAt(local);
    if (code < 0)
        return false;
    if (code == 0) {
        resetWidget(id);  // 左圆钮（骰子结果态 = 回设置态保留参数）
        return true;
    }
    if (code == 1) {
        if (rt.kind == 4)
            diceStartRoll(id);  // 右圆钮（骰子结果态 = 再掷同参数）
        else if (rt.kind == 6)
            nameStartRoll(id);  // 右圆钮（点名器 = 开始滚动）
        else
            toggleWidgetRun(id);
        return true;
    }
    if (code >= 100 && code < 200) {
        calcPress(rt, code - 100);  // 计算器键
        refreshWidgetItem(id);
        return true;
    }
    if (code >= 200 && code < 300) {
        const int col = (code - 200) / 6;
        const int t = (code - 200) % 6;
        if (t == 0)
            rt.abacusHigh[col] = rt.abacusHigh[col] > 0 ? 0 : 1;  // 上珠：贴梁/离梁切换
        else
            rt.abacusLow[col] = (rt.abacusLow[col] == t) ? 0 : t;  // 下珠槽 t：同数归 0，异数设为 t
        refreshWidgetItem(id);
        return true;
    }
    if (code == 300) {
        resetWidget(id);  // 清盘钮：全部归位
        return true;
    }
    if (code >= 400 && code <= 404) {
        rt.diceSides = kDiceSidesOption[code - 400];  // 面数选择
        refreshWidgetItem(id);
        return true;
    }
    if (code == 410) {
        rt.diceCount = qMax(1, rt.diceCount - 1);
        refreshWidgetItem(id);
        return true;
    }
    if (code == 411) {
        rt.diceCount = qMin(10, rt.diceCount + 1);
        refreshWidgetItem(id);
        return true;
    }
    if (code == 412) {
        diceStartRoll(id);  // 确定：退出设置态开始 2 秒滚动
        return true;
    }
    if (code == 500) {
        wheelStartSpin(id);  // 转盘开始：3 秒旋转定格
        return true;
    }
    if (code >= 501 && code <= 503) {
        if (rt.wheelSpinning)
            return true;  // 旋转中忽略
        const int b = code - 501;
        if (b == 0) {
            beginWidgetOptionsEditing(id);  // 编辑选项
        } else if (b == 1) {
            rt.wheelDedup = !rt.wheelDedup;  // 去重开关（静默写回广播）
            rt.wheelHint.clear();
            data_.UpdateWidgetDedup(id.toStdString(), rt.wheelDedup);
            refreshWidgetItem(id);
        } else {
            resetWidget(id);  // 重置：清结果 + 恢复全部
        }
        return true;
    }
    if (code == 601) {
        if (!rt.nameRolling)
            beginWidgetOptionsEditing(id);  // 编辑名单
        return true;
    }
    if (code == 602) {
        if (rt.nameRolling)
            return true;  // 滚动中忽略
        rt.nameDedup = !rt.nameDedup;  // 去重开关（静默写回广播）
        rt.nameHint.clear();
        data_.UpdateWidgetDedup(id.toStdString(), rt.nameDedup);
        refreshWidgetItem(id);
        return true;
    }
    if (code == 603 || code == 604) {
        if (rt.nameRolling)
            return true;  // 滚动中忽略
        rt.pickCount = qBound(1, rt.pickCount + (code == 604 ? 1 : -1), 5);  // 人数 ±
        rt.nameHint.clear();
        data_.UpdateWidgetPickCount(id.toStdString(), rt.pickCount);
        refreshWidgetItem(id);
        return true;
    }
    return false;
}

// 计算器按键状态机（key = 0~18，见 calcKeyLabel 顺序）：完整四则运算（含连续运算、
// 退格、正负、小数、除零 Error）。模型：calcDisplay（当前输入/结果）+ calcAcc（左操作
// 数）+ calcOp（待定运算 0 无/1 +/2 −/3 ×/4 ÷）+ calcFresh（下一个数字键替换显示）
// + calcError（除零；任意键恢复清零）。
void BoardView::calcPress(WidgetRuntime& rt, int key) {
    auto fmt = [](double v) -> QString {
        return QString::number(v, 'g', 10);  // 紧凑显示（整数无小数点）
    };
    auto apply = [&rt, &fmt](double rhs, bool& ok) -> double {
        double result = rhs;
        ok = true;
        switch (rt.calcOp) {
            case 1: result = rt.calcAcc + rhs; break;
            case 2: result = rt.calcAcc - rhs; break;
            case 3: result = rt.calcAcc * rhs; break;
            case 4:
                if (rhs == 0.0) {
                    ok = false;
                    return 0.0;
                }
                result = rt.calcAcc / rhs;
                break;
            default:
                break;
        }
        rt.calcAcc = result;
        rt.calcDisplay = fmt(result);
        rt.calcFresh = true;
        return result;
    };

    if (rt.calcError) {  // Error 恢复：任意键先清零继续
        rt.calcError = false;
        rt.calcDisplay = QStringLiteral("0");
        rt.calcAcc = 0.0;
        rt.calcOp = 0;
        rt.calcFresh = true;
    }
    const double cur = rt.calcDisplay.toDouble();
    if (key == 0) {  // C：全清
        rt.calcDisplay = QStringLiteral("0");
        rt.calcAcc = 0.0;
        rt.calcOp = 0;
        rt.calcFresh = true;
        return;
    }
    if (key == 1) {  // ⌫：退格（末位删除；空/负号单位归 0）
        QString s = rt.calcDisplay;
        if (s.length() > 1 && !(s.length() == 2 && s.startsWith(QLatin1Char('-'))))
            s.chop(1);
        else
            s = QStringLiteral("0");
        rt.calcDisplay = s;
        return;
    }
    if (key == 16) {  // ±：正负切换
        if (rt.calcDisplay == QStringLiteral("0"))
            return;
        rt.calcDisplay = rt.calcDisplay.startsWith(QLatin1Char('-'))
                             ? rt.calcDisplay.mid(1)
                             : QLatin1Char('-') + rt.calcDisplay;
        return;
    }
    if (key == 18) {  // .：小数点（每值至多一个；新输入态从 0. 开始）
        if (rt.calcFresh) {
            rt.calcDisplay = QStringLiteral("0.");
            rt.calcFresh = false;
        } else if (!rt.calcDisplay.contains(QLatin1Char('.'))) {
            rt.calcDisplay += QLatin1Char('.');
        }
        return;
    }
    static const int digitOf[19] = { -1, -1, -1, -1, 7, 8, 9, -1, 4, 5, 6, -1,
                                     1, 2, 3, -1, -1, 0, -1 };
    if (digitOf[key] >= 0) {  // 数字键：替换（fresh）或追加（≤14 字符）
        const int d = digitOf[key];
        if (rt.calcFresh || rt.calcDisplay == QStringLiteral("0")) {
            rt.calcDisplay = QString::number(d);
            rt.calcFresh = false;
        } else if (rt.calcDisplay.length() < 14) {
            rt.calcDisplay += QString::number(d);
        }
        return;
    }
    int op = 0;  // 运算符：2=÷ 3=× 7=− 11=+
    if (key == 2)
        op = 4;
    else if (key == 3)
        op = 3;
    else if (key == 7)
        op = 2;
    else if (key == 11)
        op = 1;
    if (op != 0) {
        if (rt.calcOp != 0 && !rt.calcFresh) {
            bool ok = true;
            apply(cur, ok);  // 连续运算：先结算前一项（5+3+ → 8+）
            if (!ok) {
                rt.calcError = true;
                rt.calcDisplay = QStringLiteral("Error");
                rt.calcOp = 0;
                return;
            }
        } else {
            rt.calcAcc = cur;  // 首次/替换运算符：左操作数 = 当前显示
            rt.calcFresh = true;
        }
        rt.calcOp = op;
        return;
    }
    if (key == 15) {  // =：执行待定运算（无待定则仅定格显示）
        if (rt.calcOp != 0) {
            bool ok = true;
            apply(cur, ok);
            if (!ok) {
                rt.calcError = true;
                rt.calcDisplay = QStringLiteral("Error");
                rt.calcOp = 0;
                rt.calcAcc = 0.0;
                return;
            }
            rt.calcOp = 0;
        } else {
            rt.calcFresh = true;
        }
    }
}

// 骰子开始滚动（设置态"确定"与结果态"再掷"共用）：退出设置态，参数静默写回
// 数据层（不入撤销栈），随机初始化全部骰面/抖动，启动 2 秒滚动动画（tick 每
// ~60ms 换面；2 秒后定格真随机值）。
void BoardView::diceStartRoll(const QString& id) {
    auto rtIt = widgetRuntimes_.find(id);
    if (rtIt == widgetRuntimes_.end())
        return;
    WidgetRuntime& rt = rtIt.value();
    if (rt.kind != 4 || rt.rolling)
        return;
    rt.setupMode = false;
    rt.rolling = true;
    rt.rollStartMs = QDateTime::currentMSecsSinceEpoch();
    rt.lastRollShuffleMs = 0;  // 首帧立即换面
    for (int i = 0; i < rt.diceCount; ++i) {
        rt.diceFaces[i] = QRandomGenerator::global()->bounded(rt.diceSides) + 1;
        rt.diceJitter[i] = QRandomGenerator::global()->bounded(21) - 10;
    }
    data_.UpdateWidgetDiceParams(id.toStdString(), rt.diceSides, rt.diceCount);
    refreshWidgetItem(id);
    if (widgetTick_ && !widgetTick_->isActive())
        widgetTick_->start();
}

// 转盘开始旋转（「开始」钮）：加权随机选目标 → 3 秒缓出定格（至少 5 整圈，
// 停在目标扇区中心对准顶部指针）；无可抽项时不启动并在结果区提示
void BoardView::wheelStartSpin(const QString& id) {
    auto rtIt = widgetRuntimes_.find(id);
    if (rtIt == widgetRuntimes_.end())
        return;
    WidgetRuntime& rt = rtIt.value();
    if (rt.kind != 5 || rt.wheelSpinning)
        return;
    if (rt.wheelOptions.isEmpty()) {
        rt.wheelHint = QStringLiteral("无选项，点编辑添加");
        refreshWidgetItem(id);
        return;
    }
    const QSet<QString> exclude = rt.wheelDedup ? rt.wheelDrawn : QSet<QString>();
    const int idx = weightedPick(rt.wheelOptions, rt.wheelWeights, exclude);
    if (idx < 0) {
        rt.wheelHint = QStringLiteral("已抽完，点重置恢复");
        refreshWidgetItem(id);
        return;
    }
    QVector<double> startDeg, sweepDeg;
    wheelSectorAngles(rt.wheelWeights, startDeg, sweepDeg);
    const double mid = startDeg[idx] + sweepDeg[idx] / 2.0;
    // 目标角度：扇区中线转到顶部指针（angle ≡ −mid，mod 360）；额外至少 5 整圈
    double delta = std::fmod(-mid - rt.wheelAngle, 360.0);
    if (delta < 0.0)
        delta += 360.0;
    rt.wheelSpinFrom = rt.wheelAngle;
    rt.wheelSpinTo = rt.wheelAngle + 360.0 * 5.0 + delta;
    rt.wheelResultIdx = idx;
    rt.wheelSpinStartMs = QDateTime::currentMSecsSinceEpoch();
    rt.wheelSpinning = true;
    rt.wheelHint.clear();
    rt.wheelResult.clear();
    refreshWidgetItem(id);
    if (widgetTick_ && !widgetTick_->isActive())
        widgetTick_->start();
}

// 点名器开始滚动（右圆钮）：2 秒高速换名 → 定格加权抽 N 人（去重时排除已点；
// 结果加入已点集合）；空名单/已点完不启动并提示
void BoardView::nameStartRoll(const QString& id) {
    auto rtIt = widgetRuntimes_.find(id);
    if (rtIt == widgetRuntimes_.end())
        return;
    WidgetRuntime& rt = rtIt.value();
    if (rt.kind != 6 || rt.nameRolling)
        return;
    if (rt.nameOptions.isEmpty()) {
        rt.nameHint = QStringLiteral("名单为空，点编辑添加");
        refreshWidgetItem(id);
        return;
    }
    const QSet<QString> exclude = rt.nameDedup ? rt.nameDrawn : QSet<QString>();
    bool anyLeft = false;
    for (const QString& n : rt.nameOptions) {
        if (!exclude.contains(n)) {
            anyLeft = true;
            break;
        }
    }
    if (!anyLeft) {
        rt.nameHint = QStringLiteral("已点完，点重置恢复");
        refreshWidgetItem(id);
        return;
    }
    rt.nameRolling = true;
    rt.nameRollStartMs = QDateTime::currentMSecsSinceEpoch();
    rt.lastNameShuffleMs = 0;  // 首帧立即换名
    rt.nameRollCursor = 0;
    rt.nameHint.clear();
    rt.nameResult.clear();
    refreshWidgetItem(id);
    if (widgetTick_ && !widgetTick_->isActive())
        widgetTick_->start();
}

// 小工具控件命中（选择/套索工具：导图钮判定后、常规选择前调用）：
// 命中卡片控件 → 执行动作（重置/开始暂停/键盘/拨珠/骰子设置）并消费事件；
// 命中卡片非控件区域 → 返回 false，走常规选择/拖动（卡片其余区域可拖动移动）。
bool BoardView::tryWidgetButtonAction(const QPointF& scenePos) {
    for (auto it = elementItems_.constBegin(); it != elementItems_.constEnd(); ++it) {
        auto* card = dynamic_cast<WidgetCardItem*>(it.value());
        if (!card)
            continue;
        const QPointF local = card->mapFromScene(scenePos);
        if (!card->cardRect.contains(local))
            continue;
        return widgetControlPress(it.key(), local);
    }
    return false;
}

// 计时器设置态：滚轮调整时分秒（delta：+1 = 下滚/增值，−1 = 上滚/减值）。
// 命中卡片滚轮区 → 调整对应列（时 0~23 / 分秒 0~59，回卷，写入本地运行时）并刷新，
// 返回 true 消费事件（不缩放视图）；否则 false 走视图缩放。
bool BoardView::widgetWheelAdjust(const QPointF& scenePos, int delta) {
    for (auto it = elementItems_.constBegin(); it != elementItems_.constEnd(); ++it) {
        auto* card = dynamic_cast<WidgetCardItem*>(it.value());
        if (!card)
            continue;
        const QPointF local = card->mapFromScene(scenePos);
        if (!card->cardRect.contains(local))
            continue;
        const int col = card->wheelColumnAt(local);
        if (col < 0)
            return false;  // 卡片命中但非滚轮区（或非设置态）：不拦截
        const auto rtIt = widgetRuntimes_.find(it.key());
        if (rtIt == widgetRuntimes_.end())
            return false;
        WidgetRuntime& rt = rtIt.value();
        if (col == 0)
            rt.wheelH = ((rt.wheelH + delta) % 24 + 24) % 24;
        else if (col == 1)
            rt.wheelM = ((rt.wheelM + delta) % 60 + 60) % 60;
        else
            rt.wheelS = ((rt.wheelS + delta) % 60 + 60) % 60;
        refreshWidgetItem(it.key());
        return true;
    }
    return false;
}

// 小工具工具下点击已有卡片：命中控件 → 执行动作；卡片内其余区域也消费事件
// （不叠放新卡，可先调参数/点确定再继续放置）。返回是否命中已有卡片。
bool BoardView::widgetToolHitExisting(const QPointF& scenePos) {
    for (auto it = elementItems_.constBegin(); it != elementItems_.constEnd(); ++it) {
        auto* card = dynamic_cast<WidgetCardItem*>(it.value());
        if (!card)
            continue;
        const QPointF local = card->mapFromScene(scenePos);
        if (!card->cardRect.contains(local))
            continue;
        widgetControlPress(it.key(), local);  // 命中控件则执行动作
        return true;                          // 卡片内一律消费（防叠卡）
    }
    return false;
}

// 聚焦节点变化后重建导图 path（+ / × 钮与外扩高亮框的显示切换）
void BoardView::refreshMindMapPath(const QString& mapId) {
    QGraphicsPathItem* item = elementItems_.value(mapId, nullptr);
    if (!item || item->data(2).toString() != QLatin1String("mindmap"))
        return;
    const auto it = mindLayouts_.constFind(mapId);
    if (it == mindLayouts_.constEnd())
        return;  // 布局缓存缺失（元素刚被删）：不重建
    const QString focusNode = (mindFocusMap_ == mapId) ? mindFocusNode_ : QString();
    item->setPath(mindMapPathFromLayout(it.value(), focusNode, true));
}

// 设置节点聚焦（mapId/nodeId 均为空 = 清除聚焦）；旧图与新图 path 均重建
void BoardView::setMindFocus(const QString& mapId, const QString& nodeId) {
    const QString oldMap = mindFocusMap_;
    mindFocusMap_ = mapId;
    mindFocusNode_ = nodeId;
    if (!oldMap.isEmpty() && oldMap != mapId)
        refreshMindMapPath(oldMap);  // 旧图失去聚焦：撤 +/× 钮
    if (!mapId.isEmpty())
        refreshMindMapPath(mapId);   // 新图聚焦态：显示 +/× 钮
}

// 按元素类型分派构建（笔画/图形/表格/思维导图）
QGraphicsPathItem* BoardView::buildElementItem(const whiteboard::Element& e) {
    if (auto* s = dynamic_cast<const whiteboard::Stroke*>(&e))
        return buildStrokeItem(*s, s->id);
    if (auto* g = dynamic_cast<const whiteboard::GraphicElement*>(&e))
        return buildGraphicItem(*g, g->id);
    if (auto* t = dynamic_cast<const whiteboard::TableElement*>(&e))
        return buildTableItem(*t, t->id);
    if (auto* mm = dynamic_cast<const whiteboard::MindMapElement*>(&e))
        return buildMindMapItem(*mm, mm->id);
    if (auto* text = dynamic_cast<const whiteboard::TextElement*>(&e))
        return buildTextItem(*text, text->id);
    if (auto* w = dynamic_cast<const whiteboard::WidgetElement*>(&e))
        return buildWidgetItem(*w, w->id);
    return nullptr;
}

void BoardView::ensureEraserItem() {
    if (eraserPreview_)
        return;
    eraserPreview_ = new QGraphicsRectItem();
    eraserPreview_->setPen(Qt::NoPen);              // 白色方块：无边框
    eraserPreview_->setBrush(QColor(255, 255, 255, 240));
    eraserPreview_->setZValue(100);
    eraserPreview_->setVisible(false);
    scene_.addItem(eraserPreview_);
}

void BoardView::updateEraserPreview(const QPointF& center) {
    ensureEraserItem();
    eraserPreview_->setRect(QRectF(center.x() - kEraserSize / 2.0,
                                   center.y() - kEraserSize / 2.0,
                                   kEraserSize, kEraserSize));
    eraserPreview_->setVisible(true);
}

whiteboard::Point BoardView::toBoardPoint(const QPointF& p) const {
    return whiteboard::Point(qRound(p.x()), qRound(p.y()));
}

whiteboard::Rect BoardView::eraserRectAt(const QPointF& center) const {
    const int x = qRound(center.x() - kEraserSize / 2.0);
    const int y = qRound(center.y() - kEraserSize / 2.0);
    return whiteboard::Rect(x, y, kEraserSize, kEraserSize);
}

QPointF BoardView::clampToScene(const QPointF& p) const {
    const QRectF r = scene_.sceneRect();
    QPointF c = p;
    c.setX(qBound(r.left(), c.x(), r.right()));
    c.setY(qBound(r.top(), c.y(), r.bottom()));
    return c;
}

// ---------- 选择与变换 ----------

void BoardView::clearSelection() {
    if (selectionFrame_) {
        scene_.removeItem(selectionFrame_);
        delete selectionFrame_;
        selectionFrame_ = nullptr;
        // 广播空选择框：远端清除选择预览
        data_.SendToolPreview(3, std::to_string(sessionId_), {}, {});
    }
    selectedItems_.clear();
    dragOp_ = DragOp::None;
    if (!mindFocusMap_.isEmpty())
        setMindFocus(QString(), QString());  // 清聚焦：重建导图 path（撤 +/× 钮）
}

void BoardView::selectItems(const QList<QGraphicsPathItem*>& items) {
    // 相同集合且已有选择框则直接返回（拖动已选集合时保持选择框）
    if (selectionFrame_ && items == selectedItems_)
        return;
    clearSelection();
    selectedItems_ = items;
    selectedItems_.removeAll(nullptr);
    if (selectedItems_.isEmpty())
        return;
    selectionFrame_ = new SelectionFrame(selectedItems_);
    scene_.addItem(selectionFrame_);
    broadcastSelectionPreview();
}

void BoardView::syncSelectionFrame() {
    if (selectionFrame_)
        selectionFrame_->sync();
}

// 广播当前选择框（4 顶点 + 选中笔画 id）供远端实时预览；无选择时广播空 points。
void BoardView::broadcastSelectionPreview() {
    std::vector<whiteboard::Point> pts;
    std::vector<std::string> ids;
    if (selectionFrame_) {
        const QRectF r = selectionFrame_->rect();
        pts = {
            whiteboard::Point(qRound(r.left()), qRound(r.top())),
            whiteboard::Point(qRound(r.right()), qRound(r.top())),
            whiteboard::Point(qRound(r.right()), qRound(r.bottom())),
            whiteboard::Point(qRound(r.left()), qRound(r.bottom())),
        };
        ids.reserve(static_cast<size_t>(selectedItems_.size()));
        for (QGraphicsPathItem* it : selectedItems_) {
            const QVariant v = it ? it->data(0) : QVariant();
            if (v.isValid())
                ids.push_back(v.toString().toStdString());
        }
    }
    data_.SendToolPreview(3, std::to_string(sessionId_), pts, ids);
}

QGraphicsPathItem* BoardView::hitElementItem(const QPointF& scenePos) const {
    // 先按包围盒粗筛，再用笔宽描边后的形状精确判断：
    // QGraphicsPathItem::shape() 默认是裸 path（零宽度），精确点击才命中，
    // 斜线等任意角度笔画几乎点不中，故用 stroker 加宽做容差。
    // 图形闭合区域内部、表格外框内部直接命中（增强空白区域可点性）。
    const qreal r = kHitTolerance;
    const QList<QGraphicsItem*> items = scene_.items(
        QRectF(scenePos.x() - r, scenePos.y() - r, r * 2, r * 2),
        Qt::IntersectsItemBoundingRect);
    for (QGraphicsItem* it : items) {
        auto* pi = dynamic_cast<QGraphicsPathItem*>(it);
        if (!pi || !pi->data(0).isValid())
            continue;
        const QPointF local = pi->mapFromScene(scenePos);
        QPainterPathStroker stroker;
        stroker.setWidth(qMax(10.0, pi->pen().widthF() + 8));
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        if (stroker.createStroke(pi->path()).contains(local))
            return pi;
        const QString kind = pi->data(2).toString();
        if (kind == QLatin1String("graphic") && pi->path().contains(local))
            return pi;
        if (kind == QLatin1String("table") && pi->data(1).toRectF().contains(local))
            return pi;
        if (kind == QLatin1String("mindmap") && pi->data(1).toRectF().contains(local))
            return pi;  // 导图：布局包围盒内部直接命中
        if (kind == QLatin1String("text") &&
            (pi->path().contains(local) || pi->data(1).toRectF().contains(local)))
            return pi;  // 文字：字形填充区域或包围盒内部直接命中
        if (kind == QLatin1String("widget") &&
            (pi->path().contains(local) || pi->data(1).toRectF().contains(local)))
            return pi;  // 小工具卡片：圆角矩形区域内部直接命中
    }
    return nullptr;
}

// 导图钮命中（选择工具：手柄判定后、元素命中前调用）：聚焦节点的 +/× 钮优先，
// 其次任意导图的折叠钮；命中则触发对应数据层操作并消费事件。
bool BoardView::tryMindMapAction(const QPointF& scenePos) {
    const double tol = whiteboard::mind_layout_detail::kBtnRadius + 3;
    auto hitBtn = [&](const QPointF& local, const whiteboard::Point& c) {
        const double dx = local.x() - c.x;
        const double dy = local.y() - c.y;
        return dx * dx + dy * dy <= tol * tol;
    };
    // 1) 聚焦节点的 "+"（加子节点）/ "×"（删子树，根无 ×）钮
    if (!mindFocusMap_.isEmpty()) {
        QGraphicsPathItem* item = elementItems_.value(mindFocusMap_, nullptr);
        const auto it = mindLayouts_.constFind(mindFocusMap_);
        if (item && it != mindLayouts_.constEnd()) {
            const QPointF local = item->mapFromScene(scenePos);
            const whiteboard::MindNodeBox* box =
                it.value().FindNode(mindFocusNode_.toStdString());
            if (box) {
                if (hitBtn(local, whiteboard::MindAddBtnPos(*box))) {
                    data_.MindMapAddChild(mindFocusMap_.toStdString(),
                                          mindFocusNode_.toStdString());
                    return true;
                }
                if (box->depth > 0 && hitBtn(local, whiteboard::MindDelBtnPos(*box))) {
                    data_.MindMapRemoveNode(mindFocusMap_.toStdString(),
                                            mindFocusNode_.toStdString());
                    return true;
                }
            }
        }
    }
    // 2) 任意导图的折叠钮（展开 / 收缩子树）
    for (auto it = mindLayouts_.constBegin(); it != mindLayouts_.constEnd(); ++it) {
        QGraphicsPathItem* item = elementItems_.value(it.key(), nullptr);
        if (!item)
            continue;
        const QPointF local = item->mapFromScene(scenePos);
        const whiteboard::MindNodeBox* box =
            it.value().FindFoldBtn(qRound(local.x()), qRound(local.y()));
        if (box) {
            data_.MindMapToggleNode(it.key().toStdString(), box->nodeId);
            return true;
        }
    }
    return false;
}

// 命中图元后更新导图节点聚焦：命中节点框 → 聚焦（显示 +/× 钮）；
// 命中其他图元或导图非节点区域（连线/空白）→ 清除聚焦（重建 path 撤钮）
void BoardView::updateMindFocusOnHit(QGraphicsPathItem* item, const QPointF& scenePos) {
    const whiteboard::MindNodeBox* box = nullptr;
    if (item && item->data(2).toString() == QLatin1String("mindmap")) {
        const auto it = mindLayouts_.constFind(item->data(0).toString());
        if (it != mindLayouts_.constEnd()) {
            const QPointF local = item->mapFromScene(scenePos);
            box = it.value().FindByFrame(qRound(local.x()), qRound(local.y()));
        }
    }
    if (box) {
        setMindFocus(item->data(0).toString(), QString::fromStdString(box->nodeId));
    } else if (!mindFocusMap_.isEmpty()) {
        setMindFocus(QString(), QString());
    }
}

// 上溯最外层表格 id（id 本身不属于任何表格时返回空字符串）
QString BoardView::topTableOf(const QString& id) const {
    QString cur = id;
    while (cellOwner_.contains(cur)) {
        const QString owner = cellOwner_.value(cur);
        if (owner.isEmpty() || owner == cur)
            break;
        cur = owner;
    }
    return cur == id ? QString() : cur;
}

// 选择展开：命中表格内任何元素 → 整表（含全部后代）参与选中，保证变换一起烘焙；
// 其余元素原样返回。去重保持顺序稳定。
QList<QGraphicsPathItem*> BoardView::expandToWholeTables(
    const QList<QGraphicsPathItem*>& items) const {
    QList<QGraphicsPathItem*> out;
    QSet<QGraphicsPathItem*> seen;
    auto addItem = [&out, &seen](QGraphicsPathItem* it) {
        if (it && !seen.contains(it)) {
            seen.insert(it);
            out.append(it);
        }
    };
    for (QGraphicsPathItem* item : items) {
        if (!item)
            continue;
        const QString id = item->data(0).toString();
        QString tableId;
        if (item->data(2).toString() == QLatin1String("table"))
            tableId = id;
        else
            tableId = topTableOf(id);
        if (tableId.isEmpty()) {
            addItem(item);
            continue;
        }
        addItem(elementItems_.value(tableId, nullptr));
        const QStringList children = tableChildren_.value(tableId);
        for (const QString& childId : children)
            addItem(elementItems_.value(childId, nullptr));
    }
    return out;
}

// 删除选中元素：收集全部 id 交给数据层递归删除（表格级联内部笔迹）；
// 数据层 removed 回调驱动 applyElementChanges 精确移除图元。
void BoardView::deleteSelection() {
    if (selectedItems_.isEmpty())
        return;
    std::vector<std::string> ids;
    ids.reserve(static_cast<size_t>(selectedItems_.size()));
    for (QGraphicsPathItem* item : selectedItems_) {
        if (!item)
            continue;
        const QVariant v = item->data(0);
        if (v.isValid())
            ids.push_back(v.toString().toStdString());
    }
    if (!ids.empty())
        data_.RemoveElements(ids);
}

// ---------- 新增元素工具（Shape/Table 拖动预览；MindMap 点击即放置） ----------

void BoardView::updateToolDrawPreview(const QRectF& rect) {
    if (!toolDrawPreview_)
        return;
    const whiteboard::Rect rc(qRound(rect.x()), qRound(rect.y()),
                              qMax(1, qRound(rect.width())), qMax(1, qRound(rect.height())));
    QPainterPath path;
    if (tool_ == Tool::Table) {
        path = tableGridPath(rc, tableRows_, tableCols_);
    } else {
        for (const auto& sp : whiteboard::BuildShape(shapeKind_, rc)) {
            if (sp.points.empty())
                continue;
            path.moveTo(sp.points[0].x, sp.points[0].y);
            for (size_t i = 1; i < sp.points.size(); ++i)
                path.lineTo(sp.points[i].x, sp.points[i].y);
            if (sp.closed)
                path.closeSubpath();
        }
    }
    toolDrawPreview_->setPath(path);
}

// ---------- 文字内联编辑 ----------

// 开始编辑：existing 为空 = 在 scenePos 处新建（用面板字号与面板颜色）；
// 非空 = 编辑已有文字元素（原字形隐藏，编辑框叠加原位）。编辑框资源见 clearTextEditor。
void BoardView::beginTextEditing(const QPointF& scenePos, QGraphicsPathItem* existing) {
    if (textEditor_)
        return;  // 防御：同一时刻至多一个编辑框（调用方已先提交/取消）
    whiteboard::TextElement seed;  // 编辑参数载体（默认值仅作兜底）
    if (existing) {
        const QVariantList meta = existing->data(6).toList();
        if (meta.size() < 6)
            return;  // 防御：非文字图元
        seed.text = meta[0].toString().toStdString();
        seed.x = meta[1].toInt();
        seed.y = meta[2].toInt();
        seed.fontSize = meta[3].toInt();
        seed.rotation = meta[4].toFloat();
        seed.color = meta[5].toUInt();
        editingTextId_ = existing->data(0).toString();
        textEditHiddenItem_ = existing;
        existing->setVisible(false);  // 隐藏原字形：编辑框即所见（提交后按新数据重建）
    } else {
        seed.x = qRound(scenePos.x());
        seed.y = qRound(scenePos.y());
        seed.fontSize = textFontSize_;
        seed.color = textColor_;  // 新建文字颜色独立于笔色（文字面板选择）
    }
    // 编辑框锚点/朝向：格内文字锚点为格局部坐标，需经图元全变换映射到场景
    //（含表格旋转）；朝角取图元场景变换的旋转分量。页面级文字与旧行为一致。
    QPointF editorAnchor(seed.x, seed.y);
    qreal editorAngle = seed.rotation;
    if (existing && cellOwner_.contains(existing->data(0).toString())) {
        const QTransform st = existing->sceneTransform();
        editorAnchor = st.map(editorAnchor);
        editorAngle = qRadiansToDegrees(std::atan2(st.m12(), st.m11()));
    }
    textEditAnchor_ = editorAnchor;
    textEditColor_ = seed.color;

    auto* editor = new InlineTextEditor();
    // 背景取微量不透明度而非全透明：Windows 上 QGraphicsProxyWidget 对
    // 全透明 widget 的首帧快照不可靠（空编辑框仅光标会整体不呈现），
    // 极淡着色保证快照非空；边框改由场景图元 textEditFrame_ 独立绘制
    //（不依赖 widget 快照，任何状态下都稳定可见）
    editor->setStyleSheet(QStringLiteral(
        "QTextEdit { background: rgba(20, 24, 28, 36); color: %1; }")
        .arg(colorToQColor(seed.color).name()));
    editor->setAttribute(Qt::WA_TranslucentBackground);
    editor->setLineWrapMode(QTextEdit::NoWrap);  // 硬换行语义与渲染一致（不自动折行）
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->document()->setDocumentMargin(0);
    QFont editFont = textElementFont(seed.fontSize);
    editor->setFont(editFont);
    editor->setPlainText(QString::fromUtf8(seed.text.c_str()));
    if (existing && !seed.text.empty())
        editor->selectAll();  // 双击编辑：全选便于直接覆盖输入
    // 文档变化自调尺寸：宽度随最长行、高度随行数（NoWrap 下文档尺寸即内容尺寸）
    QObject::connect(editor->document(), &QTextDocument::contentsChanged, editor, [this, editor]() {
        const QSizeF docSize = editor->document()->size();
        editor->setFixedSize(qMax(60, qCeil(docSize.width()) + 10),
                             qMax(24, qCeil(docSize.height()) + 6));
        updateTextEditFrame();  // 编辑框自调尺寸后同步虚线框
    });
    {
        const QSizeF docSize = editor->document()->size();
        editor->setFixedSize(120, qMax(24, qCeil(docSize.height()) + 6));
    }
    editor->onSubmit = [this]() { commitTextEditing(); };
    editor->onCancel = [this]() { cancelTextEditing(); };

    // 编辑区虚线框：场景图元独立绘制（z=149 紧贴编辑框下方），空编辑框也稳定可见
    textEditFrame_ = new QGraphicsPathItem();
    {
        QPen framePen(QColor(255, 170, 80, 220), 1.0);
        framePen.setStyle(Qt::DashLine);
        textEditFrame_->setPen(framePen);
        textEditFrame_->setBrush(Qt::NoBrush);
    }
    textEditFrame_->setZValue(149);
    scene_.addItem(textEditFrame_);
    textEditFrame_->setPos(textEditAnchor_);
    if (!qFuzzyIsNull(editorAngle))
        textEditFrame_->setTransform(QTransform().rotate(editorAngle));

    textEditor_ = editor;
    textEditorProxy_ = scene_.addWidget(editor);
    textEditorProxy_->setPos(textEditAnchor_);
    textEditorProxy_->setZValue(150);
    if (!qFuzzyIsNull(editorAngle)) {
        // 旋转文字：编辑框绕锚点（= 代理局部原点）施加同一旋转，与隐藏字形原位对齐
        textEditorProxy_->setTransform(QTransform().rotate(editorAngle));
    }
    updateTextEditFrame();
    // widget 尺寸微变再复原：强制代理重抓首帧快照（空编辑框光标可靠呈现）
    {
        const QSize fixedNow = editor->size();
        editor->setFixedSize(fixedNow + QSize(1, 0));
        editor->setFixedSize(fixedNow);
    }
    // polish / 代理嵌入可能重置文档字体：最终同步一次（与渲染路径同源，所见即所得）
    editor->document()->setDefaultFont(editFont);
    textEditorProxy_->update();
    editor->setFocus(Qt::MouseFocusReason);
}

// 同步编辑框虚线框：框尺寸与编辑框一致（创建 / 内容变化时调用）
void BoardView::updateTextEditFrame() {
    if (!textEditFrame_ || !textEditor_)
        return;
    const QSizeF s = textEditor_->size();
    QPainterPath framePath;
    framePath.addRect(QRectF(0.5, 0.5, s.width() - 1.0, s.height() - 1.0));
    textEditFrame_->setPath(framePath);
}

// 提交编辑：非空文本落库（已有走 UpdateTextContent 重建；新建走 AddElement）；
// 空文本等同取消。提交前先恢复原图元可见（重建前过渡 + 空文本取消语义）。
void BoardView::commitTextEditing() {
    if (!textEditor_)
        return;
    auto* editor = static_cast<InlineTextEditor*>(textEditor_);
    const QString text = editor->toPlainText();
    const QString existingId = editingTextId_;
    QGraphicsPathItem* hidden = textEditHiddenItem_;
    const QPointF anchor = textEditAnchor_;
    const uint32_t color = textEditColor_;
    if (hidden)
        hidden->setVisible(true);
    clearTextEditor();
    if (text.isEmpty())
        return;  // 空文本等同取消（不产生/不修改元素）
    if (!existingId.isEmpty()) {
        const QVariantList meta = hidden ? hidden->data(6).toList() : QVariantList();
        const QString oldText = !meta.isEmpty() ? meta[0].toString() : QString();
        if (text == oldText)
            return;  // 内容未变：跳过写回（避免无谓的撤销步与重建）
        // 重算字形包围盒（布局依赖）：与 seed 同参数字体渲染；格内文字为格局部坐标
        whiteboard::TextElement nt;
        if (meta.size() >= 6) {
            nt.x = meta[1].toInt();
            nt.y = meta[2].toInt();
            nt.fontSize = meta[3].toInt();
            nt.rotation = meta[4].toFloat();
            nt.color = meta[5].toUInt();
        } else {
            nt.x = qRound(anchor.x());
            nt.y = qRound(anchor.y());
            nt.fontSize = textFontSize_;
            nt.color = color;
        }
        nt.text = text.toStdString();
        const QRectF br = textPath(nt).boundingRect();
        data_.UpdateTextContent(existingId.toStdString(), text.toStdString(),
                                whiteboard::Rect(qRound(br.x()), qRound(br.y()),
                                                 qRound(br.width()), qRound(br.height())));
        return;
    }
    auto t = std::make_shared<whiteboard::TextElement>();
    t->Reset();
    t->text = text.toStdString();
    t->x = qRound(anchor.x());
    t->y = qRound(anchor.y());
    t->fontSize = textFontSize_;
    t->color = color;
    {
        const QRectF br = textPath(*t).boundingRect();  // 字形包围盒（布局/入格依赖）
        t->bounds = whiteboard::Rect(qRound(br.x()), qRound(br.y()),
                                     qRound(br.width()), qRound(br.height()));
    }
    data_.AddElement(t);
    setTool(Tool::Select);  // 新建文字成功后自动切回选择（空文本取消 / 编辑已有文字不切换）
}

// 取消编辑：恢复原图元可见并移除编辑框（不写回数据）
void BoardView::cancelTextEditing() {
    if (!textEditor_)
        return;
    if (textEditHiddenItem_)
        textEditHiddenItem_->setVisible(true);
    clearTextEditor();
}

// 移除编辑框资源：抑制失焦重入 → 解除 widget 关联（调度 deleteLater）→ 删除代理 → 清状态
void BoardView::clearTextEditor() {
    if (textEditor_) {
        auto* editor = static_cast<InlineTextEditor*>(textEditor_);
        editor->closing = true;
        editor->onSubmit = nullptr;
        editor->onCancel = nullptr;
    }
    if (textEditorProxy_) {
        textEditorProxy_->setWidget(nullptr);  // 解除关联：widget 走 deleteLater
        if (textEditorProxy_->scene())
            scene_.removeItem(textEditorProxy_);
        delete textEditorProxy_;
        textEditorProxy_ = nullptr;
    }
    if (textEditFrame_) {
        if (textEditFrame_->scene())
            scene_.removeItem(textEditFrame_);
        delete textEditFrame_;
        textEditFrame_ = nullptr;
    }
    textEditor_ = nullptr;
    editingTextId_.clear();
    textEditHiddenItem_ = nullptr;
    textRouter_ = false;
}

// ---------- 转盘/点名器选项编辑弹窗（复用文字编辑机制：代理承载 + 虚线框 + 提示条） ----------

// 打开选项编辑弹窗（kind=5/6）：浮于卡片上方（固定尺寸）；载入当前原始选项文本；
// 旋转/滚动中不允许打开；与文字编辑互斥
void BoardView::beginWidgetOptionsEditing(const QString& id) {
    if (optionsEditor_)
        commitWidgetOptionsEditing();  // 防御：同一时刻至多一个弹窗
    if (textEditor_)
        commitTextEditing();           // 与文字编辑互斥
    auto rtIt = widgetRuntimes_.find(id);
    auto* card = dynamic_cast<WidgetCardItem*>(elementItems_.value(id, nullptr));
    if (!card || rtIt == widgetRuntimes_.end())
        return;
    WidgetRuntime& rt = rtIt.value();
    if (rt.kind != 5 && rt.kind != 6)
        return;
    if (rt.wheelSpinning || rt.nameRolling)
        return;  // 动画中不允许编辑
    const QString text = (rt.kind == 5) ? rt.wheelOptionsText : rt.nameOptionsText;

    const QRectF box = card->sceneBoundingRect();  // 卡片视觉包围盒（场景坐标）
    constexpr qreal kEditW = 360.0;
    constexpr qreal kEditH = 168.0;
    const QPointF pos(box.center().x() - kEditW / 2.0, box.top() - kEditH - 34.0);

    auto* editor = new OptionsEditor();
    editor->setStyleSheet(QStringLiteral(
        "QTextEdit { background: #232326; color: #E8E8EC; border: 1px solid #4A4A4E; }"));
    editor->setLineWrapMode(QTextEdit::NoWrap);  // 一行 = 一项（与解析语义一致）
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->document()->setDocumentMargin(8);
    QFont editFont(QStringLiteral("Microsoft YaHei UI"));
    editFont.setPixelSize(14);
    editor->setFont(editFont);
    editor->setPlainText(text);
    editor->moveCursor(QTextCursor::End);
    editor->setFixedSize(qRound(kEditW), qRound(kEditH));
    editor->document()->setDefaultFont(editFont);
    editor->onSubmit = [this]() { commitWidgetOptionsEditing(); };
    editor->onCancel = [this]() { cancelWidgetOptionsEditing(); };

    // 提示条（弹窗上方，浅灰文字）：权重语法与提交方式
    optionsEditHint_ = new QGraphicsPathItem();
    {
        QFont hintFont(QStringLiteral("Microsoft YaHei UI"));
        hintFont.setPixelSize(13);
        QPainterPath path;
        path.addText(0, 0, hintFont,
                     QStringLiteral("每行一项；「名称*3」权重 x3；Ctrl+Enter 提交"));
        optionsEditHint_->setPath(path);
        optionsEditHint_->setPen(Qt::NoPen);
        optionsEditHint_->setBrush(QColor(0xC8, 0xC8, 0xCC, 230));
    }
    optionsEditHint_->setZValue(151);
    scene_.addItem(optionsEditHint_);
    optionsEditHint_->setPos(pos.x() + 2.0, pos.y() - 10.0);

    // 弹窗橙色虚线框（场景图元：任何时刻稳定可见）
    optionsEditFrame_ = new QGraphicsPathItem();
    {
        QPen framePen(QColor(255, 170, 80, 220), 1.2);
        framePen.setStyle(Qt::DashLine);
        optionsEditFrame_->setPen(framePen);
        optionsEditFrame_->setBrush(Qt::NoBrush);
        QPainterPath framePath;
        framePath.addRect(QRectF(-1.5, -1.5, kEditW + 3.0, kEditH + 3.0));
        optionsEditFrame_->setPath(framePath);
    }
    optionsEditFrame_->setZValue(151);
    scene_.addItem(optionsEditFrame_);
    optionsEditFrame_->setPos(pos);

    optionsEditor_ = editor;
    optionsEditId_ = id;
    optionsEditorProxy_ = scene_.addWidget(editor);
    optionsEditorProxy_->setPos(pos);
    optionsEditorProxy_->setZValue(152);
    optionsEditorProxy_->update();
    editor->setFocus(Qt::MouseFocusReason);
}

// 提交选项编辑：非空且变更才写回（UpdateWidgetOptions 入撤销栈 + 重建广播）；
// 内容为空提交 = 取消（不写回）
void BoardView::commitWidgetOptionsEditing() {
    if (!optionsEditor_)
        return;
    auto* editor = static_cast<OptionsEditor*>(optionsEditor_);
    const QString text = editor->toPlainText();
    const QString id = optionsEditId_;
    QString oldText;
    const auto rtIt = widgetRuntimes_.constFind(id);
    if (rtIt != widgetRuntimes_.constEnd()) {
        oldText = (rtIt.value().kind == 5) ? rtIt.value().wheelOptionsText
                                           : rtIt.value().nameOptionsText;
    }
    clearOptionsEditor();
    if (text.trimmed().isEmpty())
        return;  // 空内容提交 = 取消
    if (text == oldText)
        return;  // 内容未变：跳过写回（避免无谓的撤销步与重建）
    data_.UpdateWidgetOptions(id.toStdString(), text.toStdString());
}

// 取消选项编辑：仅移除弹窗（不写回）
void BoardView::cancelWidgetOptionsEditing() {
    if (!optionsEditor_)
        return;
    clearOptionsEditor();
}

// 移除弹窗资源：抑制失焦重入 → 解除 widget 关联 → 删除代理/虚线框/提示条 → 清状态
void BoardView::clearOptionsEditor() {
    if (optionsEditor_) {
        auto* editor = static_cast<OptionsEditor*>(optionsEditor_);
        editor->closing = true;
        editor->onSubmit = nullptr;
        editor->onCancel = nullptr;
    }
    if (optionsEditorProxy_) {
        optionsEditorProxy_->setWidget(nullptr);  // 解除关联：widget 走 deleteLater
        if (optionsEditorProxy_->scene())
            scene_.removeItem(optionsEditorProxy_);
        delete optionsEditorProxy_;
        optionsEditorProxy_ = nullptr;
    }
    if (optionsEditFrame_) {
        if (optionsEditFrame_->scene())
            scene_.removeItem(optionsEditFrame_);
        delete optionsEditFrame_;
        optionsEditFrame_ = nullptr;
    }
    if (optionsEditHint_) {
        if (optionsEditHint_->scene())
            scene_.removeItem(optionsEditHint_);
        delete optionsEditHint_;
        optionsEditHint_ = nullptr;
    }
    optionsEditor_ = nullptr;
    optionsEditId_.clear();
    optionsRouter_ = false;
}

// 提交图形/表格：构建数据层元素并交给 QtBoardData（PushSnapshot + 广播 + 增量回调）
void BoardView::commitToolDraw(const QRectF& rect) {
    const whiteboard::Rect rc(qRound(rect.x()), qRound(rect.y()),
                              qMax(1, qRound(rect.width())), qMax(1, qRound(rect.height())));
    if (tool_ == Tool::Table) {
        auto table = std::make_shared<whiteboard::TableElement>();
        table->Reset();
        table->origin = whiteboard::Point(rc.x, rc.y);  // 网格左上角
        table->rows = tableRows_;
        table->cols = tableCols_;
        // 拖拽矩形均分 = 每格初始尺寸（=回缩下限）；整表宽高由布局从子元素推导
        table->minCellW = qMax(1.0f, static_cast<float>(rc.width) / qMax(1, table->cols));
        table->minCellH = qMax(1.0f, static_cast<float>(rc.height) / qMax(1, table->rows));
        table->cells.assign(static_cast<size_t>(table->rows * table->cols),
                            std::list<std::shared_ptr<whiteboard::Element>>());
        table->color = penColor_;
        data_.AddElement(table);
    } else {
        auto graphic = std::make_shared<whiteboard::GraphicElement>();
        graphic->Reset();
        graphic->kind = shapeKind_;
        graphic->width = penWidth_;
        graphic->color = penColor_;
        graphic->subpaths = whiteboard::BuildShape(shapeKind_, rc);
        graphic->Rebuild();
        data_.AddElement(graphic);
    }
}

void BoardView::cancelToolPreview() {
    toolDrawing_ = false;
    if (toolDrawPreview_) {
        if (toolDrawPreview_->scene())
            scene_.removeItem(toolDrawPreview_);
        delete toolDrawPreview_;
        toolDrawPreview_ = nullptr;
    }
}

void BoardView::beginSelectionDrag(SelectionFrame::Handle h,
                                   const QPointF& scenePos) {
    if (selectedItems_.isEmpty())
        return;
    dragStartScene_ = scenePos;
    dragStartTransforms_.clear();
    for (QGraphicsPathItem* it : selectedItems_)
        dragStartTransforms_.append(it->transform());
    dragOp_ = DragOp::None;

    const QRectF lr = SelectionFrame::itemRect(selectedItems_.first());
    QPointF anchorLocal;
    switch (h) {
        case SelectionFrame::TL:
            dragOp_ = DragOp::ScaleTL;
            anchorLocal = lr.bottomRight();
            break;
        case SelectionFrame::TR:
            dragOp_ = DragOp::ScaleTR;
            anchorLocal = lr.bottomLeft();
            break;
        case SelectionFrame::BL:
            dragOp_ = DragOp::ScaleBL;
            anchorLocal = lr.topRight();
            break;
        case SelectionFrame::BR:
            dragOp_ = DragOp::ScaleBR;
            anchorLocal = lr.topLeft();
            break;
        case SelectionFrame::L:
            dragOp_ = DragOp::ScaleL;
            anchorLocal = QPointF(lr.right(), lr.center().y());
            break;
        case SelectionFrame::R:
            dragOp_ = DragOp::ScaleR;
            anchorLocal = QPointF(lr.left(), lr.center().y());
            break;
        case SelectionFrame::T:
            dragOp_ = DragOp::ScaleT;
            anchorLocal = QPointF(lr.center().x(), lr.bottom());
            break;
        case SelectionFrame::B:
            dragOp_ = DragOp::ScaleB;
            anchorLocal = QPointF(lr.center().x(), lr.top());
            break;
        case SelectionFrame::Rotate:
            dragOp_ = DragOp::Rotate;
            rotateCenterScene_ = selectionFrame_->center();
            rotateStartAngle_ = std::atan2(scenePos.y() - rotateCenterScene_.y(),
                                           scenePos.x() - rotateCenterScene_.x());
            break;
        case SelectionFrame::Delete:
            dragOp_ = DragOp::None;  // 删除按钮在 mousePress 阶段处理，不会进入拖动
            break;
        case SelectionFrame::None:
        case SelectionFrame::HandleCount:
            dragOp_ = DragOp::Move;  // 框内/图元上按下 = 移动整个选中集合
            break;
    }
    if (dragOp_ == DragOp::None || dragOp_ == DragOp::Move || dragOp_ == DragOp::Rotate)
        return;
    if (selectedItems_.size() == 1) {
        // 单选：锚点 = 局部角点经当前变换映射到场景（旋转后为真实角点位置，非 AABB 角）
        anchorScene_ = dragStartTransforms_.first().map(anchorLocal);
    } else {
        // 多选：锚点 = 集合并集 AABB 的对应角/边中点（场景坐标）
        const QRectF& ur = selectionFrame_->rect();
        switch (dragOp_) {
            case DragOp::ScaleTL: anchorScene_ = ur.bottomRight(); break;
            case DragOp::ScaleTR: anchorScene_ = ur.bottomLeft(); break;
            case DragOp::ScaleBL: anchorScene_ = ur.topRight(); break;
            case DragOp::ScaleBR: anchorScene_ = ur.topLeft(); break;
            case DragOp::ScaleL: anchorScene_ = QPointF(ur.right(), ur.center().y()); break;
            case DragOp::ScaleR: anchorScene_ = QPointF(ur.left(), ur.center().y()); break;
            case DragOp::ScaleT: anchorScene_ = QPointF(ur.center().x(), ur.bottom()); break;
            case DragOp::ScaleB: anchorScene_ = QPointF(ur.center().x(), ur.top()); break;
            default: break;
        }
        dragStartUnionRect_ = ur;
    }
}

void BoardView::updateSelectionDrag(const QPointF& scenePos) {
    if (dragOp_ == DragOp::None || selectedItems_.isEmpty())
        return;

    const QPointF delta = scenePos - dragStartScene_;
    QTransform m;

    if (dragOp_ == DragOp::Move) {
        m = QTransform::fromTranslate(delta.x(), delta.y());
    } else if (dragOp_ == DragOp::Rotate) {
        const qreal cur = std::atan2(scenePos.y() - rotateCenterScene_.y(),
                                     scenePos.x() - rotateCenterScene_.x());
        const qreal angle = qRadiansToDegrees(cur - rotateStartAngle_);
        m = QTransform()
                .translate(rotateCenterScene_.x(), rotateCenterScene_.y())
                .rotate(angle)
                .translate(-rotateCenterScene_.x(), -rotateCenterScene_.y());
    } else {
        // 缩放：单选走局部精确投影，多选基于并集 AABB 场景坐标
        qreal sx = 1.0;
        qreal sy = 1.0;
        if (selectedItems_.size() == 1) {
            const QTransform& t = dragStartTransforms_.first();
            const QPointF localDelta = t.inverted().map(delta);
            const QRectF lr = SelectionFrame::itemRect(selectedItems_.first());
            const qreal w = lr.width();
            const qreal h = lr.height();
            switch (dragOp_) {
                case DragOp::ScaleTL: sx = (w - localDelta.x()) / w; sy = (h - localDelta.y()) / h; break;
                case DragOp::ScaleTR: sx = (w + localDelta.x()) / w; sy = (h - localDelta.y()) / h; break;
                case DragOp::ScaleBL: sx = (w - localDelta.x()) / w; sy = (h + localDelta.y()) / h; break;
                case DragOp::ScaleBR: sx = (w + localDelta.x()) / w; sy = (h + localDelta.y()) / h; break;
                case DragOp::ScaleL: sx = (w - localDelta.x()) / w; break;
                case DragOp::ScaleR: sx = (w + localDelta.x()) / w; break;
                case DragOp::ScaleT: sy = (h - localDelta.y()) / h; break;
                case DragOp::ScaleB: sy = (h + localDelta.y()) / h; break;
                default: return;
            }
            if (w <= 0)
                sx = 1.0;
            if (h <= 0)
                sy = 1.0;
        } else {
            const qreal w = dragStartUnionRect_.width();
            const qreal h = dragStartUnionRect_.height();
            switch (dragOp_) {
                case DragOp::ScaleTL: sx = (w - delta.x()) / w; sy = (h - delta.y()) / h; break;
                case DragOp::ScaleTR: sx = (w + delta.x()) / w; sy = (h - delta.y()) / h; break;
                case DragOp::ScaleBL: sx = (w - delta.x()) / w; sy = (h + delta.y()) / h; break;
                case DragOp::ScaleBR: sx = (w + delta.x()) / w; sy = (h + delta.y()) / h; break;
                case DragOp::ScaleL: sx = (w - delta.x()) / w; break;
                case DragOp::ScaleR: sx = (w + delta.x()) / w; break;
                case DragOp::ScaleT: sy = (h - delta.y()) / h; break;
                case DragOp::ScaleB: sy = (h + delta.y()) / h; break;
                default: return;
            }
            if (w <= 0)
                sx = 1.0;
            if (h <= 0)
                sy = 1.0;
        }
        sx = qMax(0.05, sx);
        sy = qMax(0.05, sy);
        // 锚点固定（beginSelectionDrag 时算好的场景坐标），仅缩放
        m = QTransform()
                .translate(anchorScene_.x(), anchorScene_.y())
                .scale(sx, sy)
                .translate(-anchorScene_.x(), -anchorScene_.y());
    }

    // Qt 组合语义：A * B 表示"先施加 A 再施加 B"（行向量约定 p' = p·A·B）。
    // 意图是"先把图元从数据态摆到拖拽起始视觉态（dragStart），再施加场景增量 m"，
    // 故写 dragStart * m；写成 m * dragStart 会变成"先 m 后 drag"——θ=0 时可交换
    // 不暴露问题，θ≠0 时旋转后的移动/缩放/二次旋转全部错位。
    for (int i = 0; i < selectedItems_.size(); ++i)
        selectedItems_[i]->setTransform(dragStartTransforms_[i] * m);
    syncSelectionFrame();
    broadcastSelectionPreview();
}

void BoardView::endSelectionDrag() {
    if (dragOp_ == DragOp::None)
        return;
    // 仅当拖动实际改变了变换才烘焙（纯点击/选中不产生快照与同步）
    bool dirty = false;
    const int n = qMin(selectedItems_.size(), dragStartTransforms_.size());
    for (int i = 0; i < n; ++i) {
        if (selectedItems_[i] && selectedItems_[i]->transform() != dragStartTransforms_[i]) {
            dirty = true;
            break;
        }
    }
    if (dirty)
        bakeTransformToData();
    dragOp_ = DragOp::None;
    broadcastSelectionPreview();  // 烘焙后选择框同步到远端
}

// 变换烘焙：每个选中元素的新几何写回数据层（id 不变），UI 图元复位/重建。
// 表格整体处理：先预收集条目（重建会删除格子图元，不能边遍历边解引用旧指针），
// 格内子元素由所属表格统一换算格局部几何并整表重建后跳过。
// 分派：table → origin+rotation(+子元素缩放)；mindmap → root+scale+rotation；
// text → 锚点+字号+rotation；graphic → subpaths；stroke → 点集。
void BoardView::bakeTransformToData() {
    // 预收集条目（表格重建会删除格子图元 → 缓存 id/kind/归属避免悬空解引用）
    struct Entry {
        QGraphicsPathItem* item = nullptr;
        QString id;
        QString kind;
        bool isChild = false;  // 格内子元素：由所属表格整体处理
    };
    QVector<Entry> entries;
    for (QGraphicsPathItem* item : selectedItems_) {
        if (!item)
            continue;
        const QVariant v = item->data(0);
        if (!v.isValid())
            continue;
        Entry e;
        e.item = item;
        e.id = v.toString();
        e.kind = item->data(2).toString();
        e.isChild = cellOwner_.contains(e.id);
        entries.append(e);
    }

    // 撤销粒度：整次烘焙（表格 + 其余元素等多条 Update*）合并为单个撤销步
    data_.BeginBatch();
    for (const Entry& entry : entries) {
        QGraphicsPathItem* item = entry.item;
        if (entry.isChild)
            continue;  // 格内子元素：由所属表格（下方 table 分支）统一处理
        const QTransform tf = item->transform();
        if (tf.isIdentity())
            continue;
        const std::string idStr = entry.id.toStdString();
        const QString& kind = entry.kind;

        if (kind == QLatin1String("table")) {
            // 表格：布局外框四角经 tf 映射 → 新 origin/朝向角（rotation）与逐轴缩放比 s。
            // 纯移动/旋转仅写 origin/rotation（子元素数据不变，重建后视觉恒等）；
            // 含缩放时按 s 逐轴缩放各子元素格局部几何 + minCell。
            const auto layIt = tableLayouts_.constFind(entry.id);
            if (layIt == tableLayouts_.constEnd())
                continue;  // 布局缓存缺失（防御）
            const whiteboard::TableElement::Layout& layout = layIt.value();
            const QRectF r0 = item->data(1).toRectF();
            if (r0.width() < 1.0 || r0.height() < 1.0)
                continue;
            const QPointF c0 = tf.map(r0.topLeft());
            const QPointF c1 = tf.map(r0.topRight());
            const QPointF c3 = tf.map(r0.bottomLeft());
            const QPointF c2 = tf.map(r0.bottomRight());
            const qreal w = std::hypot(c1.x() - c0.x(), c1.y() - c0.y());
            const qreal h = std::hypot(c3.x() - c0.x(), c3.y() - c0.y());
            if (w < 1.0 || h < 1.0)
                continue;  // 退化防御（缩放至 0 附近）
            const qreal angleDeg = qRadiansToDegrees(
                std::atan2(c1.y() - c0.y(), c1.x() - c0.x()));
            const qreal sx = w / r0.width();
            const qreal sy = h / r0.height();
            // 新 origin：视觉中心 (c0+c2)/2 = tf 映射后的旧布局中心（平行四边形
            // 对角中点恒为中心），再回退新宽高的一半（与旧 bounds 角点算法同构）
            const QPointF visCenter((c0.x() + c2.x()) / 2.0, (c0.y() + c2.y()) / 2.0);
            const QPointF originNew(visCenter.x() - w / 2.0, visCenter.y() - h / 2.0);

            auto snapshot = data_.GetElementSnapshot(idStr);
            auto* table = dynamic_cast<whiteboard::TableElement*>(snapshot.get());
            if (!table)
                continue;
            table->origin = whiteboard::Point(qRound(originNew.x()), qRound(originNew.y()));
            table->rotation = static_cast<float>(angleDeg);
            const bool scaled = std::abs(sx - 1.0) > 1e-3 || std::abs(sy - 1.0) > 1e-3;
            if (scaled) {
                // 子元素格局部几何换算：p' = R'⁻¹(tf(V_old(p))) − c_new，
                // V_old(p) = 旧格原点 + p（旧表帧），c_new = O' + s∘(旧格原点)。
                // 行列位置按 s 近似同步缩放（非等比+旋转混合为近似，消除视觉跳变）
                const QTransform invNew = QTransform()
                        .translate(visCenter.x(), visCenter.y())
                        .rotate(-angleDeg)
                        .translate(-visCenter.x(), -visCenter.y());
                for (size_t ci = 0; ci < table->cells.size(); ++ci) {
                    double eox = 0.0;
                    double eoy = 0.0;
                    layout.CellOrigin(static_cast<int>(ci), eox, eoy);
                    const double cellOldX = r0.x() + eox;
                    const double cellOldY = r0.y() + eoy;
                    const double cellNewX = originNew.x() + sx * eox;
                    const double cellNewY = originNew.y() + sy * eoy;
                    auto mapPt = [&](const whiteboard::Point& p) {
                        const QPointF v = invNew.map(
                            tf.map(QPointF(cellOldX + p.x, cellOldY + p.y)));
                        return whiteboard::Point(qRound(v.x() - cellNewX),
                                                 qRound(v.y() - cellNewY));
                    };
                    for (auto& child : table->cells[ci]) {
                        if (!child)
                            continue;
                        if (auto* stroke = dynamic_cast<whiteboard::Stroke*>(child.get())) {
                            for (auto& p : stroke->points)
                                p = mapPt(p);
                            for (auto& p : stroke->rawPoints)
                                p = mapPt(p);
                            stroke->bounding = whiteboard::BoundaryRect();
                            for (const auto& p : stroke->points)
                                stroke->bounding.Update(p.x, p.y);
                        } else if (auto* graphic =
                                       dynamic_cast<whiteboard::GraphicElement*>(child.get())) {
                            for (auto& sp : graphic->subpaths) {
                                for (auto& p : sp.points)
                                    p = mapPt(p);
                            }
                            graphic->Rebuild();
                        } else if (auto* text =
                                       dynamic_cast<whiteboard::TextElement*>(child.get())) {
                            const whiteboard::Point anchor =
                                mapPt(whiteboard::Point(text->x, text->y));
                            text->x = anchor.x;
                            text->y = anchor.y;
                            text->fontSize =
                                qMax(4, qRound(text->fontSize * (sx + sy) / 2.0));
                            const QRectF br = textPath(*text).boundingRect();
                            text->bounds = whiteboard::Rect(qRound(br.x()), qRound(br.y()),
                                                            qRound(br.width()),
                                                            qRound(br.height()));
                        }
                        // 其余类型（导图/小工具）不入格：忽略
                    }
                }
                table->minCellW = static_cast<float>(table->minCellW * sx);
                table->minCellH = static_cast<float>(table->minCellH * sy);
            }
            // 静默写回整表 + 本地整表重建（按快照重排布局；选中态由 rebuildTable 恢复）
            // 别名构造：shared_ptr<TableElement> 与快照共享所有权（dynamic_cast 结果复用）
            data_.UpdateTableElement(
                std::shared_ptr<whiteboard::TableElement>(snapshot, table));
            rebuildTable(entry.id);
        } else if (kind == QLatin1String("mindmap")) {
            // 导图：tf 映射布局包围盒四角求横纵缩放比与朝向角；新 root = tf.map(旧 root)；
            // 新 scale = 旧 scale × 映射比例。path 为"数据态几何"（root 平移 + scale 已乘入），
            // 取 combined = tf ∘ D1⁻¹（D1 = 绕新 root 旋转 θ'）使 D1(combined(path)) == tf(path)
            // 视觉零跳变（旋转 + 非等比缩放混合时与表格同级近似）。
            const QRectF r0 = item->data(1).toRectF();
            const QVariantList geo = item->data(5).toList();
            if (geo.size() < 4 || r0.width() < 1.0 || r0.height() < 1.0)
                continue;  // 退化防御
            const QPointF root0(geo[0].toDouble(), geo[1].toDouble());
            const double sx0 = geo[2].toDouble();
            const double sy0 = geo[3].toDouble();
            const QPointF p0 = tf.map(r0.topLeft());
            const QPointF p1 = tf.map(r0.topRight());
            const QPointF p3 = tf.map(r0.bottomLeft());
            const qreal wRatio = std::hypot(p1.x() - p0.x(), p1.y() - p0.y()) / r0.width();
            const qreal hRatio = std::hypot(p3.x() - p0.x(), p3.y() - p0.y()) / r0.height();
            if (wRatio < 1e-3 || hRatio < 1e-3)
                continue;  // 缩放到 0 附近：放弃写回
            const qreal angleDeg = qRadiansToDegrees(std::atan2(p1.y() - p0.y(), p1.x() - p0.x()));
            const QPointF newRoot = tf.map(root0);
            const double newSx = sx0 * wRatio;
            const double newSy = sy0 * hRatio;
            data_.UpdateMindMapGeometry(idStr,
                                        whiteboard::Point(qRound(newRoot.x()), qRound(newRoot.y())),
                                        static_cast<float>(newSx), static_cast<float>(newSy),
                                        static_cast<float>(angleDeg));
            // UI 复位：path = combined.map(旧 path)，绕新 root 的 rotation 归一为 item transform
            const QTransform d1 = QTransform().translate(newRoot.x(), newRoot.y()).rotate(angleDeg)
                                      .translate(-newRoot.x(), -newRoot.y());
            const QTransform combined = tf * d1.inverted();
            item->setPath(combined.map(item->path()));
            item->setTransform(qFuzzyIsNull(angleDeg) ? QTransform() : d1);
            QRectF nbRect = combined.mapRect(r0);
            if (nbRect.width() <= 0)
                nbRect.setWidth(1);
            if (nbRect.height() <= 0)
                nbRect.setHeight(1);
            item->setData(1, nbRect);
            QVariantList geoNew;
            geoNew << newRoot.x() << newRoot.y() << newSx << newSy;
            item->setData(5, geoNew);
            // 布局缓存几何同步映射：命中判定与钮位置立即生效（视觉零跳变）
            const auto layoutIt = mindLayouts_.find(QString::fromStdString(idStr));
            if (layoutIt != mindLayouts_.end()) {
                whiteboard::MindLayout& layout = layoutIt.value();
                for (auto& b : layout.boxes) {
                    const QRectF fr = combined.mapRect(
                        QRectF(b.frame.x, b.frame.y, b.frame.width, b.frame.height));
                    b.frame = whiteboard::Rect(qRound(fr.x()), qRound(fr.y()),
                                               qMax(1, qRound(fr.width())), qMax(1, qRound(fr.height())));
                    const QPointF fb = combined.map(QPointF(b.foldBtn.x, b.foldBtn.y));
                    b.foldBtn = whiteboard::Point(qRound(fb.x()), qRound(fb.y()));
                }
                for (auto& lk : layout.links) {
                    const QPointF a = combined.map(QPointF(lk.a.x, lk.a.y));
                    const QPointF b2 = combined.map(QPointF(lk.b.x, lk.b.y));
                    lk.a = whiteboard::Point(qRound(a.x()), qRound(a.y()));
                    lk.b = whiteboard::Point(qRound(b2.x()), qRound(b2.y()));
                }
                layout.bounds = whiteboard::Rect(qRound(nbRect.x()), qRound(nbRect.y()),
                                                 qMax(1, qRound(nbRect.width())),
                                                 qMax(1, qRound(nbRect.height())));
            }
        } else if (kind == QLatin1String("text")) {
            // 文字：锚点经 tf 映射为新锚点；字号按横纵缩放均值（等比精确，非等比
            // 取均值近似——文字以字号语义优先，保持字形清晰）；朝向角 = tf 旋转分量。
            // UI 用新参数重建字形 path，变换归一为绕新锚点的 rotation。
            const QVariantList meta = item->data(6).toList();
            if (meta.size() < 6)
                continue;  // 退化防御
            const QPointF anchor0(meta[1].toInt(), meta[2].toInt());
            const int fontSize0 = meta[3].toInt();
            const qreal sx = std::hypot(tf.m11(), tf.m12());
            const qreal sy = std::hypot(tf.m21(), tf.m22());
            const qreal angleDeg = qRadiansToDegrees(std::atan2(tf.m12(), tf.m11()));
            if (sx < 1e-3 || sy < 1e-3)
                continue;  // 缩放到 0 附近：放弃写回
            const QPointF anchorN = tf.map(anchor0);
            const int fontSizeN = qMax(4, qRound(fontSize0 * (sx + sy) / 2.0));
            data_.UpdateTextGeometry(idStr, qRound(anchorN.x()), qRound(anchorN.y()),
                                     fontSizeN, static_cast<float>(angleDeg));
            whiteboard::TextElement nt;
            nt.text = meta[0].toString().toStdString();
            nt.x = qRound(anchorN.x());
            nt.y = qRound(anchorN.y());
            nt.fontSize = fontSizeN;
            nt.rotation = static_cast<float>(angleDeg);
            nt.color = meta[5].toUInt();
            item->setPath(textPath(nt));
            QRectF tb = item->path().boundingRect();
            if (tb.width() <= 0)
                tb.setWidth(1);
            if (tb.height() <= 0)
                tb.setHeight(1);
            item->setData(1, tb);
            QVariantList metaNew;
            metaNew << meta[0] << nt.x << nt.y << nt.fontSize << nt.rotation << meta[5];
            item->setData(6, metaNew);
            if (!qFuzzyIsNull(angleDeg)) {
                item->setTransform(QTransform().translate(nt.x, nt.y).rotate(angleDeg)
                                       .translate(-nt.x, -nt.y));
            } else {
                item->setTransform(QTransform());
            }
        } else if (kind == QLatin1String("widget")) {
            // 小工具：卡片左上角经 tf 映射为新位置；缩放取 tf 横纵缩放均值（等比，
            // 限制 0.5~3 倍；旋转分量忽略——卡片不支持旋转）。UI 按新位置/缩放
            // 重建 path，变换归一为恒等。
            const QVariantList meta = item->data(4).toList();
            if (meta.size() < 5)
                continue;  // 退化防御
            const qreal s0 = meta[4].toReal();
            const qreal sx = std::hypot(tf.m11(), tf.m12());
            const qreal sy = std::hypot(tf.m21(), tf.m22());
            if (sx < 1e-3 || sy < 1e-3)
                continue;  // 缩放到 0 附近：放弃写回
            const qreal sN = qBound(kWidgetMinScale, s0 * (sx + sy) / 2.0, kWidgetMaxScale);
            const QPointF tlN = tf.map(QPointF(meta[1].toInt(), meta[2].toInt()));
            const int nx = qRound(tlN.x());
            const int ny = qRound(tlN.y());
            data_.UpdateWidgetGeometry(idStr, nx, ny, static_cast<float>(sN));
            if (auto* card = dynamic_cast<WidgetCardItem*>(item)) {
                card->cardRect = QRectF(nx, ny, kWidgetCardW * sN, kWidgetCardH * sN);
                item->setPath(widgetCardPath(nx, ny, sN));
                item->setTransform(QTransform());
                item->setData(1, card->cardRect);
                QVariantList metaNew;
                metaNew << meta[0] << nx << ny << meta[3] << sN;
                item->setData(4, metaNew);
            }
        } else if (kind == QLatin1String("graphic")) {
            // 图形：映射后按 MoveTo 分组重建 subpaths（closed 标记取自 data(3)）
            const QPainterPath mapped = tf.map(item->path());
            const QVariantList closedList = item->data(3).toList();
            std::vector<whiteboard::Subpath> subpaths;
            int subIdx = -1;
            for (int i = 0; i < mapped.elementCount(); ++i) {
                const QPainterPath::Element& el = mapped.elementAt(i);
                if (el.type == QPainterPath::MoveToElement) {
                    subpaths.push_back(whiteboard::Subpath());
                    ++subIdx;
                    if (subIdx < closedList.size())
                        subpaths[subIdx].closed = closedList[subIdx].toBool();
                    subpaths[subIdx].points.push_back(
                        whiteboard::Point(qRound(el.x), qRound(el.y)));
                } else if (el.type == QPainterPath::LineToElement && subIdx >= 0) {
                    subpaths[subIdx].points.push_back(
                        whiteboard::Point(qRound(el.x), qRound(el.y)));
                }
            }
            // 闭合子路径：Qt closeSubpath 补的回环点映射后与首点重合，剔除
            for (auto& sp : subpaths) {
                if (sp.closed && sp.points.size() >= 2) {
                    const whiteboard::Point& f = sp.points.front();
                    const whiteboard::Point& l = sp.points.back();
                    if (f.x == l.x && f.y == l.y)
                        sp.points.pop_back();
                }
            }
            bool any = false;
            for (const auto& sp : subpaths)
                any = any || !sp.points.empty();
            if (!any)
                continue;
            data_.UpdateGraphicGeometry(idStr, subpaths);
            item->setPath(mapped);
            item->setTransform(QTransform());
            QRectF mr = mapped.boundingRect();
            if (mr.width() <= 0)
                mr.setWidth(1);
            if (mr.height() <= 0)
                mr.setHeight(1);
            item->setData(1, mr);
        } else {
            // 笔画：映射后重建点集写回数据层（id 不变）
            const QPainterPath mapped = tf.map(item->path());
            std::vector<whiteboard::Point> pts;
            pts.reserve(static_cast<size_t>(mapped.elementCount()));
            for (int i = 0; i < mapped.elementCount(); ++i) {
                const QPainterPath::Element& el = mapped.elementAt(i);
                if (el.type != QPainterPath::MoveToElement &&
                    el.type != QPainterPath::LineToElement)
                    continue;  // 源 path 无曲线，防御
                pts.push_back(whiteboard::Point(qRound(el.x), qRound(el.y)));
            }
            if (pts.size() < 2)
                continue;

            data_.UpdateStroke(idStr, pts);
            item->setPath(mapped);
            item->setTransform(QTransform());
            QRectF mappedRect = mapped.boundingRect();
            if (mappedRect.width() <= 0)
                mappedRect.setWidth(1);
            if (mappedRect.height() <= 0)
                mappedRect.setHeight(1);
            item->setData(1, mappedRect);
        }
    }
    data_.EndBatch();
    syncSelectionFrame();
}

QRectF BoardView::strokeSceneRect(const QGraphicsPathItem* item) const {
    return item->sceneTransform().mapRect(SelectionFrame::itemRect(item));
}

// 笔迹的真实形状（场景坐标）：对 path 做笔宽描边，
// 命中判定基于描边后的闭合形状而非零宽度裸 path。
QPainterPath BoardView::strokedScenePath(const QGraphicsPathItem* item) const {
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(6.0, item->pen().widthF() + 4));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(item->sceneTransform().map(item->path()));
}

// 精判：矩形与笔迹相交或笔迹被矩形完全包含
bool BoardView::pathHitsRect(const QGraphicsPathItem* item, const QRectF& rect) const {
    const QString kind = item->data(2).toString();
    if (kind == QLatin1String("text") || kind == QLatin1String("widget")) {
        // 文字/小工具为填充路径（无 pen 描边，strokedScenePath 不适用）：
        // 包围盒相交粗筛 + 场景化路径与矩形真实相交（路径含矩形边界时恒真，覆盖完全包含）
        const QPainterPath p = item->sceneTransform().map(item->path());
        return rect.intersects(p.boundingRect()) && p.intersects(rect);
    }
    return strokedScenePath(item).intersects(rect);
}

// 精判：套索闭合路径与笔迹相交，或笔迹完全被套索圈住
bool BoardView::pathHitsLasso(const QGraphicsPathItem* item,
                              const QPainterPath& lasso) const {
    const QString kind = item->data(2).toString();
    if (kind == QLatin1String("text") || kind == QLatin1String("widget")) {
        // 文字/小工具：填充路径与套索相交，或包围盒被套索完全包含
        const QPainterPath p = item->sceneTransform().map(item->path());
        return lasso.intersects(p) || lasso.contains(p.boundingRect());
    }
    const QPainterPath shape = strokedScenePath(item);
    if (lasso.intersects(shape))
        return true;
    // 笔迹完全在套索内部（无边界相交）时用点包含兜底
    const QPainterPath p = item->sceneTransform().map(item->path());
    if (p.elementCount() > 0) {
        const QPainterPath::Element& el = p.elementAt(0);
        if (lasso.contains(QPointF(el.x, el.y)))
            return true;
    }
    return false;
}

QPen BoardView::rubberPen() {
    return QPen(QColor(120, 190, 255), 1, Qt::DashLine);
}

void BoardView::beginRubberBand(const QPointF& scenePos) {
    clearSelection();
    rubberStartScene_ = scenePos;
    dragOp_ = DragOp::RubberBand;
    // 防御：橡皮筋可能已被 scene_.clear() 删除（清空/切页/撤销），指针悬空需重建
    if (!rubberBand_ || !rubberBand_->scene()) {
        rubberBand_ = new QGraphicsRectItem();
        rubberBand_->setPen(rubberPen());
        rubberBand_->setBrush(QColor(120, 190, 255, 32));
        rubberBand_->setZValue(50);
        scene_.addItem(rubberBand_);
    }
    rubberBand_->setRect(QRectF(scenePos, scenePos));
    rubberBand_->setVisible(true);
}

void BoardView::updateRubberBand(const QPointF& scenePos) {
    if (!rubberBand_)
        return;
    rubberBand_->setRect(QRectF(rubberStartScene_, scenePos).normalized());
}

// 框选结束：两阶段判定——
// 1) 橡皮筋矩形与外接矩形相交（粗筛）；2) 矩形与笔迹真实相交/包含（精判）。
void BoardView::finishRubberBand() {
    if (rubberBand_)
        rubberBand_->setVisible(false);
    dragOp_ = DragOp::None;

    if (rubberBand_) {
        QRectF r = rubberBand_->rect().normalized();
        // 纯水平/垂直拖动生成的零尺寸框对 QRectF::intersects 恒不命中，做 1px 保底
        if (r.width() <= 0)
            r.setWidth(1);
        if (r.height() <= 0)
            r.setHeight(1);
        QList<QGraphicsPathItem*> hits;
        for (QGraphicsPathItem* item : elementItems_) {
            if (!r.intersects(strokeSceneRect(item)))
                continue;  // 粗筛：外接矩形不相交
            if (!pathHitsRect(item, r))
                continue;  // 精判：笔迹与矩形无真实交集
            hits.append(item);
        }
        if (!hits.isEmpty())
            selectItems(expandToWholeTables(hits));  // 表格内元素命中时展开为整表
    }
}

// ---------- 套索选择（浅蓝虚线笔迹 + 两阶段相交判定） ----------

void BoardView::beginLasso(const QPointF& scenePos) {
    clearSelection();
    lassoPoints_.clear();
    lassoPoints_.append(scenePos);
    lassoLastSample_ = scenePos;
    dragOp_ = DragOp::Lasso;
    // 广播套索起点（远端实时显示套索路径）
    sessionId_++;
    data_.SendToolPreview(2, std::to_string(sessionId_),
                          { whiteboard::Point(qRound(scenePos.x()), qRound(scenePos.y())) }, {});
    // 防御：预览图元可能已被 scene_.clear() 删除（清空/切页/撤销），指针悬空需重建
    if (!lassoPreview_ || !lassoPreview_->scene()) {
        lassoPreview_ = new QGraphicsPathItem();
        lassoPreview_->setPen(rubberPen());
        lassoPreview_->setBrush(Qt::NoBrush);
        lassoPreview_->setZValue(50);
        scene_.addItem(lassoPreview_);
    }
    QPainterPath path;
    path.moveTo(scenePos);
    lassoPreview_->setPath(path);
    lassoPreview_->setVisible(true);
}

void BoardView::updateLasso(const QPointF& scenePos) {
    // 间隔采样，避免点数过多
    const QPointF d = scenePos - lassoLastSample_;
    if (qAbs(d.x()) < kLassoSampleDist && qAbs(d.y()) < kLassoSampleDist)
        return;
    lassoLastSample_ = scenePos;
    lassoPoints_.append(scenePos);
    QPainterPath path = lassoPreview_->path();
    path.lineTo(scenePos);
    lassoPreview_->setPath(path);
    // 广播当前完整采样点集
    std::vector<whiteboard::Point> pts;
    pts.reserve(static_cast<size_t>(lassoPoints_.size()));
    for (const QPointF& p : lassoPoints_)
        pts.push_back(whiteboard::Point(qRound(p.x()), qRound(p.y())));
    data_.SendToolPreview(2, std::to_string(sessionId_), pts, {});
}

// 套索结束：两阶段判定——
// 1) 套索外接矩形与笔迹外接矩形相交（粗筛）；2) 套索闭合路径与笔迹真实相交/圈住（精判）。
void BoardView::finishLasso() {
    if (lassoPreview_)
        lassoPreview_->setVisible(false);
    dragOp_ = DragOp::None;

    if (lassoPoints_.size() >= 3) {
        QPainterPath lasso;
        lasso.moveTo(lassoPoints_.first());
        for (int i = 1; i < lassoPoints_.size(); ++i)
            lasso.lineTo(lassoPoints_.at(i));
        lasso.closeSubpath();

        const QRectF lassoRect = lasso.boundingRect();
        QList<QGraphicsPathItem*> hits;
        for (QGraphicsPathItem* item : elementItems_) {
            if (!lassoRect.intersects(strokeSceneRect(item)))
                continue;  // 粗筛：外接矩形不相交
            if (!pathHitsLasso(item, lasso))
                continue;  // 精判：套索与笔迹无真实交集
            hits.append(item);
        }
        if (!hits.isEmpty())
            selectItems(expandToWholeTables(hits));  // 表格内元素命中时展开为整表
    }
    lassoPoints_.clear();
    // 广播空点集：远端清除套索预览
    data_.SendToolPreview(2, std::to_string(sessionId_), {}, {});
}

void BoardView::updateHoverCursor(const QPointF& scenePos) {
    if (tool_ != Tool::Select && tool_ != Tool::Lasso) {
        setCursor(tool_ == Tool::Eraser ? Qt::BlankCursor : Qt::ArrowCursor);
        return;
    }
    if (selectionFrame_) {
        switch (selectionFrame_->handleAt(scenePos)) {
            case SelectionFrame::TL:
            case SelectionFrame::BR:
                setCursor(Qt::SizeFDiagCursor);
                return;
            case SelectionFrame::TR:
            case SelectionFrame::BL:
                setCursor(Qt::SizeBDiagCursor);
                return;
            case SelectionFrame::L:
            case SelectionFrame::R:
                setCursor(Qt::SizeHorCursor);
                return;
            case SelectionFrame::T:
            case SelectionFrame::B:
                setCursor(Qt::SizeVerCursor);
                return;
            case SelectionFrame::Rotate:
                setCursor(Qt::CrossCursor);
                return;
            case SelectionFrame::Delete:
                setCursor(Qt::PointingHandCursor);
                return;
            default:
                break;
        }
        QGraphicsPathItem* hoverHit = hitElementItem(scenePos);
        // 小工具卡片控件悬停：已选卡片区域内控件提示可点击（优先于整体拖动光标）
        if (auto* card = dynamic_cast<WidgetCardItem*>(hoverHit)) {
            if (card->controlAt(card->mapFromScene(scenePos)) >= 0) {
                setCursor(Qt::PointingHandCursor);
                return;
            }
        }
        if (selectedItems_.contains(hoverHit)) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
    }
    if (hitElementItem(scenePos)) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    setCursor(Qt::ArrowCursor);
}

// ---------- 鼠标事件 ----------

void BoardView::mousePressEvent(QMouseEvent* event) {
    // 中键：任意工具下都用于平移视图
    if (event->button() == Qt::MiddleButton) {
        beginPan(event->pos());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    // 文字内联编辑中：编辑框内按下 → 路由给 QTextEdit（光标定位/拖选）；
    // 编辑框外按下 → 先提交编辑，再按当前工具走常规逻辑（点击画布其他处提交）
    if (textEditor_) {
        const QPointF sp = mapToScene(event->pos());
        if (textEditorProxy_ && textEditorProxy_->sceneBoundingRect().contains(sp)) {
            textRouter_ = true;
            QGraphicsView::mousePressEvent(event);
            return;
        }
        commitTextEditing();
    }
    // 选项编辑弹窗中：弹窗内按下 → 路由给 QTextEdit（光标定位/拖选）；
    // 弹窗外按下 → 先提交（非空且变更才写回），再按当前工具走常规逻辑
    if (optionsEditor_) {
        const QPointF sp = mapToScene(event->pos());
        if (optionsEditorProxy_ && optionsEditorProxy_->sceneBoundingRect().contains(sp)) {
            optionsRouter_ = true;
            QGraphicsView::mousePressEvent(event);
            return;
        }
        commitWidgetOptionsEditing();
    }
    // 抓手工具：左键交给 ScrollHandDrag
    if (tool_ == Tool::Pan) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const QPointF pos = mapToScene(event->pos());
    if (!scene_.sceneRect().contains(pos))
        return;

    if (tool_ == Tool::Select || tool_ == Tool::Lasso) {
        // 手柄优先（删除按钮直接删除，其余手柄进入变换拖动）；
        // 其次图元（已选集合整体移动 / 未选则单选，表格内元素展开为整表）；
        // 若按在当前选中集合的外接矩形内部空白处，也视为移动整个集合；
        // 空白处：选择=矩形框选，套索=虚线笔迹圈选。
        if (selectionFrame_) {
            const SelectionFrame::Handle h = selectionFrame_->handleAt(pos);
            if (h == SelectionFrame::Delete) {
                deleteSelection();  // 删除按钮：批量删除选中（表格级联内部笔迹）
                event->accept();
                return;
            }
            if (h != SelectionFrame::None) {
                beginSelectionDrag(h, pos);
                event->accept();
                return;
            }
        }
        if (tryMindMapAction(pos)) {
            event->accept();
            return;  // 导图钮命中（折叠 / 加 / 删节点）：消费事件
        }
        if (tryWidgetButtonAction(pos)) {
            event->accept();
            return;  // 小工具按钮命中（重置 / 开始暂停）：消费事件
        }
        QGraphicsPathItem* item = hitElementItem(pos);
        if (item) {
            if (!selectedItems_.contains(item))
                selectItems(expandToWholeTables({ item }));  // 表格内元素命中 → 展开整表
            updateMindFocusOnHit(item, pos);  // 导图节点框 → 聚焦（显示 +/× 钮）
            beginSelectionDrag(SelectionFrame::None, pos);
        } else if (selectionFrame_ && selectionFrame_->rect().contains(pos)) {
            beginSelectionDrag(SelectionFrame::None, pos);  // 框内空白 = 移动集合
        } else {
            if (!mindFocusMap_.isEmpty())
                setMindFocus(QString(), QString());  // 画布空白：清节点聚焦
            if (tool_ == Tool::Select)
                beginRubberBand(pos);
            else
                beginLasso(pos);
        }
        event->accept();
        return;
    }

    // 思维导图：单击即放置（默认树结构，根中心对准点击点，无需拖动）；
    // 放置成功后自动切回选择（替代连续放置）
    if (tool_ == Tool::MindMap) {
        auto mind = std::make_shared<whiteboard::MindMapElement>();
        mind->Reset();
        mind->root = toBoardPoint(pos);
        mind->width = penWidth_;
        mind->color = penColor_;
        data_.AddElement(mind);
        setTool(Tool::Select);
        event->accept();
        return;
    }

    // 小工具：单击即放置（卡片中心对准点击点；类型取面板选择，参数在卡片内设置；
    // 放置成功后自动切回选择）。点在已有卡片上 → 控件操作/消费，不叠放新卡。
    // 新类型默认放大以便点按：计算器 1.6 / 算盘·骰子 1.5 / 转盘 1.8 / 点名器 1.7
    if (tool_ == Tool::Widget) {
        if (widgetToolHitExisting(pos)) {
            event->accept();
            return;
        }
        auto widget = std::make_shared<whiteboard::WidgetElement>();
        widget->Reset();
        widget->kind = widgetKind_;
        const qreal defScale = (widgetKind_ == 2) ? 1.6
                               : (widgetKind_ == 3 || widgetKind_ == 4) ? 1.5
                               : (widgetKind_ == 5) ? 1.8
                               : (widgetKind_ == 6) ? 1.7 : 1.0;
        widget->scale = static_cast<float>(defScale);
        widget->x = qRound(pos.x() - kWidgetCardW * defScale / 2.0);
        widget->y = qRound(pos.y() - kWidgetCardH * defScale / 2.0);
        if (widgetKind_ == 5)
            widget->options = "选项一\n选项二\n选项三\n选项四";  // 转盘默认选项
        else if (widgetKind_ == 6)
            widget->options = "张三\n李四\n王五\n赵六";          // 点名器默认名单
        data_.AddElement(widget);
        setTool(Tool::Select);
        event->accept();
        return;
    }

    // 文字：点击已有文字元素 → 编辑该元素；点击空白 → 在点击处新建编辑框
    if (tool_ == Tool::Text) {
        QGraphicsPathItem* item = hitElementItem(pos);
        if (item && item->data(2).toString() != QLatin1String("text"))
            item = nullptr;  // 仅文字元素进入编辑（命中笔迹/图形时按新建处理）
        beginTextEditing(pos, item);
        event->accept();
        return;
    }

    // 图形/表格：拖动绘制（虚线预览随鼠标更新，松开时提交；过小拖动忽略）
    if (tool_ == Tool::Shape || tool_ == Tool::Table) {
        toolDrawing_ = true;
        toolDrawStart_ = pos;
        if (!toolDrawPreview_ || !toolDrawPreview_->scene()) {
            toolDrawPreview_ = new QGraphicsPathItem();
            toolDrawPreview_->setPen(rubberPen());
            toolDrawPreview_->setBrush(Qt::NoBrush);
            toolDrawPreview_->setZValue(60);
            scene_.addItem(toolDrawPreview_);
        }
        updateToolDrawPreview(QRectF(pos, pos));
        toolDrawPreview_->setVisible(true);
        event->accept();
        return;
    }

    sessionId_++;
    if (tool_ == Tool::Eraser) {
        updateEraserPreview(pos);
        data_.EraserBegin(eraserRectAt(pos), sessionId_);
    } else {
        previewItem_ = createPreviewItem(pos);
        strokeToken_++;
        data_.PenBegin(toBoardPoint(pos), sessionId_, strokeToken_);
    }
    event->accept();
}

// 双击：选择/套索工具下双击文字元素 → 进入内联编辑（其余情况交回基类）
void BoardView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || textEditor_) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }
    if (tool_ == Tool::Select || tool_ == Tool::Lasso) {
        const QPointF pos = mapToScene(event->pos());
        if (scene_.sceneRect().contains(pos)) {
            QGraphicsPathItem* item = hitElementItem(pos);
            if (item && item->data(2).toString() == QLatin1String("text")) {
                dragOp_ = DragOp::None;  // 复位首次点击挂起的移动操作
                beginTextEditing(pos, item);
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void BoardView::mouseMoveEvent(QMouseEvent* event) {
    // 中键平移优先
    if (panning_) {
        updatePan(event->pos());
        event->accept();
        return;
    }
    // 文字编辑框内拖选：事件路由给编辑框
    if (textRouter_) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    if (tool_ == Tool::Pan) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    const QPointF pos = mapToScene(event->pos());

    if (!(event->buttons() & Qt::LeftButton)) {
        if (toolDrawing_)
            cancelToolPreview();  // 释放事件丢失（窗口外松开）防御：丢弃未提交预览
        // 未按下：橡皮框跟随 / 选择态悬停光标
        if (tool_ == Tool::Eraser && scene_.sceneRect().contains(pos))
            updateEraserPreview(pos);
        else if (tool_ == Tool::Select || tool_ == Tool::Lasso)
            updateHoverCursor(pos);
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    // 图形/表格拖动中：实时刷新虚线预览
    if (toolDrawing_) {
        updateToolDrawPreview(QRectF(toolDrawStart_, clampToScene(pos)).normalized());
        event->accept();
        return;
    }

    if (tool_ == Tool::Select || tool_ == Tool::Lasso) {
        if (dragOp_ == DragOp::RubberBand)
            updateRubberBand(pos);
        else if (dragOp_ == DragOp::Lasso)
            updateLasso(pos);
        else if (dragOp_ != DragOp::None)
            updateSelectionDrag(pos);
        event->accept();
        return;
    }

    if (tool_ == Tool::Eraser) {
        if (scene_.sceneRect().contains(pos)) {
            updateEraserPreview(pos);
            data_.EraserMove(eraserRectAt(pos), sessionId_);
        }
    } else if (previewItem_) {
        if (scene_.sceneRect().contains(pos)) {
            QPainterPath path = previewItem_->path();
            path.lineTo(pos);
            previewItem_->setPath(path);
            data_.PenMove(toBoardPoint(pos), sessionId_);
        }
    }
}

void BoardView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        endPan();
        event->accept();
        return;
    }
    // 文字编辑框内拖选结束：事件路由给编辑框并复位路由标志
    if (textRouter_) {
        textRouter_ = false;
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    if (tool_ == Tool::Pan) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    const QPointF pos = clampToScene(mapToScene(event->pos()));

    if (tool_ == Tool::Select || tool_ == Tool::Lasso) {
        if (dragOp_ == DragOp::RubberBand)
            finishRubberBand();
        else if (dragOp_ == DragOp::Lasso)
            finishLasso();
        else
            endSelectionDrag();
        updateHoverCursor(pos);
        event->accept();
        return;
    }

    // 图形/表格拖动结束：达到最小尺寸则提交落库并切回选择（过小视为误触丢弃）
    if (toolDrawing_) {
        const QRectF rect = QRectF(toolDrawStart_, pos).normalized();
        if (rect.width() >= 8.0 && rect.height() >= 8.0) {
            commitToolDraw(rect);
            setTool(Tool::Select);  // 放置成功后自动切回选择
        }
        cancelToolPreview();
        event->accept();
        return;
    }

    if (tool_ == Tool::Eraser) {
        updateEraserPreview(pos);
        data_.EraserEnd(eraserRectAt(pos), sessionId_);
    } else if (previewItem_) {
        // 收尾点（出界时钳制到画布内），保证数据与预览一致
        QPainterPath path = previewItem_->path();
        path.lineTo(pos);
        previewItem_->setPath(path);
        data_.PenEnd(toBoardPoint(pos), sessionId_);

        pendingItems_.insert(strokeToken_, previewItem_);
        previewItem_ = nullptr;
    }
    event->accept();
}

void BoardView::leaveEvent(QEvent* event) {
    QGraphicsView::leaveEvent(event);
    if (eraserPreview_)
        eraserPreview_->setVisible(false);
}

// ---------- 数据层回调（投递回 UI 线程） ----------

void BoardView::onElementsChanged(
    std::vector<std::string> removed,
    std::vector<std::shared_ptr<whiteboard::Element>> added,
    std::vector<whiteboard::EraserPlacement> placements) {
    QMetaObject::invokeMethod(this,
        [this, removed = std::move(removed), added = std::move(added),
         placements = std::move(placements)]() {
            applyElementChanges(removed, added, placements);
        },
        Qt::QueuedConnection);
}

void BoardView::onStrokeCommitted(uint64_t token, const std::string& id,
                                  const std::string& parentId, int cellIndex) {
    QMetaObject::invokeMethod(this, [this, token, id, parentId, cellIndex]() {
        applyStrokeCommitted(token, id, parentId, cellIndex);
    }, Qt::QueuedConnection);
}

void BoardView::onCleared() {
    QMetaObject::invokeMethod(this, [this]() {
        applyCleared();
    }, Qt::QueuedConnection);
}

void BoardView::onPageChanged() {
    QMetaObject::invokeMethod(this, [this]() {
        applyPageChanged();
    }, Qt::QueuedConnection);
}

void BoardView::onStrokePreview(std::string strokeId, uint32_t color, int width,
                                std::vector<whiteboard::Point> points) {
    QMetaObject::invokeMethod(this,
        [this, strokeId = std::move(strokeId), color, width, points = std::move(points)]() {
            applyStrokePreview(strokeId, color, width, points);
        },
        Qt::QueuedConnection);
}

void BoardView::onToolPreview(uint32_t tool, std::string sessionId,
                              std::vector<whiteboard::Point> points,
                              std::vector<std::string> elementIds) {
    QMetaObject::invokeMethod(this,
        [this, tool, sessionId = std::move(sessionId),
         points = std::move(points), elementIds = std::move(elementIds)]() {
            applyToolPreview(tool, sessionId, points, elementIds);
        },
        Qt::QueuedConnection);
}

void BoardView::onSynced() {
    QMetaObject::invokeMethod(this, [this]() {
        applySynced();
    }, Qt::QueuedConnection);
}

// 撤销/重做/远程页面操作后页面整体被替换：全量重建 + 刷新页面列表
void BoardView::applyPageChanged() {
    refreshPageIds();
    reloadPage();
    emit pagesChanged();
    emit currentPageChanged(pageIds_.indexOf(currentPageId_));
}

// FullSync 应用后：全量重建所有页面状态
void BoardView::applySynced() {
    applyPageChanged();
}

// 远程笔画实时预览：按 strokeId 维护预览图元（Begin 创建，Move 更新）
void BoardView::applyStrokePreview(const std::string& strokeId, uint32_t color, int width,
                                   const std::vector<whiteboard::Point>& points) {
    const QString key = QString::fromStdString(strokeId);
    QGraphicsPathItem* item = remotePreview_.value(key, nullptr);
    if (!item) {
        item = new QGraphicsPathItem();
        item->setPen(strokePen(color, width));
        item->setBrush(Qt::NoBrush);
        item->setZValue(90);  // 低于橡皮预览框（100）
        scene_.addItem(item);
        remotePreview_.insert(key, item);
    }
    QPainterPath path;
    for (size_t i = 0; i < points.size(); ++i) {
        const whiteboard::Point& p = points[i];
        if (i == 0)
            path.moveTo(p.x, p.y);
        else
            path.lineTo(p.x, p.y);
    }
    item->setPath(path);
}

// 远端工具预览（UI 线程执行）：
// tool=1 橡皮擦：points 非空 → 累积背景色遮罩（视觉上只覆盖橡皮擦扫过的碰撞区域，
//               整条图元保持可见，数据不动）；points 空 → 移除遮罩（End 时数据层按 id 精确删/加）。
// tool=2 套索：points 非空 → 浅蓝虚线路径预览；空 → 清除。
// tool=3 选择框：points 非空 → 虚线矩形预览（4 顶点外接）；空 → 清除。
void BoardView::applyToolPreview(uint32_t tool, const std::string& sessionId,
                                 const std::vector<whiteboard::Point>& points,
                                 const std::vector<std::string>& elementIds) {
    Q_UNUSED(elementIds);
    if (tool == 1) {
        // ---------- 远端橡皮擦遮罩（填充） + 当前矩形虚线框 ----------
        if (points.empty()) {
            // End：移除遮罩与虚线框（数据层随后按 id 精确删/加，图元恢复正确形态）
            if (remoteEraserPreview_) {
                scene_.removeItem(remoteEraserPreview_);
                delete remoteEraserPreview_;
                remoteEraserPreview_ = nullptr;
            }
            if (remoteEraserMask_) {
                scene_.removeItem(remoteEraserMask_);
                delete remoteEraserMask_;
                remoteEraserMask_ = nullptr;
            }
            remoteEraserSessionId_.clear();
            remoteEraserLastRect_ = QRectF();
            return;
        }
        // 4 点橡皮矩形 → 外接矩形
        QRectF r;
        for (size_t i = 0; i < points.size(); ++i) {
            const QPointF p(points[i].x, points[i].y);
            r = i == 0 ? QRectF(p, QSizeF(1, 1)) : r.united(QRectF(p, QSizeF(1, 1)));
        }
        if (!remoteEraserMask_ || !remoteEraserMask_->scene()) {
            remoteEraserMask_ = new QGraphicsPathItem();
            remoteEraserMask_->setPen(Qt::NoPen);  // 无边框：描累积轮廓会形成"移动轨迹"残留
            // 不透明背景色填充：视觉上等于"擦除露出画布背景"，只覆盖碰撞区域
            remoteEraserMask_->setBrush(kBoardBackground);
            // 高于笔画(0)/远端预览笔画(90)/套索与选择预览(50)，低于本地橡皮指示框(100)
            remoteEraserMask_->setZValue(98);
            scene_.addItem(remoteEraserMask_);
        }
        if (!remoteEraserPreview_ || !remoteEraserPreview_->scene()) {
            remoteEraserPreview_ = new QGraphicsRectItem();
            remoteEraserPreview_->setPen(Qt::NoPen);  // 样式同本地橡皮预览：白色方块
            remoteEraserPreview_->setBrush(QColor(255, 255, 255, 240));
            remoteEraserPreview_->setZValue(99);
            scene_.addItem(remoteEraserPreview_);
        }
        // 会话切换：重置累积遮罩，防止上次 End 丢失导致遮罩残留
        if (remoteEraserSessionId_ != QString::fromStdString(sessionId)) {
            remoteEraserSessionId_ = QString::fromStdString(sessionId);
            remoteEraserMask_->setPath(QPainterPath());
            remoteEraserLastRect_ = QRectF();
        }
        // 累积遮罩：把当前擦除矩形并入；仅与「上一帧矩形」取并集防缝——
        // 不能用累积路径 boundingRect（随轨迹变大），否则来回擦时遮罩会膨胀成一个大矩形
        QPainterPath mask = remoteEraserMask_->path();
        if (!remoteEraserLastRect_.isNull() && remoteEraserLastRect_.intersects(r))
            mask.addRect(remoteEraserLastRect_.united(r));
        else
            mask.addRect(r);
        remoteEraserLastRect_ = r;
        mask.setFillRule(Qt::WindingFill);  // 重叠区域仍填充（OddEven 会产生空洞）
        remoteEraserMask_->setPath(mask);
        // 虚线框只跟随当前矩形
        remoteEraserPreview_->setRect(r);
    } else if (tool == 2) {
        // ---------- 远端套索预览 ----------
        if (points.empty()) {
            if (remoteLassoPreview_) {
                scene_.removeItem(remoteLassoPreview_);
                delete remoteLassoPreview_;
                remoteLassoPreview_ = nullptr;
            }
            return;
        }
        if (!remoteLassoPreview_ || !remoteLassoPreview_->scene()) {
            remoteLassoPreview_ = new QGraphicsPathItem();
            remoteLassoPreview_->setPen(rubberPen());
            remoteLassoPreview_->setBrush(Qt::NoBrush);
            remoteLassoPreview_->setZValue(50);
            scene_.addItem(remoteLassoPreview_);
        }
        QPainterPath path;
        for (size_t i = 0; i < points.size(); ++i) {
            if (i == 0)
                path.moveTo(points[i].x, points[i].y);
            else
                path.lineTo(points[i].x, points[i].y);
        }
        remoteLassoPreview_->setPath(path);
        remoteLassoPreview_->setVisible(true);
    } else if (tool == 3) {
        // ---------- 远端选择框预览 ----------
        if (points.empty()) {
            if (remoteSelectionPreview_) {
                scene_.removeItem(remoteSelectionPreview_);
                delete remoteSelectionPreview_;
                remoteSelectionPreview_ = nullptr;
            }
            return;
        }
        if (!remoteSelectionPreview_ || !remoteSelectionPreview_->scene()) {
            remoteSelectionPreview_ = new QGraphicsRectItem();
            remoteSelectionPreview_->setPen(rubberPen());
            remoteSelectionPreview_->setBrush(QColor(120, 190, 255, 32));
            remoteSelectionPreview_->setZValue(50);
            scene_.addItem(remoteSelectionPreview_);
        }
        QRectF r;
        for (size_t i = 0; i < points.size(); ++i) {
            const QPointF p(points[i].x, points[i].y);
            r = i == 0 ? QRectF(p, QSizeF(1, 1)) : r.united(QRectF(p, QSizeF(1, 1)));
        }
        remoteSelectionPreview_->setRect(r);
        remoteSelectionPreview_->setVisible(true);
    }
}

void BoardView::applyElementChanges(
    const std::vector<std::string>& removed,
    const std::vector<std::shared_ptr<whiteboard::Element>>& added,
    const std::vector<whiteboard::EraserPlacement>& placements) {
    // 先删后加，保证 id 不冲突。正常情况下 removed 已由数据层递归展开（删除表格时含全部
    // 后代 id）；但「整表快照更新」路径（远端 UpdateTableElement / 本地格内文字编辑）只含
    // 表格/子元素自身 id，需按 tableChildren_ 镜像展开后代。格内子元素被单独增删替换或
    // 整表快照到达时不单独建模，统一在结尾按最新快照 rebuildTable（布局按内容重排）。
    // 导图结构更新（折叠/增删节点/烘焙）走 removed+added 重建路径：先存聚焦态，
    // 记录重建前被选中的 id，重建后恢复选中与聚焦（避免重建导致失去选中/聚焦态）。
    const QString keepFocusMap = mindFocusMap_;
    const QString keepFocusNode = mindFocusNode_;
    QSet<QString> tablesToRebuild;  // 受影响待重建的表格 id（同批多次变更只重建一次）
    QStringList removeOrder;
    for (const std::string& id : removed) {
        const QString key = QString::fromStdString(id);
        if (removeOrder.contains(key))
            continue;
        removeOrder.append(key);
        const QStringList kids = tableChildren_.value(key);  // 镜像后代随表格级联删除
        for (const QString& kid : kids) {
            if (!removeOrder.contains(kid))
                removeOrder.append(kid);
        }
    }
    bool selectionChanged = false;
    QSet<QString> reselectIds;
    for (const QString& key : removeOrder) {
        // 归属映射清理：自身出表 + 从父表格后代列表移除；父表未被整体删除时待重建
        if (cellOwner_.contains(key)) {
            const QString parent = cellOwner_.take(key);
            if (tableChildren_.contains(parent))
                tableChildren_[parent].removeAll(key);
            if (!removeOrder.contains(parent) && elementItems_.contains(parent))
                tablesToRebuild.insert(parent);  // 格内内容单独被删：父表按新内容重排
        }
        tableChildren_.remove(key);  // 被删者本身是表格时移除其后代镜像
        tableLayouts_.remove(key);   // 表格布局缓存失效（重建时由 buildTableItem 重新写入）
        mindLayouts_.remove(key);    // 导图布局缓存失效（重建时由 buildMindMapItem 重新写入）
        widgetRuntimes_.remove(key); // 小工具运行时随元素删除（撤销恢复 → 初始停止态）
        if (optionsEditor_ && optionsEditId_ == key)
            cancelWidgetOptionsEditing();  // 编辑中的卡片被删除：先关弹窗
        QGraphicsPathItem* item = elementItems_.take(key);
        if (item) {
            if (selectedItems_.removeAll(item) > 0) {
                selectionChanged = true;
                reselectIds.insert(key);  // 重建（added 含同 id）后恢复选中
            }
            scene_.removeItem(item);
            delete item;
        }
    }
    if (selectionChanged) {
        if (selectedItems_.isEmpty()) {
            clearSelection();
        } else {
            const QList<QGraphicsPathItem*> keep = selectedItems_;  // 拷贝后重建框（收缩）
            clearSelection();
            selectItems(expandToWholeTables(keep));  // 维持整表选中不变量
        }
    }
    for (size_t i = 0; i < added.size(); ++i) {
        const whiteboard::Element& e = *added[i];
        const QString key = QString::fromStdString(e.id);
        // 远程预览笔画落库：先移除预览图元，再按正式图元重建
        if (QGraphicsPathItem* preview = remotePreview_.take(key)) {
            scene_.removeItem(preview);
            delete preview;
        }
        // 格内子元素（笔迹落库/橡皮碎片/新图形文字入格）：added 中坐标已是格局部，
        // 不单独建模，所属表格统一按最新快照重建（归属镜像由 buildTableItem 重建）
        if (i < placements.size() && !placements[i].parentId.empty()) {
            tablesToRebuild.insert(QString::fromStdString(placements[i].parentId));
            continue;
        }
        // 已存在的表格收到整表快照（本地格内文字编辑 / 远端表格更新）：整表重建
        if (elementItems_.contains(key)) {
            tablesToRebuild.insert(key);
            continue;
        }
        buildElementItem(e);
    }
    // 统一重建受影响的表格（快照取自数据层；选中态由 rebuildTable 内部按原选中恢复）
    for (const QString& tableId : tablesToRebuild) {
        if (elementItems_.contains(tableId))
            rebuildTable(tableId);
    }
    // 重建后恢复选中（新 item 指针已变，重建选择框；原选中集保留合并）
    if (!reselectIds.isEmpty()) {
        QList<QGraphicsPathItem*> keep = selectedItems_;
        bool hasRebuilt = false;
        for (const QString& key : reselectIds) {
            QGraphicsPathItem* rebuilt = elementItems_.value(key, nullptr);
            if (rebuilt && !keep.contains(rebuilt)) {
                keep.append(rebuilt);
                hasRebuilt = true;
            }
        }
        if (hasRebuilt && !keep.isEmpty()) {
            clearSelection();
            selectItems(expandToWholeTables(keep));  // 维持整表选中不变量
        }
    }
    // 聚焦恢复：节点在新布局中仍可见才恢复（折叠 / 删除导致不可见则保持清除）
    if (!keepFocusMap.isEmpty()) {
        QGraphicsPathItem* item = elementItems_.value(keepFocusMap, nullptr);
        const auto itL = mindLayouts_.constFind(keepFocusMap);
        if (item && itL != mindLayouts_.constEnd() &&
            itL.value().FindNode(keepFocusNode.toStdString())) {
            setMindFocus(keepFocusMap, keepFocusNode);
        }
    }
}

void BoardView::applyStrokeCommitted(uint64_t token, const std::string& id,
                                     const std::string& parentId, int cellIndex) {
    Q_UNUSED(cellIndex);
    QGraphicsPathItem* item = pendingItems_.take(token);
    if (!item)
        return;
    // Clear/切页可能已把 item 删除（item 不再属于 scene），此时忽略
    if (!item->scene())
        return;
    // 笔迹归属表格格：pending 预览为页面坐标、正式形态为格局部坐标 + 格帧变换，
    // 无法原地转正，丢弃预览并按数据层最新快照整表重建（布局随内容重排）
    if (!parentId.empty()) {
        scene_.removeItem(item);
        delete item;
        rebuildTable(QString::fromStdString(parentId));
        return;
    }
    item->setData(0, QString::fromStdString(id));
    // 与 buildStrokeItem 对齐：转正时补写外接矩形 data(1)。否则选择框与框选粗筛
    // 退化为 path().boundingRect()，水平/垂直直线一维为 0 时 QRectF 相交判定
    // 恒失败（直线框选不上）；此处与数据层 BoundaryRect::ToRect 一致做 1px 保底。
    QRectF br = item->path().boundingRect();
    if (br.width() <= 0)
        br.setWidth(1);
    if (br.height() <= 0)
        br.setHeight(1);
    item->setData(1, br);
    const QString key = QString::fromStdString(id);
    elementItems_.insert(key, item);
}

void BoardView::applyCleared() {
    if (textEditor_)
        cancelTextEditing();  // 先清编辑框：scene_.clear() 会删除代理，防悬空
    clearSelection();
    // 小工具运行时：清空当前页 → 擦除本页小工具的走时状态（其他页保留）
    for (auto it = elementItems_.constBegin(); it != elementItems_.constEnd(); ++it) {
        if (it.value()->data(2).toString() == QLatin1String("widget"))
            widgetRuntimes_.remove(it.key());
    }
    scene_.clear();  // 删除所有图元（含预览笔画、橡皮框与框选橡皮筋）
    elementItems_.clear();
    cellOwner_.clear();
    tableChildren_.clear();
    tableLayouts_.clear();  // 表格布局缓存随清空失效
    mindLayouts_.clear();  // 布局缓存随清空失效
    pendingItems_.clear();
    remotePreview_.clear();
    previewItem_ = nullptr;
    toolDrawPreview_ = nullptr;  // 已被 scene_.clear() 删除，防悬空
    textEditFrame_ = nullptr;    // 已被 scene_.clear() 删除，防悬空（编辑框已先行取消）
    toolDrawing_ = false;
    eraserPreview_ = nullptr;  // 懒重建
    rubberBand_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    lassoPreview_ = nullptr;   // 已被 scene_.clear() 删除，防悬空
    remoteEraserPreview_ = nullptr;    // 已被 scene_.clear() 删除，防悬空
    remoteEraserMask_ = nullptr;       // 已被 scene_.clear() 删除，防悬空
    remoteLassoPreview_ = nullptr;     // 已被 scene_.clear() 删除，防悬空
    remoteSelectionPreview_ = nullptr; // 已被 scene_.clear() 删除，防悬空
    remoteEraserSessionId_.clear();
    remoteEraserLastRect_ = QRectF();
    if (tool_ == Tool::Eraser)
        ensureEraserItem();
}
