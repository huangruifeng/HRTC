#pragma once
#include <QStringList>
#include <QWidget>

class QListWidget;
class QToolButton;

// 左侧画布功能区：页面列表（单击选择）、新建/删除页面按钮、可折叠为窄条。
class PagePanel : public QWidget {
    Q_OBJECT
public:
    explicit PagePanel(QWidget* parent = nullptr);

    // 同步页面列表与当前页（不改变折叠状态）
    void setPages(const QStringList& pageIds, const QString& currentId);
    bool isCollapsed() const { return collapsed_; }

public slots:
    void setCollapsed(bool collapsed);

signals:
    void createPageRequested();
    void deletePageRequested(const QString& pageId);
    void selectPageRequested(const QString& pageId);

private:
    void buildUi();
    void applyCollapsedState();

    QWidget* contentWidget_ = nullptr;      // 展开时的内容区
    QToolButton* collapseButton_ = nullptr; // 折叠/展开切换按钮
    QListWidget* pageList_ = nullptr;
    QToolButton* addButton_ = nullptr;
    QToolButton* deleteButton_ = nullptr;

    bool collapsed_ = false;
    bool suppressListSignal_ = false;
    QStringList pageIds_;
    QString currentPageId_;
};
