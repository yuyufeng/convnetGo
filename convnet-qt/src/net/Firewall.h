#pragma once

// Firewall —— 虚拟网卡数据面的出入站包过滤（线程安全，纯类）。
// 规则按顺序匹配，首条命中即决定放行/拒绝；无命中默认放行。
// 在 Router 出站(发送前)/入站(写网卡前)各评估一次。

#include <QString>
#include <QByteArray>
#include <QVector>
#include <mutex>

struct FwRule {
    bool    enabled = true;
    int     direction = 0;    // 0=双向, 1=入站, 2=出站
    int     action = 1;       // 0=允许, 1=拒绝
    quint64 peerUserId = 0;   // 0=任意对端
    int     protocol = 0;     // 0=任意, 1=ICMP, 6=TCP, 17=UDP
    int     portLow = 0;      // 目的端口下限（TCP/UDP）；portHigh<=0 表示不限端口
    int     portHigh = 0;     // 目的端口上限
};

class Firewall {
public:
    void load();                                  // 从 QSettings 读取
    void save() const;                            // 写入 QSettings
    QVector<FwRule> rules() const;                // 线程安全拷贝
    void setRules(const QVector<FwRule>& rules);  // 替换并保存

    // direction: 1=入站, 2=出站。返回 true=放行。
    bool allow(quint64 peerUserId, int direction, const QByteArray& packet) const;

    // L2/TAP：仅按对端做“拉黑”判定（忽略帧内容；带协议/端口限定的规则在 L2 不适用）。
    // 返回 true=该对端被阻断。
    bool peerBlocked(quint64 peerUserId, int direction) const;

private:
    mutable std::mutex m_mtx;
    QVector<FwRule> m_rules;
};
