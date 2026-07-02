#include "ui/ChatWindow.h"
#include "ui/UserInfoDialog.h"
#include "model/AppModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTextCursor>
#include <QPushButton>
#include <QToolButton>
#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QWidgetAction>
#include <QGridLayout>
#include <QFont>
#include <QCursor>
#include <QShortcut>
#include <QKeySequence>
#include <QTimer>
#include <QDateTime>
#include <QColor>
#include <QVariant>
#include <QEvent>
#include <QKeyEvent>

ChatWindow::ChatWindow(AppModel* model, const QString& scope, quint64 targetId,
                       const QString& title, QWidget* parent)
    : QWidget(parent), m_model(model), m_scope(scope), m_targetId(targetId)
{
    setWindowTitle(title);
    resize(480, 520);
    setAttribute(Qt::WA_DeleteOnClose);

    m_history = new QTextBrowser(this);
    m_history->setOpenExternalLinks(true);

    m_input = new QTextEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入消息，回车发送，Shift+Enter 换行"));
    m_input->setMaximumHeight(120);
    m_input->installEventFilter(this); // 拦截回车用于发送

    auto* toolRow = new QHBoxLayout;
    buildEmojiButton(toolRow);
    toolRow->addStretch(1);
    auto* sendBtn = new QPushButton(QStringLiteral("发送"), this);
    toolRow->addWidget(sendBtn);

    // 聊天区（历史 + 工具栏 + 输入）
    auto* chatArea = new QWidget(this);
    auto* v = new QVBoxLayout(chatArea);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(m_history, 1);
    v->addLayout(toolRow);
    v->addWidget(m_input);

    auto* root = new QHBoxLayout(this);
    if (m_scope == QLatin1String("g")) {
        // 群聊：右侧成员列表
        auto* split = new QSplitter(Qt::Horizontal, this);
        split->addWidget(chatArea);
        m_memberList = new QListWidget(split);
        m_memberList->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_memberList, &QListWidget::customContextMenuRequested,
                this, &ChatWindow::onMemberContextMenu);
        connect(m_memberList, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem*) { onMemberContextMenu(QPoint()); });
        split->addWidget(m_memberList);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 1);
        root->addWidget(split);
        resize(700, 520);

        // 合并刷新：在线状态抖动/群变更时，60ms 内多次触发只重建一次成员列表，
        // 避免频繁 clear()+重建 QListWidget 造成卡顿。
        m_memberTimer = new QTimer(this);
        m_memberTimer->setSingleShot(true);
        m_memberTimer->setInterval(60);
        connect(m_memberTimer, &QTimer::timeout, this, &ChatWindow::refreshMembers);
        auto scheduleMembers = [this]() {
            if (m_memberTimer && !m_memberTimer->isActive())
                m_memberTimer->start();
        };
        connect(m_model, &AppModel::groupsChanged, this, scheduleMembers);
        connect(m_model, &AppModel::presenceChanged, this,
                [scheduleMembers](quint64, bool) { scheduleMembers(); });
        refreshMembers();
    } else {
        root->addWidget(chatArea);
    }

    connect(sendBtn, &QPushButton::clicked, this, &ChatWindow::onSend);
    connect(m_model, &AppModel::chatMessageReceived, this, &ChatWindow::onChatMessage);

    auto* sendShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(sendShortcut, &QShortcut::activated, this, &ChatWindow::onSend);

    // 载入历史
    for (const ChatMessage& m : m_model->history(m_scope, m_targetId))
        appendMessage(m);
}

void ChatWindow::refreshMembers()
{
    if (!m_memberList)
        return;
    m_memberList->clear();
    GroupInfo g;
    if (!m_model->groupById(m_targetId, g))
        return;

    auto* header = new QListWidgetItem(QStringLiteral("成员 (%1)").arg(g.members.size()), m_memberList);
    header->setFlags(Qt::NoItemFlags); // 标题，不可选、无右键数据

    for (const FriendInfo& mem : g.members) {
        const QString dot = mem.online ? QStringLiteral("●") : QStringLiteral("○");
        const QString name = mem.nick.isEmpty() ? QStringLiteral("用户%1").arg(mem.userId) : mem.nick;
        const QString self = (mem.userId == m_model->myUserId()) ? QStringLiteral("（我）") : QString();
        auto* item = new QListWidgetItem(QStringLiteral("%1 %2%3").arg(dot, name, self), m_memberList);
        item->setForeground(mem.online ? QColor("#1a7f37") : QColor("#8c959f"));
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(mem.userId));
    }
}

void ChatWindow::onMemberContextMenu(const QPoint& pos)
{
    if (!m_memberList)
        return;
    QListWidgetItem* item = pos.isNull() ? m_memberList->currentItem() : m_memberList->itemAt(pos);
    if (!item || !item->data(Qt::UserRole).isValid())
        return;
    const quint64 uid = item->data(Qt::UserRole).toULongLong();

    GroupInfo g;
    if (!m_model->groupById(m_targetId, g))
        return;
    FriendInfo found;
    bool ok = false;
    for (const FriendInfo& mem : g.members) {
        if (mem.userId == uid) {
            found = mem;
            ok = true;
            break;
        }
    }
    if (!ok)
        return;

    quint64 sent = 0, recv = 0;
    m_model->peerTraffic(found.publicId, sent, recv);

    if (pos.isNull()) { // 双击：直接看资料
        showUserInfoDialog(this, found, sent, recv);
        return;
    }
    QMenu menu(this);
    QAction* info = menu.addAction(QStringLiteral("查看资料 / 虚拟IP"));
    if (menu.exec(m_memberList->mapToGlobal(pos)) == info)
        showUserInfoDialog(this, found, sent, recv);
}

void ChatWindow::buildEmojiButton(QHBoxLayout* row)
{
    static const QStringList kEmojis = {
        "😀", "😄", "😁", "😆", "😅", "😂", "🙂", "😉", "😊", "😍",
        "😘", "😜", "🤔", "😎", "😢", "😭", "😡", "👍", "👎", "👏",
        "🙏", "💪", "🎉", "❤️", "💔", "🔥", "⭐", "✅", "❌", "🚀"
    };

    // emoji 字体：Windows 走 Segoe UI Emoji（彩色）；Linux 需装 fonts-noto-color-emoji。
    // 显式设字号（14pt）以覆盖全局 QSS 的 13px，让表情更清晰。
    QFont emojiFont;
    emojiFont.setFamilies({QStringLiteral("Segoe UI Emoji"),
                           QStringLiteral("Noto Color Emoji"),
                           QStringLiteral("Segoe UI Symbol"),
                           QStringLiteral("Apple Color Emoji")});
    emojiFont.setPointSize(14);

    auto* btn = new QToolButton(this);
    btn->setText(QStringLiteral("😀"));
    btn->setFont(emojiFont);
    btn->setToolTip(QStringLiteral("表情"));
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;}")); // 隐藏下拉箭头

    auto* menu = new QMenu(btn); // btn 持有 menu

    // 10 列表情网格：QWidgetAction 承载一个 QGridLayout 容器。
    auto* panel = new QWidget; // 无父：所有权交给 QWidgetAction 接管，避免二次释放
    panel->setStyleSheet(QStringLiteral("background:#23282e;")); // 与菜单底色一致
    auto* g = new QGridLayout(panel);
    g->setSpacing(2);
    g->setContentsMargins(6, 6, 6, 6);

    constexpr int kCols = 10;
    for (int i = 0; i < kEmojis.size(); ++i) {
        const QString e = kEmojis.at(i);
        auto* cell = new QToolButton(panel); // 父=panel，随容器一起被 QWidgetAction 接管
        cell->setText(e);
        cell->setFont(emojiFont);
        cell->setAutoRaise(true);                          // 平面，hover 才高亮（配合 Theme.h）
        cell->setFixedSize(34, 34);                        // 方形格，避免彩色字形被裁切
        cell->setCursor(Qt::PointingHandCursor);
        cell->setStyleSheet(QStringLiteral("padding:0;")); // 抵消全局 QToolButton 内边距
        connect(cell, &QToolButton::clicked, this, [this, e, menu]() {
            m_input->insertPlainText(e);
            menu->hide(); // QWidgetAction 子控件不参与菜单的自动关闭，需手动关
            m_input->setFocus();
        });
        g->addWidget(cell, i / kCols, i % kCols); // 行=i/10，列=i%10 → 每行 10 个
    }

    auto* wa = new QWidgetAction(menu); // menu 持有 wa
    wa->setDefaultWidget(panel);        // panel 所有权转移给 wa
    menu->addAction(wa);

    btn->setMenu(menu);
    row->addWidget(btn);
}

bool ChatWindow::belongsHere(const ChatMessage& m) const
{
    return m.scope == m_scope && m.convId == m_targetId;
}

void ChatWindow::onChatMessage(const ChatMessage& m)
{
    if (belongsHere(m))
        appendMessage(m);
}

void ChatWindow::appendMessage(const ChatMessage& m)
{
    const QString who = m.outgoing ? QStringLiteral("我") : m.fromNick;
    const QString time = QDateTime::fromSecsSinceEpoch(m.ts).toString("HH:mm:ss");
    const QString color = m.outgoing ? "#3fb950" : "#58a6ff"; // 深色主题下更亮的名称色

    QTextCursor cur(m_history->document());
    cur.movePosition(QTextCursor::End);
    cur.insertHtml(QStringLiteral("<div style='margin-top:8px'><b style='color:%1'>%2</b> "
                                  "<span style='color:#888;font-size:small'>%3</span></div>")
                       .arg(color, who.toHtmlEscaped(), time));
    // 富文本正文（发送方 QTextEdit 的 HTML；insertHtml 会提取 body 内容）
    cur.insertHtml(m.richText);
    cur.insertBlock();
    m_history->moveCursor(QTextCursor::End);
    m_history->ensureCursorVisible();
}

void ChatWindow::onSend()
{
    if (m_input->toPlainText().trimmed().isEmpty())
        return;
    const QString html = m_input->toHtml();
    m_model->sendChat(m_scope, m_targetId, html);
    m_input->clear();
    m_input->setFocus();
}

void ChatWindow::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::ActivationChange && isActiveWindow())
        emit activated(m_scope, m_targetId); // 窗口获得焦点 -> 该会话标记已读
}

bool ChatWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_input && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !ke->modifiers().testFlag(Qt::ShiftModifier)) {
            onSend();     // 回车发送
            return true;  // 吞掉事件，不插入换行
        }
        // Shift+Enter 走默认（换行）
    }
    return QWidget::eventFilter(obj, e);
}
