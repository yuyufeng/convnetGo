#pragma once

// Protocol.h —— 与 Go 端 portocol.go 逐值对齐的 opcode 枚举。
// 线协议帧：[4 字节大端 int32 长度][ JSON(clientMessage) + "\r\n" ]
// clientMessage = { "Version": "1.0", "CMDType": <int>, "Message": [ ... ] }

namespace proto {

enum Op {
    ALL_DATA = 0,
    WS_REGISTE,                          // 1
    WS_REGISTE_RESP,                     // 2
    WS_REGISTE_FAIL,                     // 3
    C_GETWS_SERVER_INFO,                 // 4
    C_GETWS_SERVER_INFO_RESP,            // 5
    C_CONNTOWS_SERVER,                   // 6
    C_CONNTOWS_PEERCALL,                 // 7
    C_CONNTOWS_PEERCALL_RESP,            // 8
    C_CONNTOWS_PEERCALL_RESP_NOTONLINE,  // 9
    C_CONNTOWS_PEERCALL_FIN,             // 10
    C_CONNTOWS_PEERDISCONNECT,           // 11

    // 好友
    FRIEND_SEARCH,        // 12
    FRIEND_SEARCH_RESP,   // 13
    FRIEND_REQUEST,       // 14
    FRIEND_REQUEST_PUSH,  // 15
    FRIEND_ACCEPT,        // 16
    FRIEND_ACCEPT_PUSH,   // 17
    FRIEND_LIST,          // 18
    FRIEND_LIST_RESP,     // 19
    FRIEND_REMOVE,        // 20
    FRIEND_REMOVE_PUSH,   // 21

    // 用户组
    GROUP_CREATE,             // 22
    GROUP_SEARCH,             // 23
    GROUP_SEARCH_RESP,        // 24
    GROUP_JOIN_REQUEST,       // 25
    GROUP_JOIN_PUSH,          // 26
    GROUP_JOIN_APPROVE,       // 27
    GROUP_JOIN_RESULT_PUSH,   // 28
    GROUP_LEAVE,              // 29
    GROUP_LIST,               // 30
    GROUP_LIST_RESP,          // 31
    GROUP_MEMBER_CHANGE_PUSH, // 32
    GROUP_OP_RESP,            // 33

    // 在线状态
    PRESENCE_NOTIFY,      // 34
    PRESENCE_SUBSCRIBE,   // 35

    // 聊天（服务器存储转发）
    CHAT_SEND,            // 36
    CHAT_DELIVER,         // 37
    CHAT_ACK,             // 38
    CHAT_HISTORY_REQ,     // 39
    CHAT_HISTORY_RESP,    // 40

    // 黑白名单
    ACL_SET,              // 41
    ACL_GET,              // 42
    ACL_GET_RESP,         // 43

    // P2P 失败回退：信令服务器盲转发
    RELAY_DATA,           // 44
    RELAY_DATA_FWD,       // 45
    RELAY_OPEN,           // 46
    RELAY_CLOSE,          // 47

    // 账号密码认证
    ACCOUNT_REGISTER,     // 48 [account, password, nick, mac]
    ACCOUNT_LOGIN,        // 49 [account, password, mac]

    // 群管理：[groupID, action, targetUserID]（kick/grant/revoke/transfer/handover/disband）
    GROUP_MANAGE,         // 50

    // 网卡模式上报（客户端 -> 服务器）：[mode("tun"|"tap")]。服务器随花名册/在线态下发给对端。
    NIC_MODE_REPORT,      // 51

    UNKNKOWN              // 52
};

enum ClientMode {
    CLIENTMODE = 0,
    MAINCLIENT,
    CLIENT,
    SERVER
};

} // namespace proto
