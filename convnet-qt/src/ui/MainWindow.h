#pragma once

#include <QMainWindow>
#include <QHash>
#include <QIcon>
#include <QSystemTrayIcon>
#include "model/Types.h"

class AppModel;
class ChatWindow;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QTimer;
class QAction;
class QCloseEvent;
class QFrame;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppModel* model, QWidget* parent = nullptr);

private slots:
    void doLoginFlow();
    void onLoggedIn(const QString& publicId);
    void onLoginFailed(const QString& reason);
    void onConnectionStateChanged(bool connected);
    void onServerError(const QString& message);

    void onFriendsChanged();
    void onGroupsChanged();
    void onFriendRequest(quint64 fromUserId, const QString& nick, const QString& greeting);
    void onGroupJoinRequest(quint64 groupId, quint64 applicantId, const QString& nick);
    void onOpResult(bool ok, const QString& message);
    void onChatMessage(const ChatMessage& m);

    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showAddFriend();
    void showGroupDialog();
    void removeSelectedFriend();

    // 新消息提示 + 右键菜单
    void onTreeContextMenu(const QPoint& pos);
    void onChatActivated(const QString& scope, quint64 targetId);
    void onBlinkTick();
    void updateSignalIcons(); // 定时刷新好友信号格（按最新 RTT）

    // 退出登录 / 网卡状态 / 防火墙
    void doLogout();
    void showNetworkStatus();
    void onNetworkStatusChanged();
    void showFirewall();

    // 托盘 / 免打扰
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleDnd(bool on);
    void onFullscreenCheck();

protected:
    void closeEvent(QCloseEvent* e) override;   // 关闭=最小化到托盘
    void changeEvent(QEvent* e) override;        // 最小化=隐藏到托盘

private:
    ChatWindow* chatFor(const QString& scope, quint64 targetId, const QString& title);
    void openFriendChat(quint64 uid);
    void openGroupChat(quint64 gid);
    quint64 pickGroupMember(const GroupInfo& g, const QString& title); // 选一名成员(排除自己)，0=取消
    void updateStatus();
    void showUserInfo(const FriendInfo& f);
    void showConnectionSummary();
    void markUnread(const QString& convKey);
    void clearUnread(const QString& convKey);
    void rerenderLists();
    void scheduleRebuild(); // 合并短时间内的多次列表重建，避免抖动卡顿
    void updateModeWarning(); // 检测在线对端网卡模式不一致，显示告警条并支持一键切换
    void applyUnreadBackgrounds();
    QString unreadBadge(const QString& convKey) const;

    AppModel* m_model = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTreeWidgetItem* m_friendRoot = nullptr;
    QTreeWidgetItem* m_groupRoot = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_selfNick = nullptr;
    QLabel* m_selfIp = nullptr;
    QLabel* m_selfBadge = nullptr;
    QFrame* m_modeWarnBar = nullptr;    // 网卡模式不一致告警条
    QLabel* m_modeWarnLabel = nullptr;
    QPushButton* m_modeWarnBtn = nullptr;
    QString m_suggestMode;              // 建议切换到的模式（对端多数模式）
    bool m_connected = false;
    bool m_loggedIn = false;
    bool m_inLogin = false;
    QHash<QString, ChatWindow*> m_chats;

    QHash<QString, int> m_unread;   // convKey -> 未读条数
    QTimer* m_blinkTimer = nullptr;
    QTimer* m_signalTimer = nullptr;
    QTimer* m_rebuildTimer = nullptr; // 合并列表重建（单次触发）
    bool m_blinkOn = false;

    // 托盘 / 免打扰
    void setupTray();
    bool dndActive() const { return m_manualDnd || m_autoDnd; }
    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_dndAction = nullptr;
    QTimer* m_fullscreenTimer = nullptr;
    QIcon m_trayNormal;
    QIcon m_trayAlert;
    bool m_manualDnd = false;
    bool m_autoDnd = false;
    bool m_reallyQuit = false;
};
