#include "ui/AddFriendDialog.h"
#include "model/AppModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QVariant>

AddFriendDialog::AddFriendDialog(AppModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle(QStringLiteral("添加好友"));
    resize(360, 420);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("按昵称或用户ID搜索"));
    auto* searchBtn = new QPushButton(QStringLiteral("搜索"), this);

    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(m_search);
    searchRow->addWidget(searchBtn);

    m_results = new QListWidget(this);
    m_greeting = new QLineEdit(this);
    m_greeting->setPlaceholderText(QStringLiteral("验证消息（可选）"));

    auto* requestBtn = new QPushButton(QStringLiteral("发送好友申请"), this);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(searchRow);
    layout->addWidget(new QLabel(QStringLiteral("搜索结果："), this));
    layout->addWidget(m_results, 1);
    layout->addWidget(m_greeting);
    layout->addWidget(requestBtn);

    connect(searchBtn, &QPushButton::clicked, this, &AddFriendDialog::doSearch);
    connect(m_search, &QLineEdit::returnPressed, this, &AddFriendDialog::doSearch);
    connect(requestBtn, &QPushButton::clicked, this, &AddFriendDialog::doRequest);
    connect(m_results, &QListWidget::itemDoubleClicked, this, &AddFriendDialog::doRequest);
    connect(m_model, &AppModel::searchFriendsResult, this, &AddFriendDialog::onResults);
}

void AddFriendDialog::doSearch()
{
    const QString kw = m_search->text().trimmed();
    if (kw.isEmpty())
        return;
    m_results->clear();
    m_model->searchFriends(kw);
}

void AddFriendDialog::onResults(const QVector<FriendInfo>& results)
{
    m_results->clear();
    for (const FriendInfo& f : results) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  (ID:%2)%3")
                .arg(f.nick.isEmpty() ? QStringLiteral("(无昵称)") : f.nick)
                .arg(f.userId)
                .arg(f.online ? QStringLiteral("  ● 在线") : QString()),
            m_results);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(f.userId));
    }
    if (results.isEmpty())
        m_results->addItem(QStringLiteral("未找到匹配用户"));
}

void AddFriendDialog::doRequest()
{
    auto* item = m_results->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个用户"));
        return;
    }
    const quint64 uid = item->data(Qt::UserRole).toULongLong();
    m_model->requestFriend(uid, m_greeting->text().trimmed());
    QMessageBox::information(this, QStringLiteral("已发送"),
                            QStringLiteral("好友申请已发送，等待对方确认。"));
    accept();
}
