#pragma once

#include <QByteArray>

namespace DynamixelProtocol {

constexpr quint8 pingInstruction = 0x01;
constexpr quint8 readInstruction = 0x02;
constexpr quint8 syncWriteInstruction = 0x83;

struct Packet
{
    quint8 id = 0;
    quint8 code = 0;
    QByteArray parameters;
};

QByteArray makePacket(quint8 id, quint8 code,
                      const QByteArray &parameters = {});
bool takePacket(QByteArray &buffer, Packet &packet);

}
