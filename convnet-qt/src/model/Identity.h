#pragma once

// Identity —— 本机 + 账号相关的配置，QSettings 持久化。
// 身份由「账号」决定（服务器分配 userID/PublicID），不再依赖机器 UUID，
// 因此同机可登录不同账号，无需 profile。
// 伪 MAC 仍是每台机器一份（供 M3 网络层用）。

#include <QString>

class Identity {
public:
    static Identity& instance();

    void load();
    void save();

    // 账号相关
    QString account;         // 登录账号
    QString password;        // 仅当 rememberPassword 时持久化
    bool    rememberPassword = false;
    QString nick;            // 显示昵称（登录后以服务器回传为准）

    // 机器相关
    QString mac;             // 伪 MAC（M2 仅作标识；M3 由 TAP 网卡接管）
    QString serverHost;      // 默认 127.0.0.1
    quint16 serverPort = 13903;

    // 运行期：登录后服务器返回
    QString publicId;        // md5(account):userID
    QString myCvnIP;         // 本机虚拟IP（服务器按 userID 分配）
    quint64 userId() const;  // 从 publicId 解析

private:
    Identity() = default;
    void ensureDefaults();
};
