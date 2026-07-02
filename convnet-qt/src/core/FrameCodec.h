#pragma once

// FrameCodec —— 线协议帧编解码，字节级对齐 Go 端 functions.go / portocol.go。
//   编码：[4 字节大端 int32 长度][ JSON(compact) + "\r\n" ]，长度含 "\r\n"。
//   解码：从流式缓冲区中尽可能多地取出完整帧。

#include <QByteArray>
#include <QJsonArray>
#include <QVector>

struct Frame {
    int cmd = -1;
    QJsonArray message;
};

namespace FrameCodec {

// 将一条消息编码为完整帧字节。
QByteArray encode(int cmd, const QJsonArray& message);

// 从 buffer 中取出所有已完整到达的帧，并移除其已消费字节。
// 半包会保留在 buffer 中等待后续数据。
QVector<Frame> drain(QByteArray& buffer);

} // namespace FrameCodec
