#include "DynamixelProtocol.h"

#include <algorithm>

namespace DynamixelProtocol {

QByteArray makePacket(quint8 id, quint8 code, const QByteArray &parameters)
{
    const quint8 length = static_cast<quint8>(parameters.size() + 2);
    quint8 sum = static_cast<quint8>(id + length + code);

    QByteArray packet;
    packet.reserve(parameters.size() + 6);
    packet.append(char(0xff));
    packet.append(char(0xff));
    packet.append(char(id));
    packet.append(char(length));
    packet.append(char(code));
    for (const char value : parameters) {
        packet.append(value);
        sum = static_cast<quint8>(sum + static_cast<quint8>(value));
    }
    packet.append(char(static_cast<quint8>(~sum)));
    return packet;
}

bool takePacket(QByteArray &buffer, Packet &packet)
{
    while (buffer.size() >= 2) {
        const int header = buffer.indexOf(QByteArray("\xff\xff", 2));
        if (header < 0) {
            buffer = buffer.endsWith(char(0xff))
                ? QByteArray(1, char(0xff)) : QByteArray();
            return false;
        }
        if (header > 0)
            buffer.remove(0, header);
        if (buffer.size() < 4)
            return false;

        const quint8 length = static_cast<quint8>(buffer.at(3));
        if (length < 2 || length > 250) {
            buffer.remove(0, 1);
            continue;
        }

        const int packetSize = static_cast<int>(length) + 4;
        if (buffer.size() < packetSize)
            return false;

        quint8 sum = 0;
        for (int index = 2; index < packetSize - 1; ++index)
            sum = static_cast<quint8>(sum
                + static_cast<quint8>(buffer.at(index)));
        const quint8 expectedChecksum = static_cast<quint8>(~sum);
        const quint8 receivedChecksum =
            static_cast<quint8>(buffer.at(packetSize - 1));
        if (expectedChecksum != receivedChecksum) {
            buffer.remove(0, 1);
            continue;
        }

        packet.id = static_cast<quint8>(buffer.at(2));
        packet.code = static_cast<quint8>(buffer.at(4));
        packet.parameters = buffer.mid(5, static_cast<int>(length) - 2);
        buffer.remove(0, packetSize);
        return true;
    }
    return false;
}

}
