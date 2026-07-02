#include "ui/MainWindow.h"
#include "ui/LoginDialog.h"
#include "ui/ChatWindow.h"
#include "ui/AddFriendDialog.h"
#include "ui/GroupDialog.h"
#include "ui/UserInfoDialog.h"
#include "ui/FirewallDialog.h"
#include "model/AppModel.h"
#include "model/Identity.h"

#include <QTreeWidget>
#include <QHeaderView>
#include <QStringList>
#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSize>
#include <QSet>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QVariant>
#include <QColor>
#include <QApplication>
#include <QMenu>
#include <QBrush>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPolygonF>
#include <QPointF>
#include <QFont>
#include <QStandardPaths>
#include <QDir>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QEvent>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
// 树节点类型（存于 Qt::UserRole+1）：好友 / 群 / 群成员
constexpr int kRoleKind = Qt::UserRole + 1;
// 缓存头像“信号签名”（存于 Qt::UserRole+2）：外观未变则跳过重绘（省 QPainter 开销）
constexpr int kRoleIconSig = Qt::UserRole + 2;
enum ItemKind { KindFriend = 0, KindGroup = 1, KindMember = 2 };

// 信号格数（供头像绘制与缓存签名共用，保持一致）：
// -1=离线，-2=在线但 RTT 未知，0..4=格数（每 50ms 少一格，<50ms=4 格）。
int signalBars(bool online, int rttMs)
{
    if (!online)
        return -1;
    if (rttMs < 0)
        return -2;
    int b = 4 - rttMs / 50;
    if (b > 4)
        b = 4;
    if (b < 0)
        b = 0;
    return b;
}

// 左侧信号格 + 右侧圆形头像。信号格数按延时递减：每 50ms 少一格（<50ms=4格）。
QIcon makeAvatar(const QString& name, bool online, int rttMs)
{
    QPixmap pm(46, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    // 信号格
    int bars;
    QColor barColor;
    const int sb = signalBars(online, rttMs);
    if (sb == -1) {
        bars = 0;
        barColor = QColor("#4a525b");
    } else if (sb == -2) {
        bars = 0;
        barColor = QColor("#6b7178"); // 在线但延时未知/测量中
    } else {
        bars = sb;
        barColor = bars >= 3 ? QColor("#3fb950") : (bars >= 1 ? QColor("#d29922") : QColor("#f85149"));
    }
    static const int bx[4] = {1, 5, 9, 13};
    static const int bh[4] = {6, 10, 14, 18};
    for (int i = 0; i < 4; ++i) {
        const QRect r(bx[i], 24 - bh[i], 3, bh[i]);
        p.setBrush(i < bars ? barColor : QColor("#2b3138"));
        p.drawRoundedRect(r, 1, 1);
    }

    // 头像圆（右侧，x 偏移 18）
    static const QColor palette[] = {
        QColor("#e57373"), QColor("#f06292"), QColor("#ba68c8"), QColor("#7986cb"),
        QColor("#4fc3f7"), QColor("#4db6ac"), QColor("#81c784"), QColor("#ffb74d"),
        QColor("#a1887f"), QColor("#90a4ae")};
    const int n = sizeof(palette) / sizeof(palette[0]);
    p.setBrush(online ? palette[qHash(name) % n] : QColor("#c2c8cf"));
    p.drawEllipse(19, 1, 26, 26);

    const QString ch = name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(14);
    p.setFont(f);
    p.drawText(QRect(18, 0, 28, 28), Qt::AlignCenter, ch);
    p.end();
    return QIcon(pm);
}

// 群组图标：圆角方形（区别于圆形人头像），首字符居中。
QIcon makeGroupIcon(const QString& name)
{
    QPixmap pm(46, 28); // 与好友图标同宽，保证文字对齐
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#607d8b"));
    p.drawRoundedRect(19, 1, 26, 26, 6, 6);
    const QString ch = name.isEmpty() ? QStringLiteral("群") : name.left(1).toUpper();
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(13);
    p.setFont(f);
    p.drawText(QRect(18, 0, 28, 28), Qt::AlignCenter, ch);
    p.end();
    return QIcon(pm);
}

// 生成醒目的展开/折叠三角箭头，写到应用数据目录并返回文件路径（供 QSS branch image 用）。
// 一旦给 QTreeView::branch 设了样式，Qt 默认的箭头会消失，必须自己提供图片；这里画一个
// 加大的青色实心三角，明显易见。open=true 画朝下（已展开），否则朝右（已折叠）。
QString branchArrowFile(bool open)
{
    const int s = 16;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#29b6f6")); // 主题青色强调，深底上醒目
    QPolygonF tri;
    if (open) // 朝下三角（已展开）
        tri << QPointF(3.5, 5.5) << QPointF(12.5, 5.5) << QPointF(8.0, 12.0);
    else      // 朝右三角（已折叠）
        tri << QPointF(5.5, 3.5) << QPointF(12.0, 8.0) << QPointF(5.5, 12.5);
    p.drawPolygon(tri);
    p.end();

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    QDir().mkpath(dir);
    const QString path = dir + (open ? QStringLiteral("/branch_open.png")
                                     : QStringLiteral("/branch_closed.png"));
    pm.save(path, "PNG");
    return path;
}

// 顶部“我”的青色电源图标（Radmin 风格）。alert=true 时右上角加红点（托盘新消息闪烁用）。
QPixmap makePowerIcon(bool alert = false)
{
    QPixmap pm(44, 44);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#29b6f6"));
    p.drawEllipse(2, 2, 40, 40);
    QPen wp(Qt::white);
    wp.setWidth(3);
    wp.setCapStyle(Qt::RoundCap);
    p.setPen(wp);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(13, 13, 18, 18), (90 + 35) * 16, (360 - 70) * 16); // 顶部留缺口的圆环
    p.drawLine(22, 11, 22, 21);                                          // 顶部竖线
    if (alert) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#f85149"));
        p.drawEllipse(28, 2, 14, 14); // 红点角标
    }
    p.end();
    return pm;
}

#ifdef _WIN32
// 前台是否为全屏程序（游戏）——用于自动进入免打扰
bool foregroundIsFullscreen()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return false;
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow())
        return false;
    RECT r;
    if (!GetWindowRect(hwnd, &r))
        return false;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(mon, &mi))
        return false;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top
        && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}
#endif
} // namespace

MainWindow::MainWindow(AppModel* model, QWidget* parent)
    : QMainWindow(parent), m_model(model)
{
    setWindowTitle(QStringLiteral("ConvnetGo IM"));
    resize(360, 640);

    // 中央容器：顶部“我”的卡片 + 单一树形视图（取代 TAB 页）
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    auto* cv = new QVBoxLayout(central);
    cv->setContentsMargins(8, 8, 8, 8);
    cv->setSpacing(8);

    // “我”的卡片：头像 + 昵称 + 虚拟IP + 在线徽标
    auto* card = new QFrame(central);
    card->setObjectName(QStringLiteral("selfCard"));
    auto* ch = new QHBoxLayout(card);
    ch->setContentsMargins(12, 10, 12, 10);
    ch->setSpacing(12);
    auto* selfIcon = new QLabel(card);
    selfIcon->setPixmap(makePowerIcon());
    ch->addWidget(selfIcon, 0, Qt::AlignVCenter);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    m_selfNick = new QLabel(QStringLiteral("未登录"), card);
    m_selfNick->setObjectName(QStringLiteral("selfNick"));
    m_selfIp = new QLabel(QString(), card);
    m_selfIp->setObjectName(QStringLiteral("selfIp"));
    m_selfBadge = new QLabel(QStringLiteral("离线"), card);
    m_selfBadge->setFixedHeight(20);
    col->addWidget(m_selfNick);
    col->addWidget(m_selfIp);
    auto* badgeRow = new QHBoxLayout;
    badgeRow->addWidget(m_selfBadge);
    badgeRow->addStretch(1);
    col->addLayout(badgeRow);
    ch->addLayout(col, 1);
    cv->addWidget(card);

    // 树：两列（名称 / 虚拟IP），顶层「好友」「群组」可折叠
    m_tree = new QTreeWidget(central);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->setIndentation(20); // 略增缩进，给加大的展开/折叠箭头留位置
    m_tree->setIconSize(QSize(46, 28));
    m_tree->setExpandsOnDoubleClick(false); // 双击用于开聊天，展开走箭头
    // 醒目的展开/折叠箭头：主题化 QTreeView::branch 后默认箭头会消失，这里显式提供
    // 自绘的青色三角图片，明显可见。
    {
        const QString closedImg = branchArrowFile(false);
        const QString openImg = branchArrowFile(true);
        m_tree->setStyleSheet(QStringLiteral(
            "QTreeView::branch{background:#1b1f24;}"
            "QTreeView::branch:has-children:!has-siblings:closed,"
            "QTreeView::branch:closed:has-children:has-siblings{"
            "border-image:none;image:url(\"%1\");}"
            "QTreeView::branch:open:has-children:!has-siblings,"
            "QTreeView::branch:open:has-children:has-siblings{"
            "border-image:none;image:url(\"%2\");}")
                                  .arg(closedImg, openImg));
    }
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_friendRoot = new QTreeWidgetItem(m_tree, QStringList(QStringLiteral("好友")));
    m_groupRoot = new QTreeWidgetItem(m_tree, QStringList(QStringLiteral("群组")));
    for (QTreeWidgetItem* root : {m_friendRoot, m_groupRoot}) {
        QFont bf = root->font(0);
        bf.setBold(true);
        root->setFont(0, bf);
        root->setForeground(0, QColor("#a9b0b8"));
        root->setExpanded(true);
    }
    cv->addWidget(m_tree, 1);
    setCentralWidget(central);

    // 工具栏
    auto* tb = addToolBar(QStringLiteral("操作"));
    tb->setMovable(false);
    QAction* actAddFriend = tb->addAction(QStringLiteral("添加好友"));
    QAction* actGroup = tb->addAction(QStringLiteral("群组管理"));
    QAction* actRefresh = tb->addAction(QStringLiteral("刷新"));
    tb->addSeparator();
    QAction* actSummary = tb->addAction(QStringLiteral("连接概要"));
    QAction* actNetStatus = tb->addAction(QStringLiteral("网卡状态"));
    QAction* actFirewall = tb->addAction(QStringLiteral("防火墙"));
    m_dndAction = tb->addAction(QStringLiteral("免打扰"));
    m_dndAction->setCheckable(true);
    QAction* actRemoveFriend = tb->addAction(QStringLiteral("删除好友"));
    tb->addSeparator();
    QAction* actLogout = tb->addAction(QStringLiteral("退出登录"));

    // 状态栏
    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);
    updateStatus();

    // 连接信号
    connect(actAddFriend, &QAction::triggered, this, &MainWindow::showAddFriend);
    connect(actGroup, &QAction::triggered, this, &MainWindow::showGroupDialog);
    connect(actRefresh, &QAction::triggered, this, [this]() {
        m_model->refreshFriends();
        m_model->refreshGroups();
    });
    connect(actSummary, &QAction::triggered, this, &MainWindow::showConnectionSummary);
    connect(actNetStatus, &QAction::triggered, this, &MainWindow::showNetworkStatus);
    connect(actFirewall, &QAction::triggered, this, &MainWindow::showFirewall);
    connect(m_dndAction, &QAction::toggled, this, &MainWindow::toggleDnd);
    connect(actLogout, &QAction::triggered, this, &MainWindow::doLogout);
    connect(actRemoveFriend, &QAction::triggered, this, &MainWindow::removeSelectedFriend);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onItemDoubleClicked);

    // 右键菜单：好友查看资料 / 群进入或退出
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::onTreeContextMenu);

    // 未读消息闪烁定时器（有未读时才运行）
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(600);
    connect(m_blinkTimer, &QTimer::timeout, this, &MainWindow::onBlinkTick);

    // 信号格刷新定时器（按最新 RTT 重绘好友图标）
    m_signalTimer = new QTimer(this);
    m_signalTimer->setInterval(2000);
    connect(m_signalTimer, &QTimer::timeout, this, &MainWindow::updateSignalIcons);
    m_signalTimer->start();

    // 合并重建定时器：好友/群/在线状态的多次变化在 60ms 内合并成一次全树重建，
    // 避免 presence 抖动导致 GUI 线程被连续全量重建占满而卡顿。
    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(60);
    connect(m_rebuildTimer, &QTimer::timeout, this, &MainWindow::rerenderLists);

    connect(m_model, &AppModel::loggedIn, this, &MainWindow::onLoggedIn);
    connect(m_model, &AppModel::loginFailed, this, &MainWindow::onLoginFailed);
    connect(m_model, &AppModel::connectionStateChanged, this, &MainWindow::onConnectionStateChanged);
    connect(m_model, &AppModel::serverError, this, &MainWindow::onServerError);
    connect(m_model, &AppModel::reconnecting, this, [this](int s) {
        statusBar()->showMessage(QStringLiteral("与服务器断开，%1 秒后自动重连…").arg(s), 5000);
    });
    connect(m_model, &AppModel::friendsChanged, this, &MainWindow::scheduleRebuild);
    connect(m_model, &AppModel::groupsChanged, this, &MainWindow::scheduleRebuild);
    connect(m_model, &AppModel::presenceChanged, this,
            [this](quint64, bool) { scheduleRebuild(); }); // 在线状态变化 -> 合并重建
    connect(m_model, &AppModel::friendRequestReceived, this, &MainWindow::onFriendRequest);
    connect(m_model, &AppModel::groupJoinRequestReceived, this, &MainWindow::onGroupJoinRequest);
    connect(m_model, &AppModel::opResult, this, &MainWindow::onOpResult);
    connect(m_model, &AppModel::chatMessageReceived, this, &MainWindow::onChatMessage);
    connect(m_model, &AppModel::networkStatusChanged, this, &MainWindow::onNetworkStatusChanged);

    setupTray();

    // 启动后弹出登录
    QTimer::singleShot(0, this, &MainWindow::doLoginFlow);
}

void MainWindow::doLoginFlow()
{
    if (m_inLogin)
        return; // 避免重复弹出登录框
    m_inLogin = true;
    LoginDialog dlg(this);
    const bool accepted = (dlg.exec() == QDialog::Accepted);
    m_inLogin = false;
    if (!accepted)
        return;

    Identity& id = Identity::instance();
    id.account = dlg.account();
    id.serverHost = dlg.host();
    id.serverPort = dlg.port();
    id.rememberPassword = dlg.rememberPassword();
    id.password = dlg.rememberPassword() ? dlg.password() : QString();
    if (dlg.isRegister())
        id.nick = dlg.nick();
    id.save();

    if (dlg.isRegister())
        m_model->registerAccount(dlg.host(), dlg.port(), dlg.account(), dlg.password(), dlg.nick());
    else
        m_model->login(dlg.host(), dlg.port(), dlg.account(), dlg.password());
}

void MainWindow::onLoggedIn(const QString& publicId)
{
    m_loggedIn = true;
    updateStatus();
    statusBar()->showMessage(QStringLiteral("已登录：%1").arg(publicId), 5000);
}

void MainWindow::onLoginFailed(const QString& reason)
{
    QMessageBox::warning(this, QStringLiteral("登录失败"), reason);
    // 重新弹出登录框供重试（延后到当前信号处理结束后）
    QTimer::singleShot(0, this, &MainWindow::doLoginFlow);
}

void MainWindow::onConnectionStateChanged(bool connected)
{
    m_connected = connected;
    if (!connected)
        m_loggedIn = false;
    updateStatus();
    if (!connected)
        statusBar()->showMessage(QStringLiteral("与服务器断开连接"), 5000);
}

void MainWindow::onServerError(const QString& message)
{
    // 未登录时的网络错误 = 连接服务器失败：弹窗提示并重新登录
    if (!m_loggedIn && !m_inLogin) {
        QMessageBox::warning(this, QStringLiteral("连接失败"), message);
        QTimer::singleShot(0, this, &MainWindow::doLoginFlow);
        return;
    }
    statusBar()->showMessage(message, 6000);
}

void MainWindow::onFriendsChanged()
{
    // 清空「好友」分组下的子项
    for (int i = m_friendRoot->childCount() - 1; i >= 0; --i)
        delete m_friendRoot->takeChild(i);

    // friends() 已按“在线优先、其次 userID”排序，故在线的自然靠前
    const auto friends = m_model->friends();
    m_friendRoot->setText(0, QStringLiteral("好友 (%1)").arg(friends.size()));
    for (const FriendInfo& f : friends) {
        const QString nick = f.nick.isEmpty() ? QStringLiteral("(无昵称)") : f.nick;
        const QString key = QStringLiteral("u:%1").arg(f.userId);
        auto* item = new QTreeWidgetItem(m_friendRoot);
        item->setText(0, unreadBadge(key) + nick);
        item->setIcon(0, makeAvatar(nick, f.online, m_model->peerRttMs(f.publicId))); // 信号格+头像
        item->setForeground(0, f.online ? QColor("#e6e9ee") : QColor("#8b929b"));
        item->setText(1, f.cvnIP);                     // 右列显示虚拟IP（Radmin 风格）
        item->setForeground(1, QColor("#8b929b"));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(f.userId));
        item->setData(0, kRoleKind, KindFriend);
    }
    applyUnreadBackgrounds();
}

void MainWindow::onGroupsChanged()
{
    // 记住哪些群当前是展开的，重建后恢复（presence 变化会频繁重建）
    QSet<quint64> expanded;
    for (int i = 0; i < m_groupRoot->childCount(); ++i) {
        QTreeWidgetItem* gn = m_groupRoot->child(i);
        if (gn->isExpanded())
            expanded.insert(gn->data(0, Qt::UserRole).toULongLong());
    }
    for (int i = m_groupRoot->childCount() - 1; i >= 0; --i)
        delete m_groupRoot->takeChild(i);

    const auto groups = m_model->groups();
    m_groupRoot->setText(0, QStringLiteral("群组 (%1)").arg(groups.size()));
    for (const GroupInfo& g : groups) {
        const QString key = QStringLiteral("g:%1").arg(g.groupId);
        const quint64 me = m_model->myUserId();
        QString roleTag;
        if (g.ownerId == me)
            roleTag = QStringLiteral("  [群主]");
        else if (g.admins.contains(me))
            roleTag = QStringLiteral("  [管理]");
        auto* item = new QTreeWidgetItem(m_groupRoot);
        item->setText(0, unreadBadge(key) + g.name + roleTag);
        item->setIcon(0, makeGroupIcon(g.name));
        item->setForeground(0, QColor("#e6e9ee"));
        item->setText(1, QStringLiteral("%1人").arg(g.memberCount));
        item->setForeground(1, QColor("#8b929b"));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(g.groupId));
        item->setData(0, kRoleKind, KindGroup);

        // 展开后显示成员：信号/头像 + 昵称 + 虚拟IP + 在线状态
        for (const FriendInfo& mem : g.members) {
            const QString mnick = mem.nick.isEmpty() ? QStringLiteral("(无昵称)") : mem.nick;
            const QString self = (mem.userId == m_model->myUserId()) ? QStringLiteral("（我）") : QString();
            auto* mi = new QTreeWidgetItem(item);
            mi->setText(0, mnick + self);
            mi->setIcon(0, makeAvatar(mnick, mem.online, m_model->peerRttMs(mem.publicId)));
            mi->setForeground(0, mem.online ? QColor("#e6e9ee") : QColor("#8b929b"));
            mi->setText(1, mem.cvnIP);
            mi->setForeground(1, QColor("#8b929b"));
            mi->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            mi->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(mem.userId));
            mi->setData(0, kRoleKind, KindMember);
        }
        if (expanded.contains(g.groupId))
            item->setExpanded(true); // 恢复展开状态
    }
    applyUnreadBackgrounds();
}

void MainWindow::onFriendRequest(quint64 fromUserId, const QString& nick, const QString& greeting)
{
    const QString text = QStringLiteral("用户 %1 (ID:%2) 请求加为好友。\n验证消息：%3\n是否接受？")
                             .arg(nick).arg(fromUserId).arg(greeting.isEmpty() ? QStringLiteral("(无)") : greeting);
    const auto btn = QMessageBox::question(this, QStringLiteral("好友申请"), text,
                                           QMessageBox::Yes | QMessageBox::No);
    m_model->acceptFriend(fromUserId, btn == QMessageBox::Yes);
}

void MainWindow::onGroupJoinRequest(quint64 groupId, quint64 applicantId, const QString& nick)
{
    GroupInfo g;
    const QString gname = m_model->groupById(groupId, g) ? g.name : QString::number(groupId);
    const QString text = QStringLiteral("用户 %1 (ID:%2) 申请加入群「%3」。\n是否批准？")
                             .arg(nick).arg(applicantId).arg(gname);
    const auto btn = QMessageBox::question(this, QStringLiteral("入群申请"), text,
                                           QMessageBox::Yes | QMessageBox::No);
    m_model->approveJoin(groupId, applicantId, btn == QMessageBox::Yes);
}

void MainWindow::onOpResult(bool ok, const QString& message)
{
    statusBar()->showMessage((ok ? QStringLiteral("✓ ") : QStringLiteral("✗ ")) + message, 4000);
    if (!ok && !message.isEmpty())
        QMessageBox::information(this, QStringLiteral("提示"), message);
}

void MainWindow::onChatMessage(const ChatMessage& m)
{
    if (m.outgoing)
        return;
    const QString key = m.scope + ":" + QString::number(m.convId);

    // 若该会话窗口已打开且正是当前活动窗口，视为已读，不提示
    ChatWindow* w = m_chats.value(key, nullptr);
    if (w && w->isActiveWindow())
        return;

    markUnread(key);
    const QString from = m.scope == "g" ? QStringLiteral("群消息") : m.fromNick;
    statusBar()->showMessage(QStringLiteral("新消息来自 %1").arg(from), 4000);

    // 该会话窗口已打开且可见（未最小化）：用户能直接看到消息，不再弹托盘气泡（pop）
    const bool chatVisible = w && w->isVisible() && !w->isMinimized();

    // 任务栏/标题闪烁（Windows 上闪烁任务栏项，直到窗口被激活）
    QApplication::alert(w ? static_cast<QWidget*>(w) : static_cast<QWidget*>(this), 0);

    // 提示音 + 托盘气泡（免打扰时仅闪烁，不响、不弹气泡；会话窗口已可见时不弹气泡）
    if (!dndActive()) {
        QApplication::beep();
        if (m_tray && m_tray->isVisible() && !chatVisible)
            m_tray->showMessage(QStringLiteral("新消息"), QStringLiteral("来自 %1").arg(from),
                                QSystemTrayIcon::Information, 3000);
    }
    // 托盘图标闪烁由 onBlinkTick 处理（有未读且窗口非活动时）
}

ChatWindow* MainWindow::chatFor(const QString& scope, quint64 targetId, const QString& title)
{
    const QString key = scope + ":" + QString::number(targetId);
    if (auto it = m_chats.constFind(key); it != m_chats.constEnd()) {
        it.value()->raise();
        it.value()->activateWindow();
        return it.value();
    }
    auto* win = new ChatWindow(m_model, scope, targetId, title);
    m_chats.insert(key, win);
    connect(win, &QObject::destroyed, this, [this, key]() { m_chats.remove(key); });
    connect(win, &ChatWindow::activated, this, &MainWindow::onChatActivated);
    win->show();
    return win;
}

void MainWindow::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item)
        return;
    if (item == m_friendRoot || item == m_groupRoot) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    const quint64 id = item->data(0, Qt::UserRole).toULongLong();
    switch (item->data(0, kRoleKind).toInt()) {
    case KindFriend:
        openFriendChat(id);
        break;
    case KindGroup:
        openGroupChat(id);
        break;
    case KindMember:
        openFriendChat(id); // 与该成员私聊（同网络可发，服务器转发）
        break;
    }
}

quint64 MainWindow::pickGroupMember(const GroupInfo& g, const QString& title)
{
    QStringList names;
    QVector<quint64> ids;
    const quint64 me = m_model->myUserId();
    for (const FriendInfo& m : g.members) {
        if (m.userId == me)
            continue; // 排除自己
        const QString nick = m.nick.isEmpty() ? QStringLiteral("用户%1").arg(m.userId) : m.nick;
        names << QStringLiteral("%1 (ID:%2)").arg(nick).arg(m.userId);
        ids << m.userId;
    }
    if (names.isEmpty()) {
        QMessageBox::information(this, title, QStringLiteral("没有可选成员。"));
        return 0;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, title, QStringLiteral("选择成员："),
                                                 names, 0, false, &ok);
    if (!ok)
        return 0;
    const int idx = names.indexOf(chosen);
    return (idx >= 0 && idx < ids.size()) ? ids[idx] : 0;
}

void MainWindow::openFriendChat(quint64 uid)
{
    if (uid == 0)
        return;
    clearUnread(QStringLiteral("u:%1").arg(uid));
    chatFor(QStringLiteral("u"), uid, QStringLiteral("与 %1 聊天").arg(m_model->displayName(uid)));
}

void MainWindow::openGroupChat(quint64 gid)
{
    if (gid == 0)
        return;
    clearUnread(QStringLiteral("g:%1").arg(gid));
    GroupInfo g;
    const QString name = m_model->groupById(gid, g) ? g.name : QString::number(gid);
    chatFor(QStringLiteral("g"), gid, QStringLiteral("群「%1」").arg(name));
}

void MainWindow::showAddFriend()
{
    AddFriendDialog dlg(m_model, this);
    dlg.exec();
}

void MainWindow::showGroupDialog()
{
    GroupDialog dlg(m_model, this);
    dlg.exec();
}

void MainWindow::removeSelectedFriend()
{
    auto* item = m_tree->currentItem();
    if (!item || item->parent() != m_friendRoot) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先在「好友」下选择一位好友"));
        return;
    }
    const quint64 uid = item->data(0, Qt::UserRole).toULongLong();
    if (QMessageBox::question(this, QStringLiteral("删除好友"),
                              QStringLiteral("确定删除好友 %1？").arg(m_model->displayName(uid)))
        == QMessageBox::Yes) {
        m_model->removeFriend(uid);
    }
}

void MainWindow::updateStatus()
{
    // 顶部“我”的卡片
    m_selfNick->setText(Identity::instance().nick.isEmpty() ? QStringLiteral("未登录")
                                                            : Identity::instance().nick);
    const QString vip = m_model->myCvnIP();
    m_selfIp->setText(vip.isEmpty() ? QStringLiteral("虚拟IP：未分配") : vip);
    m_selfBadge->setText(m_loggedIn ? QStringLiteral(" 在线 ") : QStringLiteral(" 离线 "));
    m_selfBadge->setStyleSheet(m_loggedIn
        ? QStringLiteral("background:#2fd07a;color:#08160d;border-radius:9px;padding:1px 8px;font-size:12px;")
        : QStringLiteral("background:#5a626b;color:#c8ccd2;border-radius:9px;padding:1px 8px;font-size:12px;"));

    // 状态栏：物理网卡状态 + PublicID
    const QString nic = m_model->tapUp()
        ? QStringLiteral("网卡:%1(%2)").arg(m_model->tapIfName(), vip)
        : QStringLiteral("网卡:未启用");
    m_statusLabel->setText(QStringLiteral("  %1 | %2").arg(nic, m_model->myPublicId()));
}

void MainWindow::onNetworkStatusChanged()
{
    updateStatus();
}

void MainWindow::showFirewall()
{
    FirewallDialog dlg(m_model->firewall(), this);
    dlg.exec();
}

// ---- 托盘 / 免打扰 ----

void MainWindow::setupTray()
{
    m_trayNormal = QIcon(makePowerIcon(false));
    m_trayAlert = QIcon(makePowerIcon(true));
    setWindowIcon(m_trayNormal);

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return; // 无系统托盘：不启用（窗口照常用，只是不缩托盘）

    m_tray = new QSystemTrayIcon(m_trayNormal, this);
    m_tray->setToolTip(QStringLiteral("ConvnetGo IM"));

    auto* menu = new QMenu(this);
    QAction* actShow = menu->addAction(QStringLiteral("显示主窗口"));
    menu->addAction(m_dndAction); // 与工具栏共用同一勾选项，状态同步
    menu->addSeparator();
    QAction* actQuit = menu->addAction(QStringLiteral("退出"));
    m_tray->setContextMenu(menu);
    m_tray->show();

    connect(actShow, &QAction::triggered, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
    connect(actQuit, &QAction::triggered, this, [this]() {
        m_reallyQuit = true;
        qApp->quit();
    });
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);

    // 全屏检测 -> 自动免打扰
    m_fullscreenTimer = new QTimer(this);
    m_fullscreenTimer->setInterval(3000);
    connect(m_fullscreenTimer, &QTimer::timeout, this, &MainWindow::onFullscreenCheck);
    m_fullscreenTimer->start();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::toggleDnd(bool on)
{
    m_manualDnd = on;
    if (m_tray)
        m_tray->setToolTip(dndActive() ? QStringLiteral("ConvnetGo IM（免打扰）")
                                       : QStringLiteral("ConvnetGo IM"));
}

void MainWindow::onFullscreenCheck()
{
#ifdef _WIN32
    m_autoDnd = foregroundIsFullscreen();
#else
    m_autoDnd = false;
#endif
    if (m_tray)
        m_tray->setToolTip(dndActive() ? QStringLiteral("ConvnetGo IM（免打扰）")
                                       : QStringLiteral("ConvnetGo IM"));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_reallyQuit || !m_tray) {
        e->accept();
        return;
    }
    e->ignore();
    hide(); // 关闭=最小化到托盘
    static bool hinted = false;
    if (!hinted) {
        hinted = true;
        m_tray->showMessage(QStringLiteral("ConvnetGo IM"),
                            QStringLiteral("已最小化到托盘，仍在后台运行。右键托盘图标可退出。"),
                            QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange && (windowState() & Qt::WindowMinimized) && m_tray)
        QTimer::singleShot(0, this, [this]() { hide(); }); // 最小化 -> 隐藏到托盘
}

void MainWindow::doLogout()
{
    if (QMessageBox::question(this, QStringLiteral("退出登录"),
                              QStringLiteral("确定退出当前账号？"))
        != QMessageBox::Yes)
        return;

    // 关闭所有聊天窗口
    const auto chats = m_chats.values();
    for (ChatWindow* w : chats)
        if (w)
            w->close();
    m_chats.clear();

    m_model->logout();
    m_loggedIn = false;
    updateStatus();

    // 重新弹出登录框
    QTimer::singleShot(0, this, &MainWindow::doLoginFlow);
}

void MainWindow::showNetworkStatus()
{
    QString html;
    if (m_model->tapUp()) {
        html = QStringLiteral(
                   "<b>虚拟网卡：已启用</b><br><br>"
                   "接口名：%1<br>"
                   "本机虚拟IP：<b>%2</b><br>"
                   "子网：10.0.0.0/8（同一虚拟局域网）<br><br>"
                   "<span style='color:#888;font-size:small'>好友/组员在线且建立连接后，"
                   "可 ping 对方虚拟IP。逐对端的 P2P/中继 状态见「好友/成员资料」。</span>")
                   .arg(m_model->tapIfName().toHtmlEscaped(),
                        m_model->myCvnIP().toHtmlEscaped());
    } else {
        const QString reason = m_model->tapError().isEmpty()
            ? QStringLiteral("（尚未启动，或已断开连接）")
            : m_model->tapError().toHtmlEscaped();
        html = QStringLiteral(
                   "<b>虚拟网卡：未启用</b><br><br>"
                   "原因：%1<br><br>"
                   "<span style='color:#888;font-size:small'>Windows 需把 wintun.dll 放到程序目录"
                   "并以管理员身份运行；Linux 需 root/CAP_NET_ADMIN。<br>"
                   "未启用时 IM（好友/群/聊天）仍可正常使用，只是无法 ping 虚拟IP。</span>")
                   .arg(reason);
    }

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("网卡状态"));
    box.setTextFormat(Qt::RichText);
    box.setText(html);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    box.exec();
}

// ---- 新消息提示（未读徽标 + 闪烁）----

QString MainWindow::unreadBadge(const QString& convKey) const
{
    // 不用 emoji（部分字体无字形会显示成方框），用纯文本计数
    const int n = m_unread.value(convKey, 0);
    return n > 0 ? QStringLiteral("(%1) ").arg(n) : QString();
}

void MainWindow::markUnread(const QString& convKey)
{
    m_unread[convKey] = m_unread.value(convKey, 0) + 1;
    scheduleRebuild();
    if (!m_blinkTimer->isActive())
        m_blinkTimer->start();
}

void MainWindow::clearUnread(const QString& convKey)
{
    if (m_unread.remove(convKey) > 0) {
        scheduleRebuild();
        if (m_unread.isEmpty()) {
            m_blinkTimer->stop();
            m_blinkOn = false;
            applyUnreadBackgrounds();
            if (m_tray)
                m_tray->setIcon(m_trayNormal); // 停止托盘闪烁
        }
    }
}

void MainWindow::rerenderLists()
{
    onFriendsChanged();
    onGroupsChanged();
}

void MainWindow::scheduleRebuild()
{
    // 合并：60ms 窗口内的多次变化只触发一次全树重建
    if (m_rebuildTimer && !m_rebuildTimer->isActive())
        m_rebuildTimer->start();
}

void MainWindow::onBlinkTick()
{
    m_blinkOn = !m_blinkOn;
    applyUnreadBackgrounds();
    // 托盘图标闪烁：有未读且主窗未激活时
    if (m_tray) {
        const bool flash = !m_unread.isEmpty() && !isActiveWindow();
        m_tray->setIcon(flash && m_blinkOn ? m_trayAlert : m_trayNormal);
    }
}

void MainWindow::updateSignalIcons()
{
    if (!m_friendRoot || !m_groupRoot)
        return;
    // 好友
    for (int i = 0; i < m_friendRoot->childCount(); ++i) {
        QTreeWidgetItem* item = m_friendRoot->child(i);
        FriendInfo f;
        if (!m_model->friendById(item->data(0, Qt::UserRole).toULongLong(), f))
            continue;
        const QString nick = f.nick.isEmpty() ? QStringLiteral("(无昵称)") : f.nick;
        const int rtt = m_model->peerRttMs(f.publicId);
        const int sig = signalBars(f.online, rtt);
        const QVariant prev = item->data(0, kRoleIconSig);
        if (prev.isValid() && prev.toInt() == sig)
            continue; // 外观未变，跳过重绘
        item->setIcon(0, makeAvatar(nick, f.online, rtt));
        item->setData(0, kRoleIconSig, sig);
    }
    // 群成员（展开时可见）
    for (int gi = 0; gi < m_groupRoot->childCount(); ++gi) {
        QTreeWidgetItem* gnode = m_groupRoot->child(gi);
        if (!gnode->isExpanded())
            continue; // 未展开的群跳过，省开销
        GroupInfo g;
        if (!m_model->groupById(gnode->data(0, Qt::UserRole).toULongLong(), g))
            continue;
        for (int mi = 0; mi < gnode->childCount(); ++mi) {
            QTreeWidgetItem* mitem = gnode->child(mi);
            const quint64 uid = mitem->data(0, Qt::UserRole).toULongLong();
            for (const FriendInfo& mem : g.members) {
                if (mem.userId == uid) {
                    const QString nick = mem.nick.isEmpty() ? QStringLiteral("(无昵称)") : mem.nick;
                    const int rtt = m_model->peerRttMs(mem.publicId);
                    const int sig = signalBars(mem.online, rtt);
                    const QVariant prev = mitem->data(0, kRoleIconSig);
                    if (!(prev.isValid() && prev.toInt() == sig)) {
                        mitem->setIcon(0, makeAvatar(nick, mem.online, rtt));
                        mitem->setData(0, kRoleIconSig, sig);
                    }
                    break;
                }
            }
        }
    }
}

void MainWindow::applyUnreadBackgrounds()
{
    // 柔和的深色高亮（原来的亮黄在深色主题上过于突兀）
    const QBrush hi = m_blinkOn ? QBrush(QColor("#263947")) : QBrush();
    auto paint = [&](QTreeWidgetItem* root, const QString& prefix) {
        for (int i = 0; i < root->childCount(); ++i) {
            QTreeWidgetItem* item = root->child(i);
            const QString key = prefix + QString::number(item->data(0, Qt::UserRole).toULongLong());
            item->setBackground(0, m_unread.contains(key) ? hi : QBrush());
        }
    };
    paint(m_friendRoot, QStringLiteral("u:"));
    paint(m_groupRoot, QStringLiteral("g:"));
}

void MainWindow::onChatActivated(const QString& scope, quint64 targetId)
{
    clearUnread(scope + ":" + QString::number(targetId));
}

// ---- 右键菜单：好友查看资料/删除；群进入/退出 ----

void MainWindow::onTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item || item == m_friendRoot || item == m_groupRoot)
        return;
    const QPoint gp = m_tree->viewport()->mapToGlobal(pos);
    const quint64 id = item->data(0, Qt::UserRole).toULongLong();

    switch (item->data(0, kRoleKind).toInt()) {
    case KindFriend: {
        FriendInfo f;
        if (!m_model->friendById(id, f))
            return;
        QMenu menu(this);
        QAction* actInfo = menu.addAction(QStringLiteral("查看资料 / 虚拟IP"));
        QAction* actChat = menu.addAction(QStringLiteral("发送消息"));
        menu.addSeparator();
        QAction* actDel = menu.addAction(QStringLiteral("删除好友"));
        QAction* chosen = menu.exec(gp);
        if (chosen == actInfo)
            showUserInfo(f);
        else if (chosen == actChat)
            openFriendChat(id);
        else if (chosen == actDel && QMessageBox::question(this, QStringLiteral("删除好友"),
                     QStringLiteral("确定删除好友 %1？").arg(f.nick)) == QMessageBox::Yes)
            m_model->removeFriend(id);
        break;
    }
    case KindGroup: {
        GroupInfo g;
        if (!m_model->groupById(id, g))
            return;
        const bool owner = (g.ownerId == m_model->myUserId());

        QMenu menu(this);
        QAction* actOpen = menu.addAction(QStringLiteral("进入群聊"));
        menu.addSeparator();
        QAction* actTransfer = nullptr;
        QAction* actDisband = nullptr;
        if (owner) {
            actTransfer = menu.addAction(QStringLiteral("转移群主…"));
            actDisband = menu.addAction(QStringLiteral("解散群"));
        }
        QAction* actLeave = menu.addAction(QStringLiteral("退出群组"));
        QAction* chosen = menu.exec(gp);

        if (chosen == actOpen) {
            openGroupChat(id);
        } else if (chosen == actTransfer) {
            const quint64 succ = pickGroupMember(g, QStringLiteral("选择新群主"));
            if (succ)
                m_model->manageGroup(id, QStringLiteral("transfer"), succ);
        } else if (chosen == actDisband) {
            if (QMessageBox::question(this, QStringLiteral("解散群"),
                    QStringLiteral("确定解散群「%1」？该群将被删除。").arg(g.name)) == QMessageBox::Yes)
                m_model->manageGroup(id, QStringLiteral("disband"), 0);
        } else if (chosen == actLeave) {
            if (owner && g.members.size() > 1) {
                // 群主退出：必须指定继任者（转移并退出），或解散
                const quint64 succ = pickGroupMember(g, QStringLiteral("你是群主，退出前请指定继任群主"));
                if (succ)
                    m_model->manageGroup(id, QStringLiteral("handover"), succ);
            } else if (owner) {
                // 仅剩自己：退出即解散
                if (QMessageBox::question(this, QStringLiteral("退出群组"),
                        QStringLiteral("你是唯一成员，退出将解散群「%1」。确定？").arg(g.name)) == QMessageBox::Yes)
                    m_model->leaveGroup(id);
            } else {
                if (QMessageBox::question(this, QStringLiteral("退出群组"),
                        QStringLiteral("确定退出群「%1」？").arg(g.name)) == QMessageBox::Yes)
                    m_model->leaveGroup(id);
            }
        }
        break;
    }
    case KindMember: {
        if (!item->parent())
            return;
        const quint64 gid = item->parent()->data(0, Qt::UserRole).toULongLong();
        GroupInfo g;
        if (!m_model->groupById(gid, g))
            return;
        FriendInfo mf;
        bool found = false;
        for (const FriendInfo& m : g.members)
            if (m.userId == id) { mf = m; found = true; break; }
        if (!found)
            return;

        const quint64 me = m_model->myUserId();
        const bool owner = (g.ownerId == me);
        const bool admin = owner || g.admins.contains(me);
        const bool tgtOwner = (mf.userId == g.ownerId);
        const bool tgtAdmin = g.admins.contains(mf.userId);
        const bool canKick = (mf.userId != me) && (owner ? !tgtOwner : (admin && !tgtOwner && !tgtAdmin));

        QMenu menu(this);
        QAction* actInfo = menu.addAction(QStringLiteral("查看资料 / 虚拟IP"));
        QAction* actChat = menu.addAction(QStringLiteral("私聊"));
        QAction* actKick = nullptr;
        QAction* actAdmin = nullptr;
        QAction* actTransfer = nullptr;
        if (canKick || (owner && !tgtOwner))
            menu.addSeparator();
        if (canKick)
            actKick = menu.addAction(QStringLiteral("踢出群组"));
        if (owner && !tgtOwner) {
            actAdmin = menu.addAction(tgtAdmin ? QStringLiteral("取消管理员") : QStringLiteral("设为管理员"));
            actTransfer = menu.addAction(QStringLiteral("转移群主给TA"));
        }

        QAction* chosen = menu.exec(gp);
        if (chosen == actInfo) {
            quint64 sent = 0, recv = 0;
            m_model->peerTraffic(mf.publicId, sent, recv);
            showUserInfoDialog(this, mf, sent, recv);
        } else if (chosen == actChat) {
            openFriendChat(id);
        } else if (chosen == actKick) {
            if (QMessageBox::question(this, QStringLiteral("踢出群组"),
                    QStringLiteral("确定将 %1 踢出群「%2」？").arg(mf.nick, g.name)) == QMessageBox::Yes)
                m_model->manageGroup(gid, QStringLiteral("kick"), mf.userId);
        } else if (chosen == actAdmin) {
            m_model->manageGroup(gid, tgtAdmin ? QStringLiteral("revoke") : QStringLiteral("grant"), mf.userId);
        } else if (chosen == actTransfer) {
            if (QMessageBox::question(this, QStringLiteral("转移群主"),
                    QStringLiteral("确定把群主转移给 %1？转移后你将成为普通成员。").arg(mf.nick)) == QMessageBox::Yes)
                m_model->manageGroup(gid, QStringLiteral("transfer"), mf.userId);
        }
        break;
    }
    }
}

void MainWindow::showUserInfo(const FriendInfo& f)
{
    quint64 sent = 0, recv = 0;
    m_model->peerTraffic(f.publicId, sent, recv);
    showUserInfoDialog(this, f, sent, recv);
}

void MainWindow::showConnectionSummary()
{
    Identity& id = Identity::instance();
    const QString method = m_connected
        ? QStringLiteral("信令服务器转发（IM 消息经服务器存储转发）")
        : QStringLiteral("未连接");

    const QString html =
        QStringLiteral(
            "<b>连接概要</b><br><br>"
            "<b>我的身份</b><br>"
            "昵称：%1<br>账号：%2<br>用户ID：%3<br>"
            "虚拟IP：<b>%4</b><br>MAC：%5<br>"
            "PublicID：<span style='font-size:small'>%6</span><br><br>"
            "<b>服务器</b><br>"
            "地址：%7:%8<br>连接状态：%9<br><br>"
            "<b>IM 消息通道：%10</b><br><br>"
            "<span style='color:#888;font-size:small'>说明：聊天消息经信令服务器存储转发；"
            "网络层每个对端的 P2P直连/服务器中继 状态见「好友/成员资料」（右键查看）。"
            "虚拟网卡互联（ping 10.110.x）在 M3-2 加入。</span>")
            .arg(id.nick.toHtmlEscaped())
            .arg(id.account.toHtmlEscaped())
            .arg(m_model->myUserId())
            .arg(id.myCvnIP.isEmpty() ? QStringLiteral("(未分配)") : id.myCvnIP.toHtmlEscaped())
            .arg(id.mac.toHtmlEscaped())
            .arg(m_model->myPublicId().toHtmlEscaped())
            .arg(id.serverHost.toHtmlEscaped())
            .arg(id.serverPort)
            .arg(m_connected ? QStringLiteral("已连接") : QStringLiteral("未连接"))
            .arg(method);

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("连接概要"));
    box.setTextFormat(Qt::RichText);
    box.setText(html);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    box.exec();
}
