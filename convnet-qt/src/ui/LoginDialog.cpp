#include "ui/LoginDialog.h"
#include "model/Identity.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("ConvnetGo IM — 登录 / 注册"));
    setModal(true);

    Identity& id = Identity::instance();

    // 模式选择
    m_loginMode = new QRadioButton(QStringLiteral("登录"), this);
    m_registerMode = new QRadioButton(QStringLiteral("注册"), this);
    m_loginMode->setChecked(true);
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(m_loginMode);
    modeRow->addWidget(m_registerMode);
    modeRow->addStretch(1);

    // 字段
    m_account = new QLineEdit(id.account, this);
    m_account->setPlaceholderText(QStringLiteral("登录账号（唯一）"));
    m_password = new QLineEdit(id.password, this);
    m_password->setEchoMode(QLineEdit::Password);
    m_confirm = new QLineEdit(this);
    m_confirm->setEchoMode(QLineEdit::Password);
    m_nick = new QLineEdit(id.nick, this);
    m_nick->setPlaceholderText(QStringLiteral("显示昵称，供他人搜索"));
    m_remember = new QCheckBox(QStringLiteral("记住密码"), this);
    m_remember->setChecked(id.rememberPassword);

    m_host = new QLineEdit(id.serverHost, this);
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(id.serverPort);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), m_account);
    form->addRow(QStringLiteral("密码"), m_password);
    m_confirmLabel = new QLabel(QStringLiteral("确认密码"), this);
    form->addRow(m_confirmLabel, m_confirm);
    m_nickLabel = new QLabel(QStringLiteral("昵称"), this);
    form->addRow(m_nickLabel, m_nick);
    form->addRow(QString(), m_remember);
    form->addRow(QStringLiteral("服务器地址"), m_host);
    form->addRow(QStringLiteral("端口"), m_port);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(modeRow);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(m_loginMode, &QRadioButton::toggled, this, &LoginDialog::updateMode);
    connect(m_account, &QLineEdit::textChanged, this, &LoginDialog::validate);
    connect(m_password, &QLineEdit::textChanged, this, &LoginDialog::validate);
    connect(m_confirm, &QLineEdit::textChanged, this, &LoginDialog::validate);
    connect(m_nick, &QLineEdit::textChanged, this, &LoginDialog::validate);

    updateMode();
}

void LoginDialog::updateMode()
{
    const bool reg = m_registerMode->isChecked();
    m_confirm->setVisible(reg);
    m_confirmLabel->setVisible(reg);
    m_nick->setVisible(reg);
    m_nickLabel->setVisible(reg);
    m_ok->setText(reg ? QStringLiteral("注册") : QStringLiteral("登录"));
    validate();
}

void LoginDialog::validate()
{
    const bool reg = m_registerMode->isChecked();
    bool ok = !m_account->text().trimmed().isEmpty() && !m_password->text().isEmpty();
    if (reg) {
        ok = ok && !m_nick->text().trimmed().isEmpty()
                && m_password->text() == m_confirm->text();
    }
    if (m_ok)
        m_ok->setEnabled(ok);
}

bool LoginDialog::isRegister() const { return m_registerMode->isChecked(); }
QString LoginDialog::account() const { return m_account->text().trimmed(); }
QString LoginDialog::password() const { return m_password->text(); }
QString LoginDialog::nick() const { return m_nick->text().trimmed(); }
QString LoginDialog::host() const { return m_host->text().trimmed(); }
quint16 LoginDialog::port() const { return static_cast<quint16>(m_port->value()); }
bool LoginDialog::rememberPassword() const { return m_remember->isChecked(); }
