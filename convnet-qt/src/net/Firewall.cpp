#include "net/Firewall.h"

#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

void Firewall::load()
{
    QSettings s("convnet", "convnet-qt");
    const QByteArray raw = s.value("firewall/rules").toByteArray();
    QVector<FwRule> loaded;
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        FwRule r;
        r.enabled = o.value("enabled").toBool(true);
        r.direction = o.value("direction").toInt(0);
        r.action = o.value("action").toInt(1);
        r.peerUserId = static_cast<quint64>(o.value("peer").toDouble(0));
        r.protocol = o.value("proto").toInt(0);
        r.portLow = o.value("portLow").toInt(0);
        r.portHigh = o.value("portHigh").toInt(0);
        loaded.append(r);
    }
    std::lock_guard<std::mutex> lk(m_mtx);
    m_rules = loaded;
}

void Firewall::save() const
{
    QJsonArray arr;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (const FwRule& r : m_rules) {
            QJsonObject o;
            o["enabled"] = r.enabled;
            o["direction"] = r.direction;
            o["action"] = r.action;
            o["peer"] = static_cast<double>(r.peerUserId);
            o["proto"] = r.protocol;
            o["portLow"] = r.portLow;
            o["portHigh"] = r.portHigh;
            arr.append(o);
        }
    }
    QSettings s("convnet", "convnet-qt");
    s.setValue("firewall/rules", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVector<FwRule> Firewall::rules() const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_rules;
}

void Firewall::setRules(const QVector<FwRule>& rules)
{
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_rules = rules;
    }
    save();
}

bool Firewall::allow(quint64 peerUserId, int direction, const QByteArray& packet) const
{
    // 仅解析 IPv4；非 IPv4 一律放行
    if (packet.size() < 20)
        return true;
    const quint8 b0 = static_cast<quint8>(packet.at(0));
    if ((b0 >> 4) != 4)
        return true;
    const int ihl = (b0 & 0x0F) * 4;
    const quint8 proto = static_cast<quint8>(packet.at(9));
    int dstPort = -1;
    if ((proto == 6 || proto == 17) && packet.size() >= ihl + 4)
        dstPort = (static_cast<quint8>(packet.at(ihl + 2)) << 8) | static_cast<quint8>(packet.at(ihl + 3));

    std::lock_guard<std::mutex> lk(m_mtx);
    for (const FwRule& r : m_rules) {
        if (!r.enabled)
            continue;
        if (r.direction != 0 && r.direction != direction)
            continue;
        if (r.peerUserId != 0 && r.peerUserId != peerUserId)
            continue;
        if (r.protocol != 0 && r.protocol != proto)
            continue;
        if (r.portHigh > 0) { // 端口受限规则：仅匹配带端口的 TCP/UDP
            if (dstPort < 0)
                continue;
            if (dstPort < r.portLow || dstPort > r.portHigh)
                continue;
        }
        return r.action == 0; // 0=允许 -> true；1=拒绝 -> false
    }
    return true; // 默认放行
}
