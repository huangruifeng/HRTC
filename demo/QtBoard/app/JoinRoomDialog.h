#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QSpinBox;

// 加入房间对话框：服务器 IP / 端口 / 房间号 / 用户名，确定/取消
class JoinRoomDialog : public QDialog {
    Q_OBJECT
public:
    explicit JoinRoomDialog(QWidget* parent = nullptr);

    QString serverIp() const;
    int serverPort() const;
    QString roomId() const;
    QString userId() const;

private:
    QLineEdit* ipEdit_ = nullptr;
    QSpinBox* portSpin_ = nullptr;
    QLineEdit* roomEdit_ = nullptr;
    QLineEdit* userEdit_ = nullptr;
};
