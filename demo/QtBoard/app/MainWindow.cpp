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

#include <chrono>

#include "BoardToolBar.h"
#include "BoardUtil.h"
#include "EraserPanel.h"
#include "JoinRoomDialog.h"
#include "MorePanel.h"
#include "PenSettingPanel.h"
#include "SettingsPanel.h"
#include "SlideManagerPanel.h"

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

// 保存白板数据到文件（JSON；失败时 error 为原因）
bool saveBoardToFile(const QString& path, const std::vector<whiteboard::Page>& pages,
                     const QString& currentPageId, QString* error) {
    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kBoardFileFormat));
    root.insert(QStringLiteral("version"), kBoardFileVersion);
    root.insert(QStringLiteral("currentPageId"), currentPageId);

    QJsonArray pageArr;
    for (const whiteboard::Page& page : pages) {
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

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    windowTitleBase_ = QStringLiteral("HRTC 黑板 Demo");
    setWindowTitle(windowTitleBase_);
    resize(1280, 720);           // 16:9（与白板基准 1920x1080 等比）
    setMinimumSize(960, 540);    // 16:9 最小尺寸

    view_ = new BoardView(this);
    toolBar_ = new BoardToolBar(this);
    penPanel_ = new PenSettingPanel(this);
    eraserPanel_ = new EraserPanel(this);
    pagePanel_ = new SlideManagerPanel(this);
    settingsPanel_ = new SettingsPanel(this);
    morePanel_ = new MorePanel(this);

    // 中央区域：画布手动几何（黑板模式 16:9 居中，窗口模式铺满）+ 工具栏悬浮底部居中（距底 15px）
    central_ = new QWidget(this);
    central_->setObjectName(QStringLiteral("boardCentral"));
    auto* grid = new QGridLayout(central_);
    grid->setContentsMargins(0, 0, 0, 15);
    grid->setSpacing(0);
    view_->setParent(central_);  // 画布不入布局：几何由 updateBoardGeometry() 控制
    grid->addWidget(toolBar_, 0, 0, Qt::AlignBottom | Qt::AlignHCenter);
    setCentralWidget(central_);
    toolBar_->raise();
    central_->installEventFilter(this);  // 中央区域尺寸变化时重算画布几何
    updateBoardGeometry();               // 初始几何

    // ---------- 工具 / 功能组 ----------
    connect(toolBar_, &BoardToolBar::toolSelected, this, &MainWindow::onToolSelected);
    connect(view_, &BoardView::toolChanged, this, [this](BoardView::Tool tool) {
        toolBar_->setCurrentTool(tool);
    });
    connect(toolBar_, &BoardToolBar::undoRequested, view_, &BoardView::undo);
    connect(toolBar_, &BoardToolBar::redoRequested, view_, &BoardView::redo);
    connect(toolBar_, &BoardToolBar::zoomInRequested, view_, &BoardView::zoomIn);
    connect(toolBar_, &BoardToolBar::zoomOutRequested, view_, &BoardView::zoomOut);
    connect(toolBar_, &BoardToolBar::zoomResetRequested, view_, &BoardView::resetZoom);
    connect(view_, &BoardView::zoomChanged, this, [this](qreal percent) {
        toolBar_->setZoomPercent(percent);
    });

    // ---------- 页面组 ----------
    connect(toolBar_, &BoardToolBar::addPageRequested, view_, &BoardView::createPage);
    connect(toolBar_, &BoardToolBar::prevPageRequested, this, &MainWindow::onPrevPage);
    connect(toolBar_, &BoardToolBar::nextPageRequested, this, &MainWindow::onNextPage);
    connect(toolBar_, &BoardToolBar::pagePanelRequested, this, &MainWindow::onPagePanelRequested);
    connect(view_, &BoardView::pagesChanged, this, &MainWindow::updatePageButtons);
    connect(view_, &BoardView::currentPageChanged, this, &MainWindow::updatePageButtons);

    // 页数缩略图面板：选页 / 删页 / 关闭后复位按钮激活态
    connect(pagePanel_, &SlideManagerPanel::pageActivated, this, &MainWindow::onPageActivated);
    connect(pagePanel_, &SlideManagerPanel::pageDeleteRequested, this,
            &MainWindow::onPageDeleteRequested);
    connect(pagePanel_, &SlideManagerPanel::closed, this, [this]() {
        toolBar_->setPageButtonActive(false);
        // Popup 因点击页码按钮而在按下阶段自动关闭：置标志，
        // 防止同一次点击的释放阶段（clicked）重开面板
        if ((QGuiApplication::mouseButtons() & Qt::LeftButton) &&
            toolBar_->pageButtonGlobalRect().contains(QCursor::pos()))
            pageButtonClosePending_ = true;
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
        // Popup 因点击更多按钮而在按下阶段自动关闭：置标志，
        // 防止同一次点击的释放阶段（clicked）重开面板
        if ((QGuiApplication::mouseButtons() & Qt::LeftButton) &&
            toolBar_->moreButtonGlobalRect().contains(QCursor::pos()))
            moreButtonClosePending_ = true;
    });
    // ---------- 设置面板（入口在"更多"面板内） ----------
    connect(settingsPanel_, &SettingsPanel::backgroundSelected, this,
            &MainWindow::onBackgroundSelected);
    connect(settingsPanel_, &SettingsPanel::displayModeChanged, this, &MainWindow::setDisplayMode);

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
        if ((QGuiApplication::mouseButtons() & Qt::LeftButton) &&
            toolBar_->toolButtonGlobalRect(BoardView::Tool::Eraser).contains(QCursor::pos()))
            eraserButtonClosePending_ = true;
    });

    // 初始状态：书写工具 + 笔宽 3 + 白色 + 100%
    view_->setPenColor(penColor_);
    view_->setPenWidth(penWidth_);
    view_->setTool(BoardView::Tool::Pen);
    toolBar_->setCurrentTool(BoardView::Tool::Pen);
    toolBar_->setZoomPercent(100.0);
    penPanel_->setCurrent(PenSettingPanel::PenKind::Normal, penColor_, penWidth_);

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

    // 参照 MaxWhiteboard：非该模式时点击仅切换到该模式（不弹设置面板）；
    // 已在该模式时再次点击工具按钮才弹出设置面板（再点收起）
    const bool alreadyActive = (view_->currentTool() == tool);

    view_->setTool(tool);  // toolChanged 回传同步工具栏选中态

    // 书写 / 擦除：切换对应设置面板；其它工具收起全部面板
    if (tool == BoardView::Tool::Pen) {
        eraserPanel_->hide();
        if (alreadyActive)
            showPanelAbove(penPanel_, tool);
        else
            penPanel_->hide();
    } else if (tool == BoardView::Tool::Eraser) {
        penPanel_->hide();
        if (alreadyActive)
            showPanelAbove(eraserPanel_, tool);
        else
            eraserPanel_->hide();
    } else {
        penPanel_->hide();
        eraserPanel_->hide();
    }
}

// 面板水平居中对齐触发按钮，位于按钮上方 8px
void MainWindow::showPanelAbove(QWidget* panel, BoardView::Tool tool) {
    if (panel->isVisible()) {  // 再次点击同一工具按钮：收起面板
        panel->hide();
        return;
    }

    const QRect buttonRect = toolBar_->toolButtonGlobalRect(tool);
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
    toolBar_->setPageInfo(index + 1, pages.size());
    toolBar_->setPrevNextEnabled(index > 0, index >= 0 && index < pages.size() - 1);
    toolBar_->setAddPageEnabled(pages.size() < BoardView::kMaxPages);  // 页数上限 20
    if (pagePanel_->isVisible())  // 面板展开中：同步刷新缩略图
        rebuildPagePanel();
}

// 页码按钮：弹出/收起页数缩略图面板
void MainWindow::onPagePanelRequested() {
    if (pagePanel_->isVisible()) {
        pagePanel_->hide();  // hideEvent 中复位按钮激活态
        return;
    }
    // 同一次点击的按下阶段 Popup 已自动关闭面板（closed 时置标志）→ 视为收起，不重开
    if (pageButtonClosePending_) {
        pageButtonClosePending_ = false;
        return;
    }
    rebuildPagePanel();
    positionPagePanel();
    pagePanel_->show();
    toolBar_->setPageButtonActive(true);
}

// 重建缩略图条目：全量页数据（数据层深拷贝）+ 当前页下标 + 当前背景铺底
void MainWindow::rebuildPagePanel() {
    const std::vector<whiteboard::Page> pages = view_->allPages();
    const int index = view_->pageIds().indexOf(view_->currentPageId());
    pagePanel_->rebuild(pages, index, view_->boardBackground());
}

// 面板定位：水平居中于页码按钮，位于其上方 8px（防出屏）
void MainWindow::positionPagePanel() {
    const QRect buttonRect = toolBar_->pageButtonGlobalRect();
    QPoint pos(buttonRect.center().x() - pagePanel_->width() / 2,
               buttonRect.top() - pagePanel_->height() - 8);

    QScreen* screen = QGuiApplication::screenAt(buttonRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        const int minX = available.left() + 8;
        const int maxX = available.right() - pagePanel_->width() - 8;
        pos.setX(maxX > minX ? qBound(minX, pos.x(), maxX) : minX);
        const int minY = available.top() + 8;
        const int maxY = available.bottom() - pagePanel_->height() - 8;
        pos.setY(maxY > minY ? qBound(minY, pos.y(), maxY) : minY);
    }
    pagePanel_->move(pos);
}

// 点击缩略图：切换页面并关闭面板
void MainWindow::onPageActivated(int index) {
    const QStringList pages = view_->pageIds();
    if (index < 0 || index >= pages.size())
        return;
    pagePanel_->hide();
    view_->selectPage(pages.at(index));
}

// 删除指定页（数据层自动调整当前页并保证至少 1 页）
void MainWindow::onPageDeleteRequested(int index) {
    const QStringList pages = view_->pageIds();
    if (pages.size() <= 1 || index < 0 || index >= pages.size())
        return;
    view_->deletePage(pages.at(index));  // pagesChanged → updatePageButtons → 面板重建
    positionPagePanel();                 // 高度变化后重新定位
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

// 面板定位：水平居中于更多按钮，位于其上方 8px（防出屏）
void MainWindow::positionMorePanel() {
    const QRect buttonRect = toolBar_->moreButtonGlobalRect();
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
    const QString currentPageId = view_->currentPageId();
    QString error;
    if (!saveBoardToFile(path, pages, currentPageId, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    QToolTip::showText(QCursor::pos(), QStringLiteral("已保存到 %1").arg(path), this);
}

// 打开白板：选择文件 → 解析 → 全量替换数据层（ApplyFullSync 异步）+ 同步切换当前页
// （数据线程 FIFO 保证先应用后切页），UI 由 onSynced 回调自动全量重建
void MainWindow::onOpenBoard() {
    morePanel_->hide();
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

// 面板定位：水平居中于设置按钮，位于其上方 8px（防出屏）
void MainWindow::positionSettingsPanel() {
    const QRect buttonRect = toolBar_->moreButtonGlobalRect();
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
    if (pagePanel_->isVisible())
        rebuildPagePanel();
}

// ---------- 显示模式 ----------

// 切换显示模式：黑板模式全屏（16:9 居中、无标题栏、盖系统任务栏）；
// 窗口模式恢复为切换前的 16:9 窗口
void MainWindow::setDisplayMode(bool boardMode) {
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

// 画布几何：窗口模式铺满中央区域（底部预留工具栏悬浮带 15px，与布局边距一致）；
// 黑板模式视口取 16:9 最大内接矩形居中（参考 DisplayWindow Viewbox Stretch=Uniform）
void MainWindow::updateBoardGeometry() {
    if (!central_ || !view_)
        return;
    const QRect area = central_->rect();
    if (!boardMode_) {
        view_->setGeometry(area.adjusted(0, 0, 0, -15));
        return;
    }
    int w = area.width();
    int h = qRound(w * 9.0 / 16.0);
    if (h > area.height()) {
        h = area.height();
        w = qRound(h * 16.0 / 9.0);
    }
    view_->setGeometry(QRect(area.x() + (area.width() - w) / 2,
                             area.y() + (area.height() - h) / 2, w, h));
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
    const bool full = isFullScreen();
    if (full == boardMode_)
        return;
    boardMode_ = full;
    settingsPanel_->setDisplayMode(boardMode_);
    central_->setStyleSheet(boardMode_ ? QStringLiteral("#boardCentral { background: #000000; }")
                                       : QString());
    updateBoardGeometry();
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
