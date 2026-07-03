#pragma once

// Types.h —— IM 客户端的核心数据结构（GUI 线程内使用）。

#include <QString>
#include <QVector>
#include <QMetaType>

struct FriendInfo {
    quint64 userId = 0;
    QString publicId;
    QString nick;
    QString cvnIP;
    QString mac;
    bool    online = false;
    QString connMethod; // 网络层对接方式（P2P直连/服务器中继/连接中…），运行期由 P2PManager 更新
    QString nicMode;    // 对端网卡模式 "tun"/"tap"（服务器随花名册/在线态下发；用于一致性提示）
};

struct GroupInfo {
    quint64 groupId = 0;
    QString name;
    quint64 ownerId = 0;
    int     memberCount = 0;
    bool    hasPassword = false; // 是否设置了入群密码（凭密码免审批加入）
    QVector<quint64> admins;     // 管理员 userID（群主之外的协管）
    QVector<FriendInfo> members;
};

struct ChatMessage {
    QString scope;        // "u" 单聊 | "g" 群聊
    quint64 fromUserId = 0;
    QString fromNick;
    quint64 groupId = 0;  // 群聊时有效
    quint64 convId = 0;   // 会话标识：单聊=对端 userId；群聊=groupId
    QString msgId;
    QString contentType;  // "rich" | "text"
    QString richText;     // HTML 片段
    qint64  ts = 0;
    bool    outgoing = false;
};

Q_DECLARE_METATYPE(FriendInfo)
Q_DECLARE_METATYPE(GroupInfo)
Q_DECLARE_METATYPE(ChatMessage)
