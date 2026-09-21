#include "MainWindow.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QToolTip>
#include <QWidget>
#include <QWindow>

#include <chrono>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>  // 桌面批注点击穿透命中测试（WM_NCHITTEST / HTTRANSPARENT）
#  include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#endif

#include "BoardDisk.h"
#include "BoardToolBar.h"
#include "BoardUtil.h"
#include "EraserPanel.h"
#include "JoinRoomDialog.h"
#include "MorePanel.h"
#include "OtherToolsPanel.h"
#include "PageRail.h"
#include "PenSettingPanel.h"
#include "SettingsPanel.h"
#include "ShapePickerPanel.h"
#include "TableSetupPanel.h"
#include "TextSetupPanel.h"
#include "WidgetSetupPanel.h"

namespace {

// 白板文件格式标识与版本（扩展名 .hrwb）
const char* const kBoardFileFormat = "hrtc-whiteboard";
constexpr int kBoardFileVersion = 1;

// 兜底页 id（文件中的页 id 为空或重复时）：与数据层一致的 chrono 纳秒时间戳 + 序号
QString fallbackPageId(int index) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    return QString::number(ns) + QStringLiteral("-") + QString::number(index);
}

// 笔画 → JSON（points 扁平存储 [x0,y0,x1,y1,...]）
QJsonObject strokeToJson(const whiteboard::Stroke& stroke) {
    QJsonArray points;
    for (const whiteboard::Point& p : stroke.points) {
        points.append(p.x);
        points.append(p.y);
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::fromStdString(stroke.id));
    obj.insert(QStringLiteral("color"), static_cast<double>(stroke.color));
    obj.insert(QStringLiteral("width"), stroke.width);
    obj.insert(QStringLiteral("points"), points);
    return obj;
}

// JSON → 笔画（直接设置点集与包围盒，避免 Append 插值改变原始形态）
bool strokeFromJson(const QJsonObject& obj, whiteboard::Stroke& stroke) {
    const QJsonArray points = obj.value(QStringLiteral("points")).toArray();
    const int count = points.size();
    if (count < 4)  // 至少两个点
        return false;
    const QString id = obj.value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
        stroke.id = id.toStdString();
    stroke.color = static_cast<uint32_t>(obj.value(QStringLiteral("color")).toDouble(0x00FFFFFF));
    stroke.width = qMax(1, obj.value(QStringLiteral("width")).toInt(3));

    stroke.bounding = whiteboard::BoundaryRect();
    stroke.points.reserve(static_cast<size_t>(count / 2));
    for (int i = 0; i + 1 < count; i += 2) {
        const whiteboard::Point p(points.at(i).toInt(), points.at(i + 1).toInt());
        stroke.points.push_back(p);
        stroke.rawPoints.push_back(p);
        stroke.bounding.Update(p.x, p.y);
    }
    return true;
}

// 保存白板数据到文件（JSON；失败时 error 为原因；skipPageId 非空时跳过该页，
// 供桌面批注模式排除临时批注页）
bool saveBoardToFile(const QString& path, const std::vector<whiteboard::Page>& pages,
                     const QString& currentPageId, const QString& skipPageId, QString* error) {
    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kBoardFileFormat));
    root.insert(QStringLiteral("version"), kBoardFileVersion);
    root.insert(QStringLiteral("currentPageId"), currentPageId);

    QJsonArray pageArr;
    for (const whiteboard::Page& page : pages) {
        if (!skipPageId.isEmpty() && QString::fromStdString(page.pageId) == skipPageId)
            continue;  // 跳过临时批注页（桌面批注模式）
        QJsonObject pageObj;
        pageObj.insert(QStringLiteral("pageId"), QString::fromStdString(page.pageId));
        QJsonArray strokeArr;
        for (const auto& e : page.elements) {
            const auto* stroke = dynamic_cast<const whiteboard::Stroke*>(e.get());
            if (stroke)
                strokeArr.append(strokeToJson(*stroke));
        }
        pageObj.insert(QStringLiteral("strokes"), strokeArr);
        pageArr.append(pageObj);
    }
    root.insert(QStringLiteral("pages"), pageArr);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("无法写入文件：%1").arg(file.errorString());
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size()) {
        *error = QStringLiteral("写入文件失败：%1").arg(file.errorString());
        return false;
    }
    file.close();
    return true;
}

// 从文件读取白板数据（返回 false 时 error 为失败原因）
bool loadBoardFromFile(const QString& path, std::vector<whiteboard::Page>* pages,
                       QString* currentPageId, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        *error = QStringLiteral("文件不是有效的白板文件（%1）").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() !=
        QString::fromLatin1(kBoardFileFormat)) {
        *error = QStringLiteral("文件不是 HRTC 白板文件。");
        return false;
    }

    const QJsonArray pageArr = root.value(QStringLiteral("pages")).toArray();
    pages->clear();
    QSet<QString> seenIds;
    int index = 0;
    for (const QJsonValue& pageValue : pageArr) {
        const QJsonObject pageObj = pageValue.toObject();
        whiteboard::Page page;
        QString pageId = pageObj.value(QStringLiteral("pageId")).toString();
        if (pageId.isEmpty() || seenIds.contains(pageId))
            pageId = fallbackPageId(index);  // 兜底：空/重复 id 重新生成
        seenIds.insert(pageId);
        page.pageId = pageId.toStdString();
        for (const QJsonValue& strokeValue : pageObj.value(QStringLiteral("strokes")).toArray()) {
            whiteboard::Stroke stroke;
            if (strokeFromJson(strokeValue.toObject(), stroke))
                page.Append(stroke);
        }
        pages->push_back(std::move(page));
        ++index;
    }
    *currentPageId = root.value(QStringLiteral("currentPageId")).toString();
    return true;
}

// 工具 → "其他"面板条目下标（图形/导图/表格/文字/小工具；非 5 类工具返回 -1）
int otherToolIndex(BoardView::Tool tool) {
    switch (tool) {
        case BoardView::Tool::Shape:
            return OtherToolsPanel::ItemShape;
        case BoardView::Tool::MindMap:
            return OtherToolsPanel::ItemMindMap;
        case BoardView::Tool::Table:
            return OtherToolsPanel::ItemTable;
        case BoardView::Tool::Text:
            return OtherToolsPanel::ItemText;
        case BoardView::Tool::Widget:
            return OtherToolsPanel::ItemWidget;
        default:
            return -1;
    }
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    windowTitleBase_ = QStringLiteral("HRTC 黑板 Demo");
    setWindowTitle(windowTitleBase_);
    resize(1280, 720);           // 16:9（与白板基准 1920x1080 等比）
    setMinimumSize(960, 540);    // 16:9 最小尺寸

    view_ = new BoardView(this);
    toolBar_ = new BoardToolBar(this);
    disk_ = new BoardDisk(this);
    pageRail_ = new PageRail(this);
    penPanel_ = new PenSettingPanel(this);
    eraserPanel_ = new EraserPanel(this);
    settingsPanel_ = new SettingsPanel(this);
    morePanel_ = new MorePanel(this);
    shapePanel_ = new ShapePickerPanel(this);
    tablePanel_ = new TableSetupPanel(this);
    textPanel_ = new TextSetupPanel(this);
    widgetPanel_ = new WidgetSetupPanel(this);
    otherPanel_ = new OtherToolsPanel(this);

    // 中央区域：画布手动几何（黑板模式 16:9 居中，窗口模式铺满）+ 工具栏悬浮底部居中（距底 15px）
    // + 圆盘/页面栏悬浮（几何自管理：圆盘右缘中部、页面栏左缘贴边）
    central_ = new QWidget(this);
    central_->setObjectName(QStringLiteral("boardCentral"));
    auto* grid = new QGridLayout(central_);
    grid->setContentsMargins(0, 0, 0, 15);
    grid->setSpacing(0);
    view_->setParent(central_);  // 画布不入布局：几何由 updateBoardGeometry() 控制
    grid->addWidget(toolBar_, 0, 0, Qt::AlignBottom | Qt::AlignHCenter);
    disk_->setParent(central_);       // 悬浮球：不入布局，几何自管理
    pageRail_->setParent(central_);   // 页面栏标签：同上
    setCentralWidget(central_);
    toolBar_->raise();
    disk_->raise();
    pageRail_->raise();
    disk_->hide();  // 默认底部工具栏模式（设置面板可切换）
    central_->installEventFilter(this);  // 中央区域尺寸变化时重算画布几何
    updateBoardGeometry();               // 初始几何（含页面栏/圆盘活动范围与初始位）

    // ---------- 工具 / 功能组 ----------
    connect(toolBar_, &BoardToolBar::toolSelected, this, &MainWindow::onToolSelected);
    connect(view_, &BoardView::toolChanged, this, [this](BoardView::Tool tool) {
        toolBar_->setCurrentTool(tool);
        disk_->setCurrentTool(tool);  // 悬浮球图标/内环高亮/外环焦点同步
        otherPanel_->setCurrentIndex(otherToolIndex(tool));  // 当前工具映射到"其他"面板条目
        updateOtherButtonState();
        // 桌面批注模式："鼠标"工具 → 点击穿透操作电脑；其他批注工具恢复正常
        if (desktopMode_)
            setClickThrough(tool == BoardView::Tool::Mouse);
    });
    connect(toolBar_, &BoardToolBar::undoRequested, view_, &BoardView::undo);
    connect(toolBar_, &BoardToolBar::redoRequested, view_, &BoardView::redo);
    connect(toolBar_, &BoardToolBar::zoomInRequested, view_, &BoardView::zoomIn);
    connect(toolBar_, &BoardToolBar::zoomOutRequested, view_, &BoardView::zoomOut);
    connect(toolBar_, &BoardToolBar::zoomResetRequested, view_, &BoardView::resetZoom);
    connect(view_, &BoardView::zoomChanged, this, [this](qreal percent) {
        zoomPercent_ = percent;
        toolBar_->setZoomPercent(percent);
        disk_->setZoomPercent(percent);
    });

    // ---------- 页面栏（左侧常驻，两种模式共用） ----------
    connect(pageRail_, &PageRail::prevPageRequested, this, &MainWindow::onPrevPage);
    connect(pageRail_, &PageRail::nextPageRequested, this, &MainWindow::onNextPage);
    connect(pageRail_, &PageRail::addPageRequested, view_, &BoardView::createPage);
    connect(pageRail_, &PageRail::pageActivated, this, &MainWindow::onPageActivated);
    connect(pageRail_, &PageRail::pageDeleteRequested, this, &MainWindow::onPageDeleteRequested);
    connect(pageRail_, &PageRail::pageCopyRequested, this, &MainWindow::onCopyPage);
    connect(pageRail_, &PageRail::expandRequested, this, &MainWindow::rebuildPageRail);
    connect(view_, &BoardView::pagesChanged, this, &MainWindow::updatePageButtons);
    connect(view_, &BoardView::currentPageChanged, this, &MainWindow::updatePageButtons);

    // ---------- 桌面圆盘（悬浮球模式） ----------
    // 工具类目（擦除/选择/套索/鼠标）：直切工具，toolChanged 回传同步双工具栏
    connect(disk_, &BoardDisk::toolSelected, view_, &BoardView::setTool);
    // 笔参数（类型/颜色/粗细）：与底部笔面板同一处理（含荧光笔 alpha 编码）
    connect(disk_, &BoardDisk::penParamChanged, this, &MainWindow::onPenChanged);
    connect(disk_, &BoardDisk::eraserSizeChanged, view_, &BoardView::setEraserSize);
    connect(disk_, &BoardDisk::undoRequested, view_, &BoardView::undo);
    connect(disk_, &BoardDisk::redoRequested, view_, &BoardView::redo);
    connect(disk_, &BoardDisk::zoomInRequested, view_, &BoardView::zoomIn);
    connect(disk_, &BoardDisk::zoomOutRequested, view_, &BoardView::zoomOut);
    connect(disk_, &BoardDisk::zoomResetRequested, view_, &BoardView::resetZoom);
    // 外环"清屏"：滑动清屏面板锚定圆盘上方（面板已开时点击该钮收起：pending 拦截重开）
    connect(disk_, &BoardDisk::clearPanelRequested, this, [this]() {
        if (eraserButtonClosePending_) {
            eraserButtonClosePending_ = false;
            return;
        }
        showPanelAbove(eraserPanel_, BoardView::Tool::Eraser);
    });
    // 更多外环：文字/表格切工具并弹参数面板（锚定圆盘）；形状直接切图形工具
    connect(disk_, &BoardDisk::textToolRequested, this, [this]() {
        if (textButtonClosePending_) {  // 面板已开时点击该钮收起：pending 拦截重开
            textButtonClosePending_ = false;
            return;
        }
        view_->setTool(BoardView::Tool::Text);
        showPanelAbove(textPanel_, BoardView::Tool::Text);
    });
    connect(disk_, &BoardDisk::tableToolRequested, this, [this]() {
        if (tableButtonClosePending_) {  // 同上
            tableButtonClosePending_ = false;
            return;
        }
        view_->setTool(BoardView::Tool::Table);
        showPanelAbove(tablePanel_, BoardView::Tool::Table);
    });
    // 外环"图形"：四种形状合一入口 → 弹形状选择面板（选中后经 shapeSelected 切图形工具）
    connect(disk_, &BoardDisk::shapePanelRequested, this, [this]() {
        if (shapeButtonClosePending_) {  // 面板已开时点击该钮收起：pending 拦截重开
            shapeButtonClosePending_ = false;
            return;
        }
        shapePanel_->setCurrentKind(view_->shapeKind());  // 恢复当前形状选中态
        view_->setTool(BoardView::Tool::Shape);
        showPanelAbove(shapePanel_, BoardView::Tool::Shape);
    });
    // 外环"小工具"：切小工具并弹类型选择面板
    connect(disk_, &BoardDisk::widgetToolRequested, this, [this]() {
        if (widgetButtonClosePending_) {  // 同上
            widgetButtonClosePending_ = false;
            return;
        }
        widgetPanel_->setCurrent(widgetKind_);  // 恢复上次类型选择态
        view_->setTool(BoardView::Tool::Widget);
        showPanelAbove(widgetPanel_, BoardView::Tool::Widget);
    });
    // 更多外环"菜单"：复用更多面板（互动/保存/打开/设置/退出；锚点随模式切换）
    connect(disk_, &BoardDisk::menuRequested, this, &MainWindow::onMoreRequested);
    // 桌面批注模式：底部工具栏"桌面"按钮进入；圆盘"更多"外环房子钮切换——
    // 非桌面模式点按进入（返回桌面批注：圆盘模式工具栏不可见时的进入入口，
    // 修复此前该钮在非桌面模式空操作的问题）；桌面模式点按退出（返回白板）
    connect(toolBar_, &BoardToolBar::desktopRequested, this, [this]() { enterDesktopMode(); });
    connect(disk_, &BoardDisk::backBoardRequested, this, [this]() {
        if (desktopMode_)
            exitDesktopMode();
        else
            enterDesktopMode();
    });

    // ---------- 更多面板（互动 / 保存 / 打开 / 设置 / 退出） ----------
    connect(toolBar_, &BoardToolBar::moreRequested, this, &MainWindow::onMoreRequested);
    connect(morePanel_, &MorePanel::interactRequested, this, [this]() {
        morePanel_->hide();  // 先收起更多面板，再延迟弹出加入房间对话框
        QTimer::singleShot(0, this, &MainWindow::onInteract);
    });
    connect(morePanel_, &MorePanel::saveRequested, this, &MainWindow::onSaveBoard);
    connect(morePanel_, &MorePanel::openRequested, this, &MainWindow::onOpenBoard);
    connect(morePanel_, &MorePanel::settingsRequested, this, [this]() {
        morePanel_->hide();  // 先收起更多面板，再延迟打开设置面板（避免 Popup 更替冲突）
        QTimer::singleShot(0, this, &MainWindow::onSettingsRequested);
    });
    connect(morePanel_, &MorePanel::exitRequested, this, &MainWindow::onExitApp);
    connect(morePanel_, &MorePanel::closed, this, [this]() {
        toolBar_->setMoreButtonActive(false);
        // Popup 因点击更多按钮/圆盘菜单钮而在按下阶段自动关闭：置标志，
        // 防止同一次点击的释放阶段重开面板（锚点随工具栏模式取工具栏/圆盘）
        if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
            const bool onAnchor =
                diskMode_ ? disk_->diskGlobalRect().contains(QCursor::pos())
                          : toolBar_->moreButtonGlobalRect().contains(QCursor::pos());
            if (onAnchor)
                moreButtonClosePending_ = true;
        }
    });

    // ---------- "其他"工具面板（图形 / 导图 / 表格 / 文字 / 小工具汇总入口） ----------
    connect(toolBar_, &BoardToolBar::otherRequested, this, &MainWindow::onOtherRequested);
    connect(otherPanel_, &OtherToolsPanel::closed, this, [this]() {
        updateOtherButtonState();
        // Popup 因点击"其他"按钮而在按下阶段自动关闭：置标志，
        // 防止同一次点击的释放阶段（clicked）重开面板
        if ((QGuiApplication::mouseButtons() & Qt::LeftButton) &&
            toolBar_->otherButtonGlobalRect().contains(QCursor::pos()))
            otherButtonClosePending_ = true;
    });
    // 条目链路：先收起"其他"面板，再延迟切工具/弹参数面板（避免 Popup 更替冲突，
    // 同"更多→设置"先例；图形/表格/文字/小工具 附带各自参数面板）
    connect(otherPanel_, &OtherToolsPanel::shapeRequested, this, [this]() {
        otherPanel_->hide();
        QTimer::singleShot(0, this, [this]() {
            view_->setTool(BoardView::Tool::Shape);
            showPanelAbove(shapePanel_, BoardView::Tool::Shape);
        });
    });
    connect(otherPanel_, &OtherToolsPanel::mindMapRequested, this, [this]() {
        otherPanel_->hide();
        QTimer::singleShot(0, this, [this]() {
            view_->setTool(BoardView::Tool::MindMap);
        });
    });
    connect(otherPanel_, &OtherToolsPanel::tableRequested, this, [this]() {
        otherPanel_->hide();
        QTimer::singleShot(0, this, [this]() {
            view_->setTool(BoardView::Tool::Table);
            showPanelAbove(tablePanel_, BoardView::Tool::Table);
        });
    });
    connect(otherPanel_, &OtherToolsPanel::textRequested, this, [this]() {
        otherPanel_->hide();
        QTimer::singleShot(0, this, [this]() {
            view_->setTool(BoardView::Tool::Text);
            showPanelAbove(textPanel_, BoardView::Tool::Text);
        });
    });
    connect(otherPanel_, &OtherToolsPanel::widgetRequested, this, [this]() {
        otherPanel_->hide();
        QTimer::singleShot(0, this, [this]() {
            widgetPanel_->setCurrent(widgetKind_);  // 恢复上次类型选择态
            view_->setTool(BoardView::Tool::Widget);
            showPanelAbove(widgetPanel_, BoardView::Tool::Widget);
        });
    });
    // ---------- 设置面板（入口在"更多"面板内） ----------
    connect(settingsPanel_, &SettingsPanel::backgroundSelected, this,
            &MainWindow::onBackgroundSelected);
    connect(settingsPanel_, &SettingsPanel::displayModeChanged, this, &MainWindow::setDisplayMode);
    connect(settingsPanel_, &SettingsPanel::toolBarModeChanged, this, &MainWindow::setToolBarMode);

    // ---------- 笔设置 / 滑动清屏面板 ----------
    connect(penPanel_, &PenSettingPanel::penChanged, this, &MainWindow::onPenChanged);
    connect(eraserPanel_, &EraserPanel::clearRequested, this, &MainWindow::onClear);
    // 笔 / 擦除面板关闭：按钮恢复普通选中态图；
    // 若因点击对应工具按钮在按下阶段自动关闭：置标志，防止释放阶段重开
    connect(penPanel_, &PenSettingPanel::closed, this, [this]() {
        toolBar_->setToolPanelExtended(BoardView::Tool::Pen, false);
        if ((QGuiApplication::mouseButtons() & Qt::LeftButton) &&
            toolBar_->toolButtonGlobalRect(BoardView::Tool::Pen).contains(QCursor::pos()))
            penButtonClosePending_ = true;
    });
    connect(eraserPanel_, &EraserPanel::closed, this, [this]() {
        toolBar_->setToolPanelExtended(BoardView::Tool::Eraser, false);
        // 按下阶段关闭置标志防释放阶段重开（底部=擦除按钮 / 圆盘=外环清屏钮）
        if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
            const bool onAnchor =
                diskMode_ ? disk_->diskGlobalRect().contains(QCursor::pos())
                          : toolBar_->toolButtonGlobalRect(BoardView::Tool::Eraser)
                                .contains(QCursor::pos());
            if (onAnchor)
                eraserButtonClosePending_ = true;
        }
    });

    // ---------- 图形 / 表格面板（工具栏弹出形状与行列选择） ----------
    connect(shapePanel_, &ShapePickerPanel::shapeSelected, this, [this](int kind) {
        shapePanel_->setCurrentKind(kind);
        view_->setShapeKind(kind);
        view_->setTool(BoardView::Tool::Shape);  // 选完形状切回图形工具继续绘制
    });
    connect(shapePanel_, &ShapePickerPanel::closed, this, [this]() {
        toolBar_->setToolPanelExtended(BoardView::Tool::Shape, false);
        // 按下阶段关闭置标志防释放阶段重开（底部=其他按钮 / 圆盘=外环图形钮）
        if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
            const bool onAnchor =
                diskMode_ ? disk_->diskGlobalRect().contains(QCursor::pos())
                          : toolBar_->toolButtonGlobalRect(BoardView::Tool::Shape)
                                .contains(QCursor::pos());
            if (onAnchor)
                shapeButtonClosePending_ = true;
        }
    });
    connect(tablePanel_, &TableSetupPanel::tableSizeChanged, this, [this](int rows, int cols) {
        tablePanel_->setCurrentSize(rows, cols);
        view_->setTableSize(rows, cols);
        view_->setTool(BoardView::Tool::Table);  // 选完行列切回表格工具继续绘制
    });
    connect(tablePanel_, &TableSetupPanel::closed, this, [this]() {
        toolBar_->setToolPanelExtended(BoardView::Tool::Table, false);
        // 按下阶段关闭置标志防释放阶段重开（底部=表格按钮 / 圆盘=外环表格钮）
        if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
            const bool onAnchor =
                diskMode_ ? disk_->diskGlobalRect().contains(QCursor::pos())
                          : toolBar_->toolButtonGlobalRect(BoardView::Tool::Table)
                                .contains(QCursor::pos());
            if (onAnchor)
                tableButtonClosePending_ = true;
        }
    });
    connect(textPanel_, &TextSetupPanel::fontSizeChanged, this, [this](int size) {
        textPanel_->setCurrentSize(size);
        view_->setTextFontSize(size);
        view_->setTool(BoardView::Tool::Text);  // 选完字号切回文字工具继续输入
    });
    connect(textPanel_, &TextSetupPanel::colorChanged, this, [this](uint32_t color) {
        textColor_ = color;
        view_->setTextColor(color);  // 新建文字颜色即时生效（面板保持展开，同笔面板）
    });
    connect(textPanel_, &TextSetupPanel::closed, this, [this]() {
        toolBar_->setToolPanelExtended(BoardView::Tool::Text, false);
        // 按下阶段关闭置标志防释放阶段重开（底部=文字按钮 / 圆盘=外环文字钮）
        if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
            const bool onAnchor =
                diskMode_ ? disk_->diskGlobalRect().contains(QCursor::pos())
                          : toolBar_->toolButtonGlobalRect(BoardView::Tool::Text)
                                .contains(QCursor::pos());
            if (onAnchor)
                textButtonClosePending_ = true;
        }
    });

    // ---------- 小工具面板（秒表/计时器/计算器/算盘/骰子/大转盘/点名器类型选择；参数在卡片内设置） ----------
    connect(widgetPanel_, &WidgetSetupPanel::widgetChosen, this, [this](int kind) {
        widgetKind_ = kind;
        widgetPanel_->setCurrent(kind);
        view_->setWidgetKind(kind);
        view_->setTool(BoardView::Tool::Widget);  // 选完类型切回小工具继续放置
    });
    connect(widgetPanel_, &WidgetSetupPanel::closed, this, [this]() {
        toolBar_->setToolPanelExtended(BoardView::Tool::Widget, false);
        // 按下阶段关闭置标志防释放阶段重开（底部=其他按钮 / 圆盘=外环小工具钮）
        if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
            const bool onAnchor =
                diskMode_ ? disk_->diskGlobalRect().contains(QCursor::pos())
                          : toolBar_->toolButtonGlobalRect(BoardView::Tool::Widget)
                                .contains(QCursor::pos());
            if (onAnchor)
                widgetButtonClosePending_ = true;
        }
    });

    // 初始状态：书写工具 + 笔宽 3 + 白色 + 100%
    view_->setPenColor(penColor_);
    view_->setPenWidth(penWidth_);
    view_->setTextColor(textColor_);  // 文字工具颜色（默认白色，独立于笔色）
    view_->setTool(BoardView::Tool::Pen);
    toolBar_->setCurrentTool(BoardView::Tool::Pen);
    toolBar_->setZoomPercent(100.0);
    disk_->setZoomPercent(100.0);
    disk_->setPenState(PenSettingPanel::PenKind::Normal, penColor_, penWidth_);  // 圆盘外环选中态
    penPanel_->setCurrent(PenSettingPanel::PenKind::Normal, penColor_, penWidth_);
    textPanel_->setCurrentColor(textColor_);
    view_->setWidgetKind(widgetKind_);  // 小工具默认秒表（类型 0~6，参数在卡片内设置）
    widgetPanel_->setCurrent(widgetKind_);

    // 默认黑板背景：内置网格图（参考 MaxWhiteboard Image.Background.Default）
    backgroundKey_ = QStringLiteral(":/images/board_bg_default.png");
    backgroundPixmap_ = QPixmap(backgroundKey_);
    view_->setBoardBackground(backgroundPixmap_);

    updatePageButtons();

    applyStyleSheet();
}

// ---------- 工具 ----------

void MainWindow::onToolSelected(BoardView::Tool tool) {
    // Popup 面板因点击本按钮在按下阶段自动关闭 → 同一次点击的释放阶段视为收起，不重开
    if (tool == BoardView::Tool::Pen && penButtonClosePending_) {
        penButtonClosePending_ = false;
        return;
    }
    if (tool == BoardView::Tool::Eraser && eraserButtonClosePending_) {
        eraserButtonClosePending_ = false;
        return;
    }
    if (tool == BoardView::Tool::Shape && shapeButtonClosePending_) {
        shapeButtonClosePending_ = false;
        return;
    }
    if (tool == BoardView::Tool::Table && tableButtonClosePending_) {
        tableButtonClosePending_ = false;
        return;
    }
    if (tool == BoardView::Tool::Text && textButtonClosePending_) {
        textButtonClosePending_ = false;
        return;
    }
    if (tool == BoardView::Tool::Widget && widgetButtonClosePending_) {
        widgetButtonClosePending_ = false;
        return;
    }

    // 参照 MaxWhiteboard：非该模式时点击仅切换到该模式（不弹设置面板）；
    // 已在该模式时再次点击工具按钮才弹出设置面板（再点收起）
    const bool alreadyActive = (view_->currentTool() == tool);

    view_->setTool(tool);  // toolChanged 回传同步工具栏选中态

    // 书写 / 擦除 / 图形 / 表格 / 文字 / 小工具：切换对应设置面板；其它工具收起全部面板
    if (tool == BoardView::Tool::Pen) {
        eraserPanel_->hide();
        shapePanel_->hide();
        tablePanel_->hide();
        textPanel_->hide();
        widgetPanel_->hide();
        if (alreadyActive)
            showPanelAbove(penPanel_, tool);
        else
            penPanel_->hide();
    } else if (tool == BoardView::Tool::Eraser) {
        penPanel_->hide();
        shapePanel_->hide();
        tablePanel_->hide();
        textPanel_->hide();
        widgetPanel_->hide();
        if (alreadyActive)
            showPanelAbove(eraserPanel_, tool);
        else
            eraserPanel_->hide();
    } else if (tool == BoardView::Tool::Shape) {
        penPanel_->hide();
        eraserPanel_->hide();
        tablePanel_->hide();
        textPanel_->hide();
        widgetPanel_->hide();
        if (alreadyActive)
            showPanelAbove(shapePanel_, tool);
        else
            shapePanel_->hide();
    } else if (tool == BoardView::Tool::Table) {
        penPanel_->hide();
        eraserPanel_->hide();
        shapePanel_->hide();
        textPanel_->hide();
        widgetPanel_->hide();
        if (alreadyActive)
            showPanelAbove(tablePanel_, tool);
        else
            tablePanel_->hide();
    } else if (tool == BoardView::Tool::Text) {
        penPanel_->hide();
        eraserPanel_->hide();
        shapePanel_->hide();
        tablePanel_->hide();
        widgetPanel_->hide();
        if (alreadyActive)
            showPanelAbove(textPanel_, tool);
        else
            textPanel_->hide();
    } else if (tool == BoardView::Tool::Widget) {
        penPanel_->hide();
        eraserPanel_->hide();
        shapePanel_->hide();
        tablePanel_->hide();
        textPanel_->hide();
        // 单段式：点击小工具按钮直接展开类型面板（不再要求先激活工具）；面板已
        // 展开时再次点击的收起由 pending 标志与 showPanelAbove 处理
        widgetPanel_->setCurrent(widgetKind_);  // 恢复上次选择态
        showPanelAbove(widgetPanel_, tool);
    } else {
        penPanel_->hide();
        eraserPanel_->hide();
        shapePanel_->hide();
        tablePanel_->hide();
        textPanel_->hide();
        widgetPanel_->hide();
    }
}

// 弹窗面板锚点：底部模式取工具栏对应按钮矩形（无按钮则退回"其他"按钮，
// 5 类工具已移入"其他"面板）；圆盘模式取圆盘矩形
QRect MainWindow::anchorRect(BoardView::Tool tool) const {
    if (diskMode_)
        return disk_->diskGlobalRect();
    QRect rect = toolBar_->toolButtonGlobalRect(tool);
    if (rect.isNull())
        rect = toolBar_->otherButtonGlobalRect();
    return rect;
}

// 更多/设置面板锚点：底部模式取更多按钮，圆盘模式取圆盘
QRect MainWindow::moreAnchorRect() const {
    if (diskMode_)
        return disk_->diskGlobalRect();
    return toolBar_->moreButtonGlobalRect();
}

// 面板水平居中对齐触发按钮，位于按钮上方 8px
void MainWindow::showPanelAbove(QWidget* panel, BoardView::Tool tool) {
    if (panel->isVisible()) {  // 再次点击同一工具按钮：收起面板
        panel->hide();
        return;
    }

    QRect buttonRect = anchorRect(tool);  // 锚点随工具栏模式（底部工具栏 / 圆盘）
    if (buttonRect.isNull())
        return;

    panel->adjustSize();
    QPoint pos(buttonRect.center().x() - panel->width() / 2,
               buttonRect.top() - panel->height() - 8);

    // 防止面板超出屏幕（多显示器 / 小窗口）
    QScreen* screen = QGuiApplication::screenAt(buttonRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        const int minX = available.left() + 8;
        const int maxX = available.right() - panel->width() - 8;
        pos.setX(maxX > minX ? qBound(minX, pos.x(), maxX) : minX);
        const int minY = available.top() + 8;
        const int maxY = available.bottom() - panel->height() - 8;
        pos.setY(maxY > minY ? qBound(minY, pos.y(), maxY) : minY);
    }
    panel->move(pos);
    panel->show();
    toolBar_->setToolPanelExtended(tool, true);  // 面板展开中：按钮换展开态图
}

// 笔设置变化（类型/颜色/宽度任一变化）：荧光笔在颜色高 8 位编码 30% 透明度
// （0x4D ≈ 30%；渲染层 colorToQColor 统一提取 alpha）；调整笔设置即切回书写
void MainWindow::onPenChanged(PenSettingPanel::PenKind kind, uint32_t color, int width) {
    penColor_ = (kind == PenSettingPanel::PenKind::Highlighter)
                    ? (color | 0x4D000000u)
                    : (color & 0x00FFFFFFu);
    penWidth_ = width;
    view_->setPenColor(penColor_);
    view_->setPenWidth(penWidth_);
    view_->setTool(BoardView::Tool::Pen);
    disk_->setPenState(kind, color, width);  // 圆盘外环选中态同步（两模式状态互通）
}

// 滑动清屏：清空当前页并切回书写
void MainWindow::onClear() {
    view_->clearBoard();
    view_->setTool(BoardView::Tool::Pen);
}

// ---------- 页面 ----------

void MainWindow::onPrevPage() {
    const QStringList pages = view_->pageIds();
    const int index = pages.indexOf(view_->currentPageId());
    if (index > 0)
        view_->selectPage(pages.at(index - 1));
}

void MainWindow::onNextPage() {
    const QStringList pages = view_->pageIds();
    const int index = pages.indexOf(view_->currentPageId());
    if (index >= 0 && index < pages.size() - 1)
        view_->selectPage(pages.at(index + 1));
}

void MainWindow::updatePageButtons() {
    const QStringList pages = view_->pageIds();
    const int index = pages.indexOf(view_->currentPageId());
    pageRail_->setPageInfo(index, pages.size());  // 页码标签 + 面板翻页钮可用态
    if (pageRail_->expanded())  // 面板展开中：同步刷新缩略图
        rebuildPageRail();
}

// 重建页面栏缩略图条目：全量页数据（数据层深拷贝）+ 当前页下标 + 当前背景铺底
void MainWindow::rebuildPageRail() {
    const std::vector<whiteboard::Page> pages = view_->allPages();
    const int index = view_->pageIds().indexOf(view_->currentPageId());
    pageRail_->rebuild(pages, index, view_->boardBackground());
}

// 点击缩略图：切换页面（面板保持展开，currentPageChanged → 重建条目）
void MainWindow::onPageActivated(int index) {
    const QStringList pages = view_->pageIds();
    if (index < 0 || index >= pages.size())
        return;
    view_->selectPage(pages.at(index));
}

// 删除指定页（数据层自动调整当前页并保证至少 1 页）
void MainWindow::onPageDeleteRequested(int index) {
    const QStringList pages = view_->pageIds();
    if (pages.size() <= 1 || index < 0 || index >= pages.size())
        return;
    view_->deletePage(pages.at(index));  // pagesChanged → updatePageButtons → 面板重建（高度自适应重定位）
}

// 复制指定页：插入源页之后并跳转到新页（数据层深拷贝 + 逐元素网络同步）
void MainWindow::onCopyPage(int index) {
    const QStringList pages = view_->pageIds();
    if (pages.size() >= BoardView::kMaxPages || index < 0 || index >= pages.size())
        return;
    view_->copyPage(pages.at(index));  // pagesChanged → updatePageButtons → 面板重建
}

// ---------- 设置 / 更多 / 保存打开 ----------

// 设置入口（来自"更多"面板）：弹出/收起设置面板
void MainWindow::onSettingsRequested() {
    if (settingsPanel_->isVisible()) {
        settingsPanel_->hide();
        return;
    }
    rebuildSettingsPanel();
    positionSettingsPanel();
    settingsPanel_->show();
}

// 更多按钮：弹出/收起更多面板
void MainWindow::onMoreRequested() {
    if (morePanel_->isVisible()) {
        morePanel_->hide();  // hideEvent 中复位按钮激活态
        return;
    }
    // 同一次点击的按下阶段 Popup 已自动关闭面板（closed 时置标志）→ 视为收起，不重开
    if (moreButtonClosePending_) {
        moreButtonClosePending_ = false;
        return;
    }
    positionMorePanel();
    morePanel_->show();
    toolBar_->setMoreButtonActive(true);
}

// 面板定位：水平居中于更多按钮/圆盘，位于其上方 8px（防出屏）
void MainWindow::positionMorePanel() {
    const QRect buttonRect = moreAnchorRect();  // 锚点随工具栏模式
    QPoint pos(buttonRect.center().x() - morePanel_->width() / 2,
               buttonRect.top() - morePanel_->height() - 8);

    QScreen* screen = QGuiApplication::screenAt(buttonRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        const int minX = available.left() + 8;
        const int maxX = available.right() - morePanel_->width() - 8;
        pos.setX(maxX > minX ? qBound(minX, pos.x(), maxX) : minX);
        const int minY = available.top() + 8;
        const int maxY = available.bottom() - morePanel_->height() - 8;
        pos.setY(maxY > minY ? qBound(minY, pos.y(), maxY) : minY);
    }
    morePanel_->move(pos);
}

// "其他"按钮：弹出/收起其他工具面板
void MainWindow::onOtherRequested() {
    if (otherPanel_->isVisible()) {
        otherPanel_->hide();  // hideEvent 中复位按钮激活态
        return;
    }
    // 同一次点击的按下阶段 Popup 已自动关闭面板（closed 时置标志）→ 视为收起，不重开
    if (otherButtonClosePending_) {
        otherButtonClosePending_ = false;
        return;
    }
    positionOtherPanel();
    otherPanel_->show();
    otherPanel_->setCurrentIndex(otherToolIndex(view_->currentTool()));  // 恢复当前工具选中态
    toolBar_->setOtherButtonActive(true);
}

// 面板定位：水平居中于"其他"按钮，位于其上方 8px（防出屏）
void MainWindow::positionOtherPanel() {
    const QRect buttonRect = toolBar_->otherButtonGlobalRect();
    QPoint pos(buttonRect.center().x() - otherPanel_->width() / 2,
               buttonRect.top() - otherPanel_->height() - 8);

    QScreen* screen = QGuiApplication::screenAt(buttonRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        const int minX = available.left() + 8;
        const int maxX = available.right() - otherPanel_->width() - 8;
        pos.setX(maxX > minX ? qBound(minX, pos.x(), maxX) : minX);
        const int minY = available.top() + 8;
        const int maxY = available.bottom() - otherPanel_->height() - 8;
        pos.setY(maxY > minY ? qBound(minY, pos.y(), maxY) : minY);
    }
    otherPanel_->move(pos);
}

// "其他"按钮激活态：面板展开中 或 当前工具属于 5 类（图形/导图/表格/文字/小工具）
void MainWindow::updateOtherButtonState() {
    toolBar_->setOtherButtonActive(otherPanel_->isVisible() ||
                                   otherToolIndex(view_->currentTool()) >= 0);
}

// 保存白板：选择路径 → 序列化全部页面（数据层深拷贝快照）写入文件
void MainWindow::onSaveBoard() {
    morePanel_->hide();
    const QString defaultPath = QDir::homePath() + QStringLiteral("/白板_%1.hrwb")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存白板"), defaultPath,
        QStringLiteral("HRTC 白板文件 (*.hrwb);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".hrwb"), Qt::CaseInsensitive))
        path += QStringLiteral(".hrwb");

    const std::vector<whiteboard::Page> pages = view_->allPages();
    // 桌面批注模式：跳过临时批注页；当前页为临时页时记录进入前页面
    const QString currentPageId = (view_->currentPageId() == desktopPageId_)
                                      ? desktopOriginPageId_
                                      : view_->currentPageId();
    QString error;
    if (!saveBoardToFile(path, pages, currentPageId, desktopPageId_, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    QToolTip::showText(QCursor::pos(), QStringLiteral("已保存到 %1").arg(path), this);
}

// 打开白板：选择文件 → 解析 → 全量替换数据层（ApplyFullSync 异步）+ 同步切换当前页
// （数据线程 FIFO 保证先应用后切页），UI 由 onSynced 回调自动全量重建
void MainWindow::onOpenBoard() {
    morePanel_->hide();
    if (desktopMode_)
        exitDesktopMode();  // 互斥：先退出桌面批注（临时页丢弃）再打开文件
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开白板"), QDir::homePath(),
        QStringLiteral("HRTC 白板文件 (*.hrwb);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    std::vector<whiteboard::Page> pages;
    QString currentPageId;
    QString error;
    if (!loadBoardFromFile(path, &pages, &currentPageId, &error)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        return;
    }
    if (pages.empty()) {
        QMessageBox::warning(this, QStringLiteral("打开失败"),
                             QStringLiteral("文件中没有可用的白板页面。"));
        return;
    }

    QtBoardData& data = view_->data();
    data.ApplyFullSync(pages);  // 异步：替换全部页面、清空历史并触发 onSynced
    if (!currentPageId.isEmpty())
        data.SelectPage(currentPageId.toStdString());  // 同步：在 ApplyFullSync 之后执行
    QToolTip::showText(QCursor::pos(), QStringLiteral("已打开 %1").arg(path), this);
}

// 重建背景网格：当前背景标识（每次展开刷新，同步扫描 Backgrounds 目录）
void MainWindow::rebuildSettingsPanel() {
    settingsPanel_->rebuild(backgroundKey_);
}

// 面板定位：水平居中于更多按钮/圆盘，位于其上方 8px（防出屏）
void MainWindow::positionSettingsPanel() {
    const QRect buttonRect = moreAnchorRect();  // 锚点随工具栏模式
    QPoint pos(buttonRect.center().x() - settingsPanel_->width() / 2,
               buttonRect.top() - settingsPanel_->height() - 8);

    QScreen* screen = QGuiApplication::screenAt(buttonRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        const int minX = available.left() + 8;
        const int maxX = available.right() - settingsPanel_->width() - 8;
        pos.setX(maxX > minX ? qBound(minX, pos.x(), maxX) : minX);
        const int minY = available.top() + 8;
        const int maxY = available.bottom() - settingsPanel_->height() - 8;
        pos.setY(maxY > minY ? qBound(minY, pos.y(), maxY) : minY);
    }
    settingsPanel_->move(pos);
}

// 应用黑板背景：画布底色立即生效；缩略图面板展开中则以其新背景重建
void MainWindow::onBackgroundSelected(const QString& key, const QPixmap& pixmap) {
    backgroundKey_ = key;
    backgroundPixmap_ = pixmap;
    view_->setBoardBackground(pixmap);
    if (pageRail_->expanded())  // 页面栏展开中：缩略图以新背景重建
        rebuildPageRail();
}

// ---------- 显示模式 ----------

// 切换显示模式：黑板模式全屏（16:9 居中、无标题栏、盖系统任务栏）；
// 窗口模式恢复为切换前的 16:9 窗口
void MainWindow::setDisplayMode(bool boardMode) {
    if (desktopMode_)
        exitDesktopMode();  // 互斥：桌面批注模式先退出再应用显示模式
    if (boardMode_ == boardMode)
        return;
    boardMode_ = boardMode;
    settingsPanel_->setDisplayMode(boardMode_);
    // 黑板模式下画布外区域（非 16:9 屏幕的黑边）呈纯黑（参考 DisplayWindow Background）
    central_->setStyleSheet(boardMode_ ? QStringLiteral("#boardCentral { background: #000000; }")
                                       : QString());
    if (boardMode_) {
        windowedGeometry_ = saveGeometry();  // 记录窗口模式几何，切回时恢复
        showFullScreen();
    } else {
        showNormal();
        restoreGeometry(windowedGeometry_);
    }
    updateBoardGeometry();
}

// 切换工具栏模式入口（设置面板）：桌面批注模式下先退出再应用
void MainWindow::setToolBarMode(bool diskMode) {
    if (desktopMode_)
        exitDesktopMode();
    applyToolBarMode(diskMode);
}

// 应用工具栏模式：底部工具栏 ↔ 桌面圆盘（悬浮球）；收起全部弹窗面板，
// 圆盘收起并回灌共享状态（工具/笔参数/橡皮大小/缩放/形状，两模式互通）；
// 进入/退出桌面批注模式直接调用（无桌面模式守卫）
void MainWindow::applyToolBarMode(bool diskMode) {
    if (diskMode_ == diskMode)
        return;
    diskMode_ = diskMode;
    settingsPanel_->setToolBarMode(diskMode_);  // 无信号同步选中态
    // 收起全部弹窗面板与页面栏面板（避免跨模式残留；Popup 关闭链复位按钮态）
    penPanel_->hide();
    eraserPanel_->hide();
    shapePanel_->hide();
    tablePanel_->hide();
    textPanel_->hide();
    widgetPanel_->hide();
    otherPanel_->hide();
    morePanel_->hide();
    settingsPanel_->hide();
    pageRail_->setExpanded(false);
    disk_->setExpanded(false);
    toolBar_->setVisible(!diskMode_);
    disk_->setVisible(diskMode_);
    if (diskMode_) {
        // 共享状态回灌圆盘（悬浮球图标/内环高亮/外环选中态）
        disk_->setCurrentTool(view_->currentTool());
        const bool highlighter = (penColor_ & 0xFF000000u) != 0;  // 荧光笔 alpha 位编码
        disk_->setPenState(highlighter ? PenSettingPanel::PenKind::Highlighter
                                       : PenSettingPanel::PenKind::Normal,
                           penColor_ & 0x00FFFFFFu, penWidth_);
        disk_->setEraserSize(view_->eraserSize());
        disk_->setZoomPercent(zoomPercent_);
    }
}

// 画布几何：窗口模式铺满中央区域（底部预留工具栏悬浮带 15px，与布局边距一致）；
// 黑板模式视口取 16:9 最大内接矩形居中（参考 DisplayWindow Viewbox Stretch=Uniform）
void MainWindow::updateBoardGeometry() {
    if (!central_ || !view_)
        return;
    const QRect area = central_->rect();
    QRect boardRect;
    if (desktopMode_) {
        boardRect = area;  // 桌面批注：画布铺满中央区域（无 16:9、无底部预留）
    } else if (!boardMode_) {
        boardRect = area.adjusted(0, 0, 0, -15);
    } else {
        int w = area.width();
        int h = qRound(w * 9.0 / 16.0);
        if (h > area.height()) {
            h = area.height();
            w = qRound(h * 16.0 / 9.0);
        }
        boardRect = QRect(area.x() + (area.width() - w) / 2,
                          area.y() + (area.height() - h) / 2, w, h);
    }
    view_->setGeometry(boardRect);
    // 页面栏/圆盘活动范围同步（首次各贴画布左/右缘垂直居中，之后随范围钮回）
    pageRail_->setBounds(boardRect);
    if (desktopMode_) {
        // 桌面批注：圆盘活动范围=中央区域全屏矩形（子部件可拖遍整个桌面）
        disk_->setBounds(area);
    } else {
        disk_->setBounds(boardRect);
    }
}

// 中央区域尺寸变化（窗口缩放 / 全屏切换）→ 重算画布几何
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == central_ && event->type() == QEvent::Resize)
        updateBoardGeometry();
    return QMainWindow::eventFilter(watched, event);
}

// 全屏状态被外部改变（如系统操作）时同步黑板模式
void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange)
        return;
    if (desktopMode_)
        return;  // 桌面批注模式：全屏切换由状态机控制，跳过黑板模式同步
    const bool full = isFullScreen();
    if (full == boardMode_)
        return;
    boardMode_ = full;
    settingsPanel_->setDisplayMode(boardMode_);
    central_->setStyleSheet(boardMode_ ? QStringLiteral("#boardCentral { background: #000000; }")
                                       : QString());
    updateBoardGeometry();
}

// ---------- 桌面批注模式 ----------

// 进入桌面批注模式：主窗口变全屏无边框置顶透明覆盖窗（透出真实桌面），
// 自动新建空白临时批注页承载批注（退出即删），工具栏自动切圆盘
//（子部件天然浮于画布之上，无顶层 z 序竞争，可拖遍整个桌面）
void MainWindow::enterDesktopMode() {
    if (desktopMode_)
        return;
    // 护栏：页面数达上限（临时批注页需占 1 页配额）
    if (view_->pageIds().size() >= BoardView::kMaxPages) {
        QToolTip::showText(QCursor::pos(),
                           QStringLiteral("页面数已达上限（%1 页），无法进入桌面批注")
                               .arg(BoardView::kMaxPages),
                           this);
        return;
    }

    // 收起全部弹窗面板与页面栏（桌面下只保留圆盘）
    penPanel_->hide();
    eraserPanel_->hide();
    shapePanel_->hide();
    tablePanel_->hide();
    textPanel_->hide();
    widgetPanel_->hide();
    otherPanel_->hide();
    morePanel_->hide();
    settingsPanel_->hide();
    pageRail_->setExpanded(false);
    pageRail_->hide();

    // 记录进入前状态（退出时恢复；须在切笔/建页之前记录）
    preDesktopTool_ = view_->currentTool();
    preDesktopDiskMode_ = diskMode_;
    preDesktopBoardMode_ = boardMode_;
    if (!boardMode_)
        desktopSavedGeometry_ = saveGeometry();  // 窗口模式几何（黑板模式切回仍全屏）

    // 切书写：提交进行中的文字编辑（避免随后的 reloadPage 丢弃未提交文本）
    view_->setTool(BoardView::Tool::Pen);

    // 进入标志须先置位：showFullScreen 触发的 WindowStateChange 靠它跳过黑板模式同步
    desktopMode_ = true;

    // 新建空白临时批注页（插入当前页后并自动切换；退出时删除）
    desktopOriginPageId_ = view_->currentPageId();
    view_->createPage();
    desktopPageId_ = view_->currentPageId();
    view_->setTool(BoardView::Tool::Pen);

    // 主窗口透明化：半透明底 + 无边框 + 置顶 + 全屏 + QSS 透明
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 黑屏根因修复：setWindowFlags 在父子关系不变时不会销毁既有平台窗口，而窗口
    // 表面的 alpha 格式仅在平台窗口创建时捕获——须显式销毁令其带 alpha 重建，
    // 否则透明失效（渲染走 BitBlt，透明区变纯黑盖住整个桌面）
    if (QWindow* wh = windowHandle()) {
        wh->destroy();
        QSurfaceFormat fmt = wh->requestedFormat();
        fmt.setAlphaBufferSize(8);
        wh->setFormat(fmt);
    }
    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: transparent; }
    )"));
    // 中央区域自身 QSS 优先级高于窗口级：黑板模式下残留的纯黑底色须一并改透明
    central_->setStyleSheet(QStringLiteral("#boardCentral { background: transparent; }"));
    showFullScreen();
    view_->setBackgroundTransparent(true);

    // 工具栏切圆盘模式（共享状态回灌；底部工具栏随之隐藏）
    applyToolBarMode(true);

    // 圆盘保持中央区域子部件：全屏覆盖窗下与顶层窗等效（可拖遍整个桌面），
    // 且子部件永远浮于主窗内容之上，不参与顶层 z 序竞争（避免被主窗遮挡/失点）
    disk_->setVisible(true);
    disk_->raise();
    disk_->setExpanded(false);

    updateBoardGeometry();
}

// 退出桌面批注模式（反向于进入）：删除临时批注页（批注丢弃）并切回进入前页面，
// 圆盘回中央区域子部件，恢复窗口/背景/工具栏模式
void MainWindow::exitDesktopMode() {
    if (!desktopMode_)
        return;

    // 强制关闭点击穿透（恢复正常鼠标交互）
    setClickThrough(false);

    // 删除临时批注页；切回进入前页面（存在性守卫：联网等场景可能已被移除）
    if (!desktopPageId_.isEmpty() && view_->pageIds().contains(desktopPageId_))
        view_->deletePage(desktopPageId_);
    if (!desktopOriginPageId_.isEmpty() &&
        view_->pageIds().contains(desktopOriginPageId_))
        view_->selectPage(desktopOriginPageId_);
    desktopPageId_.clear();
    desktopOriginPageId_.clear();

    // 圆盘保持中央区域子部件（按进入前工具栏模式显示）；退出即收起为悬浮球
    //（与进入对称：applyToolBarMode 在磁盘模式直回时不会代收）
    disk_->setVisible(preDesktopDiskMode_);
    disk_->setExpanded(false);
    if (preDesktopDiskMode_)
        disk_->raise();

    // 恢复工具栏模式与页面栏
    applyToolBarMode(preDesktopDiskMode_);
    if (!preDesktopDiskMode_)
        toolBar_->setVisible(true);  // 原底部工具栏模式：确保工具栏可见
    pageRail_->show();

    // 恢复显示模式标志（窗口操作期间 desktopMode_ 保持置位：changeEvent 全部抑制，
    // 全屏/窗口切换只由本函数掌控，避免 setWindowFlags 隐藏窗口触发误同步）
    boardMode_ = preDesktopBoardMode_;

    // 去透明：恢复窗口标志与样式（黑板模式中央区域仍为纯黑）
    setAttribute(Qt::WA_TranslucentBackground, false);
    setWindowFlags(Qt::Window);
    // 对称修复：销毁平台窗口令其以"无 alpha"表面重建，恢复常规不透明渲染路径
    //（否则残留 alpha 表面在窗口模式下产生黑色未绘制区域）
    if (QWindow* wh = windowHandle())
        wh->destroy();
    applyStyleSheet();
    central_->setStyleSheet(boardMode_ ? QStringLiteral("#boardCentral { background: #000000; }")
                                       : QString());
    view_->setBackgroundTransparent(false);

    // 恢复窗口显示模式与几何
    if (boardMode_) {
        showFullScreen();
    } else {
        showNormal();
        restoreGeometry(desktopSavedGeometry_);
    }

    desktopMode_ = false;  // 窗口态已就绪：恢复 changeEvent 常规同步
    updateBoardGeometry();
    view_->setTool(preDesktopTool_);  // 恢复进入前工具
}

// 桌面批注点击穿透（"鼠标"工具）：只置标志，实际命中测试在 nativeEvent 处理——
// 穿透开启时画布区域对鼠标透明（可操作电脑/其他应用），批注与圆盘仍渲染可见，
// 圆盘区域保持可交互（点内环批注工具切回）；非 Windows 仅置标志（空实现）
void MainWindow::setClickThrough(bool on) {
    clickThrough_ = on;
    // 画布背景填充策略随之切换：穿透时 alpha=0（系统级穿透鼠标），
    // 批注时 alpha=1（保证画布可命中可书写）
    view_->setClickThrough(on);
}

// 点击穿透命中测试：桌面模式 + 穿透开启时，除圆盘区域外一律返回 HTTRANSPARENT
//（命中测试继续下发到下层窗口 → 桌面/其他应用收到鼠标）；圆盘区域走默认处理，
// 保持"穿透时点圆盘仍可交互"（Qt 将事件路由到圆盘子部件）
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, long* result) {
#ifdef Q_OS_WIN
    if (desktopMode_ && clickThrough_ && eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            const QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            const QPoint diskPos = disk_->mapFromGlobal(globalPos);
            if (disk_->isVisible() && disk_->rect().contains(diskPos))
                return false;  // 圆盘区域：默认命中（保持可交互）
            *result = HTTRANSPARENT;
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ---------- 互动白板 ----------

void MainWindow::onInteract() {
    if (session_ && session_->IsActive()) {
        // 已连接：断开
        session_->Stop();
        updateSessionStatus(QStringLiteral("离线"));
        return;
    }

    JoinRoomDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString ip = dlg.serverIp();
    const QString roomId = dlg.roomId();
    const QString userId = dlg.userId();
    if (ip.isEmpty() || roomId.isEmpty() || userId.isEmpty())
        return;

    session_ = std::make_unique<WhiteboardSession>(view_->data(), this);
    connect(session_.get(), &WhiteboardSession::joined, this, [this, ip, roomId]() {
        updateSessionStatus(QStringLiteral("已连接 %1@%2")
                                .arg(QString::fromStdString(session_->UserId()))
                                .arg(roomId + QStringLiteral("@") + ip));
    });
    connect(session_.get(), &WhiteboardSession::disconnected, this, [this]() {
        updateSessionStatus(QStringLiteral("离线"));
    });
    connect(session_.get(), &WhiteboardSession::errorOccurred, this, [this](const QString& msg) {
        updateSessionStatus(msg.left(48));
    });

    if (!session_->Start(ip.toStdString(), dlg.serverPort(),
                         roomId.toStdString(), userId.toStdString())) {
        updateSessionStatus(QStringLiteral("连接失败"));
    }
}

// 退出程序：关闭应用（"更多"面板入口；会话对象随窗口析构自动断开）
void MainWindow::onExitApp() {
    QApplication::quit();
}

void MainWindow::updateSessionStatus(const QString& text) {
    if (text == QStringLiteral("离线"))
        setWindowTitle(windowTitleBase_);
    else
        setWindowTitle(windowTitleBase_ + QStringLiteral(" — ") + text);
}

// ---------- 样式 ----------

// 16:9 等比锁定：以相对变化更大的方向驱动另一方向（拖宽同步高，拖高同步宽）
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (aspectLockGuard_ || isMaximized() || isFullScreen())
        return;
    const QSize current = event->size();
    const QSize oldSize = event->oldSize();
    if (!oldSize.isValid() || current.width() <= 0 || current.height() <= 0)
        return;

    const double dw = qAbs(current.width() - oldSize.width()) /
                      static_cast<double>(oldSize.width());
    const double dh = qAbs(current.height() - oldSize.height()) /
                      static_cast<double>(oldSize.height());
    int w = current.width();
    int h = current.height();
    if (dw >= dh)
        h = qRound(w * 9.0 / 16.0);  // 宽度驱动：按 16:9 求高
    else
        w = qRound(h * 16.0 / 9.0);  // 高度驱动：按 16:9 求宽
    if (w == current.width() && h == current.height())
        return;

    aspectLockGuard_ = true;
    resize(w, h);
    aspectLockGuard_ = false;
}

void MainWindow::applyStyleSheet() {
    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: #152A20; }
        #boardCentral { background: #152A20; }
    )"));
}
