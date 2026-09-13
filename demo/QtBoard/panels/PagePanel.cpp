#include "PagePanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const int kExpandedWidth = 190;
const int kCollapsedWidth = 32;
}  // namespace

PagePanel::PagePanel(QWidget* parent) : QWidget(parent) {
    buildUi();
    applyCollapsedState();
}

void PagePanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 顶部标题行：标题 + 折叠按钮
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("pagePanelHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 6, 4, 6);

    auto* title = new QLabel(QStringLiteral("页面"), header);
    title->setObjectName(QStringLiteral("pagePanelTitle"));
    headerLayout->addWidget(title);

    collapseButton_ = new QToolButton(header);
    collapseButton_->setObjectName(QStringLiteral("collapseButton"));
    collapseButton_->setText(QStringLiteral("«"));
    collapseButton_->setToolTip(QStringLiteral("折叠/展开功能区"));
    collapseButton_->setCursor(Qt::PointingHandCursor);
    headerLayout->addStretch();
    headerLayout->addWidget(collapseButton_);
    root->addWidget(header);

    connect(collapseButton_, &QToolButton::clicked, this, [this]() {
        setCollapsed(!collapsed_);
    });

    // 内容区：新建/删除按钮 + 页面列表
    contentWidget_ = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(contentWidget_);
    contentLayout->setContentsMargins(8, 4, 8, 8);
    contentLayout->setSpacing(6);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);

    addButton_ = new QToolButton(contentWidget_);
    addButton_->setText(QStringLiteral("+ 新建"));
    addButton_->setToolTip(QStringLiteral("添加新页面"));
    addButton_->setCursor(Qt::PointingHandCursor);
    addButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(addButton_, &QToolButton::clicked, this, &PagePanel::createPageRequested);

    deleteButton_ = new QToolButton(contentWidget_);
    deleteButton_->setText(QStringLiteral("删除"));
    deleteButton_->setToolTip(QStringLiteral("删除当前页面"));
    deleteButton_->setCursor(Qt::PointingHandCursor);
    connect(deleteButton_, &QToolButton::clicked, this, [this]() {
        if (currentPageId_.isEmpty())
            return;
        emit deletePageRequested(currentPageId_);
    });

    buttonRow->addWidget(addButton_);
    buttonRow->addWidget(deleteButton_);
    contentLayout->addLayout(buttonRow);

    pageList_ = new QListWidget(contentWidget_);
    pageList_->setObjectName(QStringLiteral("pageList"));
    pageList_->setSelectionMode(QAbstractItemView::SingleSelection);
    contentLayout->addWidget(pageList_);

    connect(pageList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem* /*previous*/) {
                if (suppressListSignal_ || !current)
                    return;
                const QString id = current->data(Qt::UserRole).toString();
                if (!id.isEmpty() && id != currentPageId_)
                    emit selectPageRequested(id);
            });

    root->addWidget(contentWidget_);
}

void PagePanel::setPages(const QStringList& pageIds, const QString& currentId) {
    pageIds_ = pageIds;
    currentPageId_ = currentId;

    suppressListSignal_ = true;
    pageList_->clear();
    for (int i = 0; i < pageIds_.size(); ++i) {
        auto* item = new QListWidgetItem(
            QStringLiteral("第 %1 页").arg(i + 1), pageList_);
        item->setData(Qt::UserRole, pageIds_.at(i));
        if (pageIds_.at(i) == currentPageId_)
            pageList_->setCurrentItem(item);
    }
    suppressListSignal_ = false;

    // 只有一页时禁用删除
    deleteButton_->setEnabled(pageIds_.size() > 1);
}

void PagePanel::setCollapsed(bool collapsed) {
    if (collapsed_ == collapsed)
        return;
    collapsed_ = collapsed;
    applyCollapsedState();
}

void PagePanel::applyCollapsedState() {
    contentWidget_->setVisible(!collapsed_);
    collapseButton_->setText(collapsed_ ? QStringLiteral("»")
                                        : QStringLiteral("«"));
    setFixedWidth(collapsed_ ? kCollapsedWidth : kExpandedWidth);
}
