#include "model/AppModel.h"

#include "core/Protocol.h"
#include "core/SignalingClient.h"
#include "p2p/P2PManager.h"
#include "p2p/RelayTransport.h"
#include "net/Router.h"
#include "net/TapManager.h"
#include "net/Firewall.h"
#include "model/Identity.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>

#include <algorithm>

using namespace proto;

// ---- 解析辅助 ----

static quint64 toU64(const QJsonValue& v)
{
    if (v.isDouble())
        return static_cast<quint64>(v.toDouble());
    if (v.isString())
        return v.toString().toULongLong();
    return 0;
}

static quint64 uidFromPid(const QString& pid)
{
    const int idx = pid.lastIndexOf(':');
    return idx < 0 ? 0 : pid.mid(idx + 1).toULongLong();
}

static FriendInfo parseRoster(const QJsonObject& o)
{
    FriendInfo f;
    f.userId = toU64(o.value("UserID"));
    f.publicId = o.value("PublicID").toString();
    f.nick = o.value("Nick").toString();
    f.cvnIP = o.value("CvnIP").toString();
    f.mac = o.value("Mac").toString();
    f.online = o.value("Online").toBool();
    f.nicMode = o.value("NicMode").toString();
    return f;
}

// ---- 构造与连接 ----

AppModel::AppModel(SignalingClient* sig, QObject* parent)
    : QObject(parent), m_sig(sig)
{
    m_p2p = new P2PManager(sig, this);
    m_relay = new RelayTransport(sig, this);
    m_fw = new Firewall();                        // 纯类，AppModel 析构时释放
    m_fw->load();
    m_router = new Router(m_p2p, m_relay);       // 纯类，AppModel 析构时释放
    m_router->setFirewall(m_fw);
    m_tap = new TapManager(this);

    // 数据面接线：P2P 入站 -> Router 写网卡；网卡入站写回统一走 Router
    m_p2p->setInboundSink([this](const QString& peer, const QByteArray& p) { m_router->deliverInbound(peer, p); });
    m_router->setWriteToTap([this](const QByteArray& p) { m_tap->write(p); });
    connect(m_tap, &TapManager::error, this, [this](const QString& e) {
        m_tapUp = false;
        m_tapError = e;
        emit networkStatusChanged();
        emit serverError(e);
    });

    connect(m_p2p, &P2PManager::peerMethodChanged, this, &AppModel::onPeerMethodChanged);

    connect(m_sig, &SignalingClient::frameReceived, this, &AppModel::onFrame);
    connect(m_sig, &SignalingClient::connected, this, &AppModel::onSocketConnected);
    connect(m_sig, &SignalingClient::disconnected, this, &AppModel::onSocketDisconnected);
    connect(m_sig, &SignalingClient::socketError, this, &AppModel::onSocketError);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &AppModel::tryReconnect);
}

AppModel::~AppModel()
{
    // 关闭顺序：停读线程 -> 同步销毁 PeerLink（切断入站回调）-> 释放 Router
    if (m_tap)
        m_tap->stop();
    delete m_p2p; // 显式销毁子对象，令 PeerLink 析构同步执行，停止 libdatachannel 回调
    m_p2p = nullptr;
    delete m_router; // 非 QObject，手动释放
    m_router = nullptr;
    delete m_fw;
    m_fw = nullptr;
}

void AppModel::startNetwork()
{
    if (m_netStarted)
        return;
    const QString ip = Identity::instance().myCvnIP;
    if (ip.isEmpty())
        return;
    const NicMode mode =
        (Identity::instance().nicMode == QLatin1String("tap")) ? NicMode::Tap : NicMode::Tun;
    m_router->setLayer2(mode == NicMode::Tap);
    QString err;
    if (!m_tap->start(ip, 8, mode, [this](const QByteArray& p) { m_router->routeOutbound(p); }, err)) {
        m_tapUp = false;
        m_tapIfName.clear();
        m_tapError = err;
        emit networkStatusChanged();
        emit serverError(QStringLiteral("虚拟网卡未启用：%1（IM 仍可用）").arg(err));
        return;
    }
    m_netStarted = true;
    m_tapUp = true;
    m_tapIfName = m_tap->ifName();
    m_tapError.clear();
    emit networkStatusChanged();
    rebuildRoutes();
}

QString AppModel::nicMode() const { return Identity::instance().nicMode; }

void AppModel::setNicMode(const QString& mode)
{
    const QString m = (mode == QLatin1String("tap")) ? QStringLiteral("tap") : QStringLiteral("tun");
    if (Identity::instance().nicMode == m)
        return;
    Identity::instance().nicMode = m;
    Identity::instance().save();
    if (isConnected())
        m_sig->send(NIC_MODE_REPORT, {m}); // 上报新模式给服务器 -> 转发给对端做一致性提示
    // 重启网卡使新模式生效（P2P 链路保留）
    if (m_netStarted) {
        m_tap->stop();
        m_netStarted = false;
        m_tapUp = false;
        m_tapIfName.clear();
        emit networkStatusChanged();
        startNetwork();
    }
}

void AppModel::rebuildRoutes()
{
    if (!m_router)
        return;
    m_router->clearRoutes();
    // 两种模式都登记对端集合（L2 广播 fanout 用）；L3 额外建 cvnIP->对端 路由。排除自己。
    for (const FriendInfo& f : m_friends) {
        if (f.publicId.isEmpty() || f.publicId == m_publicId)
            continue;
        m_router->addPeer(f.publicId);
        if (!f.cvnIP.isEmpty())
            m_router->setRoute(Router::parseIpv4(f.cvnIP), f.publicId);
    }
    for (const GroupInfo& g : m_groups)
        for (const FriendInfo& mem : g.members) {
            if (mem.publicId.isEmpty() || mem.publicId == m_publicId)
                continue;
            m_router->addPeer(mem.publicId);
            if (!mem.cvnIP.isEmpty())
                m_router->setRoute(Router::parseIpv4(mem.cvnIP), mem.publicId);
        }
}

void AppModel::connectOnlinePeers()
{
    if (!m_p2p)
        return;
    for (const FriendInfo& f : m_friends)
        if (f.online && !f.publicId.isEmpty())
            m_p2p->ensureLink(f.publicId);
    for (const GroupInfo& g : m_groups)
        for (const FriendInfo& mem : g.members)
            if (mem.online && !mem.publicId.isEmpty())
                m_p2p->ensureLink(mem.publicId);
}

void AppModel::onPeerMethodChanged(const QString& peerPublicId)
{
    const quint64 uid = uidFromPid(peerPublicId);
    const QString method = m_p2p->methodText(peerPublicId);
    bool changed = false;
    if (m_friends.contains(uid)) {
        m_friends[uid].connMethod = method;
        changed = true;
    }
    for (auto& g : m_groups)
        for (auto& mem : g.members)
            if (mem.userId == uid) {
                mem.connMethod = method;
                changed = true;
            }
    if (changed) {
        emit friendsChanged();
        emit groupsChanged();
    }
}

QString AppModel::keyUser(quint64 uid) { return QStringLiteral("u:") + QString::number(uid); }
QString AppModel::keyGroup(quint64 gid) { return QStringLiteral("g:") + QString::number(gid); }

void AppModel::login(const QString& host, quint16 port,
                     const QString& account, const QString& password)
{
    m_authHost = host;
    m_authPort = port;
    m_pending = PendingAuth::Login;
    m_pendingAccount = account;
    m_pendingPassword = password;
    m_pendingNick.clear();
    m_reconnectDelayMs = 2000;
    m_sig->connectToServer(host, port);
}

void AppModel::registerAccount(const QString& host, quint16 port,
                               const QString& account, const QString& password, const QString& nick)
{
    m_authHost = host;
    m_authPort = port;
    m_pending = PendingAuth::Register;
    m_pendingAccount = account;
    m_pendingPassword = password;
    m_pendingNick = nick;
    m_reconnectDelayMs = 2000;
    m_sig->connectToServer(host, port);
}

void AppModel::logout()
{
    m_wantConnected = false;       // 主动退出：不再自动重连
    m_reconnectTimer->stop();
    m_sig->disconnectFromServer(); // 触发 onSocketDisconnected（停网卡/拆 P2P）
    m_friends.clear();
    m_groups.clear();
    m_history.clear();
    m_publicId.clear();
    m_userId = 0;
    m_pending = PendingAuth::None;
    emit friendsChanged();
    emit groupsChanged();
    emit networkStatusChanged();
}

QString AppModel::myCvnIP() const { return Identity::instance().myCvnIP; }

void AppModel::peerTraffic(const QString& publicId, quint64& sent, quint64& recv) const
{
    sent = 0;
    recv = 0;
    if (m_router)
        m_router->traffic(publicId, sent, recv);
}

int AppModel::peerRttMs(const QString& publicId) const
{
    return m_p2p ? m_p2p->rttMs(publicId) : -1;
}

bool AppModel::isConnected() const { return m_sig->isConnected(); }

void AppModel::onSocketConnected()
{
    m_reconnectTimer->stop();
    sendAuth();
    emit connectionStateChanged(true);
}

void AppModel::onSocketDisconnected()
{
    // 顺序要紧：先停 TUN 读线程（不再有出站 sendFrame），再拆 P2P 链路，避免悬垂
    if (m_tap)
        m_tap->stop();
    if (m_p2p)
        m_p2p->reset();
    if (m_router)
        m_router->clearRoutes();
    m_netStarted = false;
    m_tapUp = false;
    m_tapIfName.clear();
    emit networkStatusChanged();
    emit connectionStateChanged(false);
    if (m_wantConnected)
        scheduleReconnect(); // 已登录会话意外断开 -> 自动重连
}

void AppModel::onSocketError(const QString& message)
{
    if (m_wantConnected)
        scheduleReconnect();      // 会话期内的错误：静默重连，不弹框
    else
        emit serverError(message); // 首次连接失败：交给 UI 提示并重新登录
}

void AppModel::scheduleReconnect()
{
    if (!m_wantConnected || m_reconnectTimer->isActive())
        return;
    emit reconnecting(m_reconnectDelayMs / 1000);
    m_reconnectTimer->start(m_reconnectDelayMs);
}

void AppModel::tryReconnect()
{
    if (!m_wantConnected)
        return;
    m_pending = PendingAuth::Login;               // 重连后用账号密码重新登录
    m_sig->connectToServer(m_authHost, m_authPort);
    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, 30000); // 指数退避，上限 30s
}

void AppModel::sendAuth()
{
    const QString mac = Identity::instance().mac;
    if (m_pending == PendingAuth::Register)
        m_sig->send(ACCOUNT_REGISTER, {m_pendingAccount, m_pendingPassword, m_pendingNick, mac});
    else if (m_pending == PendingAuth::Login)
        m_sig->send(ACCOUNT_LOGIN, {m_pendingAccount, m_pendingPassword, mac});
}

// ---- 好友动作 ----

void AppModel::searchFriends(const QString& keyword) { m_sig->send(FRIEND_SEARCH, {keyword}); }
void AppModel::requestFriend(quint64 userId, const QString& greeting)
{
    m_sig->send(FRIEND_REQUEST, {static_cast<double>(userId), greeting});
}
void AppModel::acceptFriend(quint64 fromUserId, bool accept)
{
    m_sig->send(FRIEND_ACCEPT, {static_cast<double>(fromUserId), accept});
}
void AppModel::removeFriend(quint64 userId) { m_sig->send(FRIEND_REMOVE, {static_cast<double>(userId)}); }
void AppModel::refreshFriends() { m_sig->send(FRIEND_LIST, {}); }

// ---- 群组动作 ----

void AppModel::createGroup(const QString& name, const QString& password) { m_sig->send(GROUP_CREATE, {name, password}); }
void AppModel::searchGroups(const QString& keyword) { m_sig->send(GROUP_SEARCH, {keyword}); }
void AppModel::joinGroup(quint64 groupId, const QString& password)
{
    m_sig->send(GROUP_JOIN_REQUEST, {static_cast<double>(groupId), password});
}
void AppModel::approveJoin(quint64 groupId, quint64 applicantId, bool approve)
{
    m_sig->send(GROUP_JOIN_APPROVE,
                {static_cast<double>(groupId), static_cast<double>(applicantId), approve});
}
void AppModel::leaveGroup(quint64 groupId) { m_sig->send(GROUP_LEAVE, {static_cast<double>(groupId)}); }
void AppModel::manageGroup(quint64 groupId, const QString& action, quint64 target)
{
    m_sig->send(GROUP_MANAGE, {static_cast<double>(groupId), action, static_cast<double>(target)});
}
void AppModel::refreshGroups() { m_sig->send(GROUP_LIST, {}); }

// ---- 聊天 ----

void AppModel::sendChat(const QString& scope, quint64 targetId, const QString& richText)
{
    const QString msgId = m_publicId + "-" + QString::number(++m_msgSeq);
    ChatMessage m;
    m.scope = scope;
    m.fromUserId = m_userId;
    m.fromNick = myNick();
    m.groupId = (scope == "g") ? targetId : 0;
    m.convId = targetId;
    m.msgId = msgId;
    m.contentType = "rich";
    m.richText = richText;
    m.ts = QDateTime::currentSecsSinceEpoch();
    m.outgoing = true;

    const QString key = (scope == "g") ? keyGroup(targetId) : keyUser(targetId);
    m_history[key].append(m);
    emit chatMessageReceived(m);

    m_sig->send(CHAT_SEND,
                {scope, static_cast<double>(targetId), msgId, QStringLiteral("rich"), richText});
}

// ---- 状态访问 ----

QVector<FriendInfo> AppModel::friends() const
{
    QVector<FriendInfo> v;
    for (const auto& f : m_friends)
        v.append(f);
    std::sort(v.begin(), v.end(), [](const FriendInfo& a, const FriendInfo& b) {
        if (a.online != b.online)
            return a.online > b.online; // 在线优先
        return a.userId < b.userId;
    });
    return v;
}

QVector<GroupInfo> AppModel::groups() const
{
    QVector<GroupInfo> v;
    for (const auto& g : m_groups)
        v.append(g);
    std::sort(v.begin(), v.end(), [](const GroupInfo& a, const GroupInfo& b) {
        return a.groupId < b.groupId;
    });
    return v;
}

bool AppModel::friendById(quint64 userId, FriendInfo& out) const
{
    auto it = m_friends.constFind(userId);
    if (it == m_friends.constEnd())
        return false;
    out = it.value();
    return true;
}

bool AppModel::groupById(quint64 groupId, GroupInfo& out) const
{
    auto it = m_groups.constFind(groupId);
    if (it == m_groups.constEnd())
        return false;
    out = it.value();
    return true;
}

QVector<ChatMessage> AppModel::history(const QString& scope, quint64 targetId) const
{
    return m_history.value((scope == "g") ? keyGroup(targetId) : keyUser(targetId));
}

QString AppModel::myNick() const { return Identity::instance().nick; }

QString AppModel::displayName(quint64 userId) const
{
    auto it = m_friends.constFind(userId);
    if (it != m_friends.constEnd() && !it.value().nick.isEmpty())
        return it.value().nick;
    return QStringLiteral("用户%1").arg(userId);
}

// ---- 主分发 ----

void AppModel::onFrame(int cmd, const QJsonArray& msg)
{
    switch (cmd) {
    case WS_REGISTE_RESP: {
        m_publicId = msg.at(0).toString();
        Identity& id = Identity::instance();
        id.publicId = m_publicId;
        if (!m_pendingAccount.isEmpty())
            id.account = m_pendingAccount;
        // 采用服务器回传的权威昵称（换设备/重连后可取回）
        const QString serverNick = msg.at(1).toString();
        if (!serverNick.isEmpty())
            id.nick = serverNick;
        id.myCvnIP = msg.at(2).toString(); // 本机虚拟IP
        id.save();
        m_userId = id.userId();
        m_pending = PendingAuth::None;
        m_wantConnected = true;      // 登录成功 -> 掉线后自动重连
        m_reconnectDelayMs = 2000;   // 重置退避
        if (m_p2p)
            m_p2p->setSelfPublicId(m_publicId);
        startNetwork(); // 起虚拟网卡（需 root；失败仅提示，IM 不受影响）
        m_sig->send(NIC_MODE_REPORT, {Identity::instance().nicMode}); // 上报本机网卡模式给服务器
        emit loggedIn(m_publicId);
        refreshFriends();
        refreshGroups();
        break;
    }

    case C_CONNTOWS_PEERCALL_RESP:
        m_p2p->handlePeerCall(msg);
        break;
    case C_CONNTOWS_PEERCALL_RESP_NOTONLINE:
        m_p2p->handlePeerOffline(msg);
        break;
    case RELAY_DATA_FWD: {
        // 中继入站：base64 -> 原始 IP 包 -> 写网卡（附来源 publicId 以计流量）
        const QString fromPid = msg.at(0).toString();
        const QByteArray pkt = QByteArray::fromBase64(msg.at(1).toString().toLatin1());
        if (!pkt.isEmpty())
            m_router->deliverInbound(fromPid, pkt);
        break;
    }
    case WS_REGISTE_FAIL:
        m_pending = PendingAuth::None;
        m_wantConnected = false;       // 认证失败：不自动重连（避免错误凭据死循环）
        m_reconnectTimer->stop();
        emit loginFailed(msg.at(0).toString());
        m_sig->disconnectFromServer(); // 断开以便用户重试（失败不会自动关闭连接）
        break;

    case FRIEND_SEARCH_RESP: {
        QVector<FriendInfo> res;
        for (const auto& v : msg.at(0).toArray())
            res.append(parseRoster(v.toObject()));
        emit searchFriendsResult(res);
        break;
    }
    case FRIEND_REQUEST_PUSH:
        emit friendRequestReceived(toU64(msg.at(0)), msg.at(2).toString(), msg.at(3).toString());
        break;
    case FRIEND_ACCEPT_PUSH: {
        const bool accepted = msg.at(1).toBool();
        if (accepted) {
            FriendInfo f = parseRoster(msg.at(0).toObject());
            m_friends[f.userId] = f;
            emit friendsChanged();
            if (f.online && !f.publicId.isEmpty())
                m_p2p->ensureLink(f.publicId);
            emit opResult(true, QStringLiteral("已添加好友 %1").arg(f.nick));
        } else {
            emit opResult(false, QStringLiteral("对方拒绝了好友申请"));
        }
        break;
    }
    case FRIEND_LIST_RESP: {
        m_friends.clear();
        for (const auto& v : msg.at(0).toArray()) {
            FriendInfo f = parseRoster(v.toObject());
            m_friends[f.userId] = f;
        }
        emit friendsChanged();
        rebuildRoutes();
        connectOnlinePeers();
        break;
    }
    case FRIEND_REMOVE_PUSH:
        m_friends.remove(toU64(msg.at(0)));
        emit friendsChanged();
        break;

    case GROUP_SEARCH_RESP: {
        QVector<GroupInfo> res;
        for (const auto& v : msg.at(0).toArray()) {
            const QJsonObject o = v.toObject();
            GroupInfo g;
            g.groupId = toU64(o.value("GroupID"));
            g.name = o.value("Name").toString();
            g.ownerId = toU64(o.value("OwnerID"));
            g.memberCount = o.value("MemberCount").toInt();
            g.hasPassword = o.value("HasPassword").toBool();
            res.append(g);
        }
        emit searchGroupsResult(res);
        break;
    }
    case GROUP_JOIN_PUSH:
        emit groupJoinRequestReceived(toU64(msg.at(0)), toU64(msg.at(1)), msg.at(2).toString());
        break;
    case GROUP_JOIN_RESULT_PUSH: {
        const bool approved = msg.at(2).toBool();
        if (approved) {
            emit opResult(true, QStringLiteral("已加入群 %1").arg(msg.at(1).toString()));
            refreshGroups();
        } else {
            emit opResult(false, QStringLiteral("入群申请被拒绝"));
        }
        break;
    }
    case GROUP_LIST_RESP: {
        m_groups.clear();
        for (const auto& v : msg.at(0).toArray()) {
            const QJsonObject o = v.toObject();
            GroupInfo g;
            g.groupId = toU64(o.value("GroupID"));
            g.name = o.value("Name").toString();
            g.ownerId = toU64(o.value("OwnerID"));
            for (const auto& av : o.value("Admins").toArray())
                g.admins.append(toU64(av));
            for (const auto& mv : o.value("Members").toArray())
                g.members.append(parseRoster(mv.toObject()));
            g.memberCount = g.members.size();
            m_groups[g.groupId] = g;
        }
        emit groupsChanged();
        rebuildRoutes();
        connectOnlinePeers();
        break;
    }
    case GROUP_MEMBER_CHANGE_PUSH:
        // 成员变化（含解散）——直接向服务器重新同步，规避各种边界。
        refreshGroups();
        break;
    case GROUP_OP_RESP: {
        const bool ok = msg.at(0).toBool();
        const QString message = msg.at(1).toString();
        const QJsonValue payload = msg.at(2);
        if (ok && payload.isObject() && payload.toObject().contains("GroupID")) {
            const QJsonObject o = payload.toObject();
            GroupInfo g;
            g.groupId = toU64(o.value("GroupID"));
            g.name = o.value("Name").toString();
            g.ownerId = toU64(o.value("OwnerID"));
            for (const auto& av : o.value("Admins").toArray())
                g.admins.append(toU64(av));
            for (const auto& mv : o.value("Members").toArray())
                g.members.append(parseRoster(mv.toObject()));
            g.memberCount = g.members.size();
            m_groups[g.groupId] = g;
            emit groupsChanged();
        }
        emit opResult(ok, message);
        break;
    }

    case PRESENCE_NOTIFY: {
        const quint64 uid = toU64(msg.at(0));
        const bool online = msg.at(1).toBool();
        const QString mode = msg.size() > 2 ? msg.at(2).toString() : QString(); // 对端网卡模式（可能缺省）
        if (m_friends.contains(uid)) {
            m_friends[uid].online = online;
            if (!mode.isEmpty())
                m_friends[uid].nicMode = mode;
        }
        for (auto& g : m_groups)
            for (auto& mem : g.members)
                if (mem.userId == uid) {
                    mem.online = online;
                    if (!mode.isEmpty())
                        mem.nicMode = mode;
                }
        // 只发 presenceChanged：UI 侧据此做“合并重建”。此前每次在线状态变化都额外
        // emit friendsChanged()+groupsChanged() 各触发一次全量树重建（共两次），大量
        // presence 事件会持续占满 GUI 线程，导致关窗口等一切操作卡顿。
        emit presenceChanged(uid, online);
        if (online)
            connectOnlinePeers(); // 对端上线，尝试建立 P2P
        break;
    }

    case CHAT_DELIVER: {
        ChatMessage m;
        m.scope = msg.at(0).toString();
        m.fromUserId = toU64(msg.at(1));
        m.fromNick = msg.at(2).toString();
        m.groupId = toU64(msg.at(3));
        m.msgId = msg.at(4).toString();
        m.contentType = msg.at(5).toString();
        m.richText = msg.at(6).toString();
        m.ts = static_cast<qint64>(msg.at(7).toDouble());
        m.outgoing = false;
        m.convId = (m.scope == "g") ? m.groupId : m.fromUserId;

        const QString key = (m.scope == "g") ? keyGroup(m.groupId) : keyUser(m.fromUserId);
        m_history[key].append(m);
        emit chatMessageReceived(m);
        // 回 ACK：在线时无害，离线消息据此出队
        m_sig->send(CHAT_ACK, {m.msgId});
        break;
    }
    case CHAT_ACK:
        // 服务器对我方发送的确认；M2 暂不处理送达回执。
        break;

    default:
        // 其余（ACL_GET_RESP / RELAY_* 等）在 M2 忽略。
        break;
    }
}
