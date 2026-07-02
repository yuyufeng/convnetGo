#pragma once

// AppModel —— 客户端单一数据源。订阅 SignalingClient 的 frameReceived，
// 维护好友/群组/聊天记录状态，并把变化以 Qt 信号发给 UI（同线程直连）。

#include <QObject>
#include <QHash>
#include <QVector>
#include <QJsonArray>

#include "model/Types.h"

class SignalingClient;
class P2PManager;
class TapManager;
class Router;
class RelayTransport;
class Firewall;
class QTimer;

class AppModel : public QObject {
    Q_OBJECT
public:
    explicit AppModel(SignalingClient* sig, QObject* parent = nullptr);
    ~AppModel() override;

    // ---- 连接/登录 ----
    void login(const QString& host, quint16 port, const QString& account, const QString& password);
    void registerAccount(const QString& host, quint16 port,
                         const QString& account, const QString& password, const QString& nick);
    void logout();
    bool isConnected() const;

    // ---- 好友 ----
    void searchFriends(const QString& keyword);
    void requestFriend(quint64 userId, const QString& greeting);
    void acceptFriend(quint64 fromUserId, bool accept);
    void removeFriend(quint64 userId);
    void refreshFriends();

    // ---- 群组 ----
    void createGroup(const QString& name, const QString& password);
    void searchGroups(const QString& keyword);
    void joinGroup(quint64 groupId, const QString& password);
    void approveJoin(quint64 groupId, quint64 applicantId, bool approve);
    void leaveGroup(quint64 groupId);
    void manageGroup(quint64 groupId, const QString& action, quint64 target); // kick/grant/revoke/transfer/handover/disband
    void refreshGroups();

    // ---- 聊天 ----
    void sendChat(const QString& scope, quint64 targetId, const QString& richText);

    // ---- 状态访问 ----
    QVector<FriendInfo> friends() const;
    QVector<GroupInfo> groups() const;
    bool friendById(quint64 userId, FriendInfo& out) const;
    bool groupById(quint64 groupId, GroupInfo& out) const;
    QVector<ChatMessage> history(const QString& scope, quint64 targetId) const;
    QString myPublicId() const { return m_publicId; }
    quint64 myUserId() const { return m_userId; }
    QString myNick() const;
    QString displayName(quint64 userId) const; // 好友昵称，未知则回退 uid

    // 网卡状态（供 UI 查看）
    bool    tapUp() const { return m_tapUp; }
    QString tapIfName() const { return m_tapIfName; }
    QString tapError() const { return m_tapError; }
    QString myCvnIP() const;
    void    peerTraffic(const QString& publicId, quint64& sent, quint64& recv) const; // 按对端收发字节
    int     peerRttMs(const QString& publicId) const; // 与对端的 P2P 往返时延(ms)，-1=未知
    Firewall* firewall() const { return m_fw; } // 供防火墙设置界面使用

signals:
    void connectionStateChanged(bool connected);
    void loggedIn(const QString& publicId);
    void loginFailed(const QString& reason);
    void serverError(const QString& message);
    void reconnecting(int seconds); // 断线后自动重连中

    void friendsChanged();
    void groupsChanged();
    void networkStatusChanged();
    void searchFriendsResult(const QVector<FriendInfo>& results);
    void searchGroupsResult(const QVector<GroupInfo>& results);

    void friendRequestReceived(quint64 fromUserId, const QString& fromNick, const QString& greeting);
    void groupJoinRequestReceived(quint64 groupId, quint64 applicantId, const QString& applicantNick);
    void presenceChanged(quint64 userId, bool online);
    void chatMessageReceived(const ChatMessage& msg);
    void opResult(bool ok, const QString& message);

private slots:
    void onFrame(int cmd, const QJsonArray& msg);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(const QString& message);
    void onPeerMethodChanged(const QString& peerPublicId);
    void tryReconnect();

private:
    enum class PendingAuth { None, Login, Register };

    void sendAuth();
    void scheduleReconnect();
    void connectOnlinePeers();      // 为在线好友/组员建立 P2P 链路
    void startNetwork();            // 登录后按 myCvnIP 起虚拟网卡
    void rebuildRoutes();           // roster -> cvnIP→publicId 路由表
    static QString keyUser(quint64 uid);
    static QString keyGroup(quint64 gid);

    SignalingClient* m_sig = nullptr;
    P2PManager* m_p2p = nullptr;
    RelayTransport* m_relay = nullptr;
    Router* m_router = nullptr;
    TapManager* m_tap = nullptr;
    Firewall* m_fw = nullptr;
    bool m_netStarted = false;
    bool m_tapUp = false;
    QString m_tapIfName;
    QString m_tapError;
    QString m_publicId;
    quint64 m_userId = 0;
    int m_msgSeq = 0;

    PendingAuth m_pending = PendingAuth::None;
    QString m_pendingAccount;
    QString m_pendingPassword;
    QString m_pendingNick;

    // 断线重连
    bool m_wantConnected = false;    // 登录成功后为 true；logout/认证失败置 false
    QString m_authHost;
    quint16 m_authPort = 0;
    QTimer* m_reconnectTimer = nullptr;
    int m_reconnectDelayMs = 2000;

    QHash<quint64, FriendInfo> m_friends;
    QHash<quint64, GroupInfo> m_groups;
    QHash<QString, QVector<ChatMessage>> m_history;
};
