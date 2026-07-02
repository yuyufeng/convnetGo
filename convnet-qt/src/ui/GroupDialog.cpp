#include "ui/GroupDialog.h"
#include "model/AppModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
#include <QVariant>

GroupDialog::GroupDialog(AppModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle(QStringLiteral("群组管理"));
    resize(380, 460);

    // 创建群
    auto* createBox = new QGroupBox(QStringLiteral("创建群（每人最多 5 个）"), this);
    m_createName = new QLineEdit(createBox);
    m_createName->setPlaceholderText(QStringLiteral("群名称"));
    m_createPass = new QLineEdit(createBox);
    m_createPass->setPlaceholderText(QStringLiteral("入群密码（可选：设了则凭密码免审批加入，留空则需群主审批）"));
    auto* createBtn = new QPushButton(QStringLiteral("创建"), createBox);
    auto* createRow = new QHBoxLayout;
    createRow->addWidget(m_createName, 1);
    createRow->addWidget(createBtn);
    auto* createLayout = new QVBoxLayout(createBox);
    createLayout->addLayout(createRow);
    createLayout->addWidget(m_createPass);

    // 搜索并加入
    auto* joinBox = new QGroupBox(QStringLiteral("搜索并加入（每群最多 10 人）"), this);
    m_search = new QLineEdit(joinBox);
    m_search->setPlaceholderText(QStringLiteral("按群名或群ID搜索"));
    auto* searchBtn = new QPushButton(QStringLiteral("搜索"), joinBox);
    m_results = new QListWidget(joinBox);
    auto* joinBtn = new QPushButton(QStringLiteral("申请加入"), joinBox);

    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(m_search);
    searchRow->addWidget(searchBtn);
    auto* joinLayout = new QVBoxLayout(joinBox);
    joinLayout->addLayout(searchRow);
    joinLayout->addWidget(m_results, 1);
    joinLayout->addWidget(joinBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(createBox);
    layout->addWidget(joinBox, 1);

    connect(createBtn, &QPushButton::clicked, this, &GroupDialog::doCreate);
    connect(searchBtn, &QPushButton::clicked, this, &GroupDialog::doSearch);
    connect(m_search, &QLineEdit::returnPressed, this, &GroupDialog::doSearch);
    connect(joinBtn, &QPushButton::clicked, this, &GroupDialog::doJoin);
    connect(m_results, &QListWidget::itemDoubleClicked, this, &GroupDialog::doJoin);
    connect(m_model, &AppModel::searchGroupsResult, this, &GroupDialog::onResults);
}

void GroupDialog::doCreate()
{
    const QString name = m_createName->text().trimmed();
    if (name.isEmpty())
        return;
    m_model->createGroup(name, m_createPass->text());
    m_createName->clear();
    m_createPass->clear();
    QMessageBox::information(this, QStringLiteral("已提交"),
                            QStringLiteral("创建请求已发送（若超过 5 个会被服务器拒绝）。"));
}

void GroupDialog::doSearch()
{
    const QString kw = m_search->text().trimmed();
    if (kw.isEmpty())
        return;
    m_results->clear();
    m_model->searchGroups(kw);
}

void GroupDialog::onResults(const QVector<GroupInfo>& results)
{
    m_searchResults = results;
    m_results->clear();
    for (const GroupInfo& g : results) {
        const QString lock = g.hasPassword ? QStringLiteral("[密] ") : QString();
        auto* item = new QListWidgetItem(
            lock + QStringLiteral("%1  (ID:%2, %3人)").arg(g.name).arg(g.groupId).arg(g.memberCount),
            m_results);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(g.groupId));
    }
    if (results.isEmpty())
        m_results->addItem(QStringLiteral("未找到匹配群组"));
}

void GroupDialog::doJoin()
{
    auto* item = m_results->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个群"));
        return;
    }
    const quint64 gid = item->data(Qt::UserRole).toULongLong();

    // 该群是否需要入群密码
    bool hasPassword = false;
    for (const GroupInfo& g : m_searchResults)
        if (g.groupId == gid) {
            hasPassword = g.hasPassword;
            break;
        }

    if (hasPassword) {
        bool ok = false;
        const QString pw = QInputDialog::getText(this, QStringLiteral("输入入群密码"),
                                                 QStringLiteral("该群需要密码，凭密码可直接加入："),
                                                 QLineEdit::Password, QString(), &ok);
        if (!ok)
            return; // 取消
        m_model->joinGroup(gid, pw);
        QMessageBox::information(this, QStringLiteral("已提交"),
                                QStringLiteral("已凭密码请求加入（密码正确则立即入群）。"));
    } else {
        m_model->joinGroup(gid, QString());
        QMessageBox::information(this, QStringLiteral("已申请"),
                                QStringLiteral("入群申请已发送，等待群主批准。"));
    }
}
