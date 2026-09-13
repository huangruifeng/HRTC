#include "JoinRoomDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QVBoxLayout>

JoinRoomDialog::JoinRoomDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("加入互动房间"));
    setModal(true);

    ipEdit_ = new QLineEdit(QStringLiteral("127.0.0.1"), this);

    portSpin_ = new QSpinBox(this);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(8083);

    roomEdit_ = new QLineEdit(QStringLiteral("room-1"), this);

    // 默认用户名：user-随机数
    const quint32 rnd = QRandomGenerator::global()->generate();
    userEdit_ = new QLineEdit(QStringLiteral("user-%1").arg(rnd % 100000), this);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("服务器 IP"), ipEdit_);
    form->addRow(QStringLiteral("端口"), portSpin_);
    form->addRow(QStringLiteral("房间号"), roomEdit_);
    form->addRow(QStringLiteral("用户名"), userEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("加入"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString JoinRoomDialog::serverIp() const {
    return ipEdit_->text().trimmed();
}

int JoinRoomDialog::serverPort() const {
    return portSpin_->value();
}

QString JoinRoomDialog::roomId() const {
    return roomEdit_->text().trimmed();
}

QString JoinRoomDialog::userId() const {
    return userEdit_->text().trimmed();
}
