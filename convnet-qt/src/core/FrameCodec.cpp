#include "core/FrameCodec.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace FrameCodec {

QByteArray encode(int cmd, const QJsonArray& message)
{
    QJsonObject obj;
    obj.insert("Version", QStringLiteral("1.0"));
    obj.insert("CMDType", cmd);
    obj.insert("Message", message);

    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    payload.append("\r\n"); // 与 Go: jsonStr + "\r\n"，长度含此两字节

    const quint32 n = static_cast<quint32>(payload.size());
    QByteArray out;
    out.reserve(4 + payload.size());
    out.append(static_cast<char>((n >> 24) & 0xFF));
    out.append(static_cast<char>((n >> 16) & 0xFF));
    out.append(static_cast<char>((n >> 8) & 0xFF));
    out.append(static_cast<char>(n & 0xFF));
    out.append(payload);
    return out;
}

QVector<Frame> drain(QByteArray& buffer)
{
    QVector<Frame> frames;
    const int kLen = 4;

    while (buffer.size() >= kLen) {
        const quint8 b0 = static_cast<quint8>(buffer.at(0));
        const quint8 b1 = static_cast<quint8>(buffer.at(1));
        const quint8 b2 = static_cast<quint8>(buffer.at(2));
        const quint8 b3 = static_cast<quint8>(buffer.at(3));
        const quint32 n = (quint32(b0) << 24) | (quint32(b1) << 16) |
                          (quint32(b2) << 8) | quint32(b3);

        if (static_cast<quint32>(buffer.size()) < static_cast<quint32>(kLen) + n)
            break; // 半包，等更多数据

        const QByteArray payload = buffer.mid(kLen, static_cast<int>(n));
        buffer.remove(0, kLen + static_cast<int>(n));

        QJsonParseError err{};
        // 载荷尾部含 "\r\n"，trimmed 后再解析
        const QJsonDocument doc = QJsonDocument::fromJson(payload.trimmed(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        Frame f;
        f.cmd = obj.value("CMDType").toInt(-1);
        f.message = obj.value("Message").toArray();
        frames.append(f);
    }
    return frames;
}

} // namespace FrameCodec
