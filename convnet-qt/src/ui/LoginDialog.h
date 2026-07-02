#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QRadioButton;
class QCheckBox;
class QLabel;

// 登录 / 注册对话框。账号密码认证：登录用 [账号,密码]，注册另需 [昵称,确认密码]。
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent = nullptr);

    bool    isRegister() const;
    QString account() const;
    QString password() const;
    QString nick() const;
    QString host() const;
    quint16 port() const;
    bool    rememberPassword() const;

private slots:
    void updateMode();
    void validate();

private:
    QRadioButton* m_loginMode = nullptr;
    QRadioButton* m_registerMode = nullptr;
    QLineEdit* m_account = nullptr;
    QLineEdit* m_password = nullptr;
    QLineEdit* m_confirm = nullptr;
    QLineEdit* m_nick = nullptr;
    QCheckBox* m_remember = nullptr;
    QLineEdit* m_host = nullptr;
    QSpinBox*  m_port = nullptr;
    QPushButton* m_ok = nullptr;
    QLabel* m_nickLabel = nullptr;
    QLabel* m_confirmLabel = nullptr;
};
