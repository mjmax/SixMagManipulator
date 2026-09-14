#include "DynamixelProtocol.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr int motorCount = 6;
constexpr quint16 serverPort = 45454;
constexpr quint8 presentPositionAddress = 36;
constexpr quint8 movingSpeedAddress = 32;
constexpr quint8 goalPositionAddress = 30;
constexpr quint8 broadcastId = 0xfe;
constexpr double pi = 3.14159265358979323846;

quint16 simulatedPosition(int id, qint64 elapsedMilliseconds)
{
    const double seconds = elapsedMilliseconds / 1000.0;
    const double phase = static_cast<double>(id) * pi / 3.0;
    const double raw = 511.5 + 220.0 * std::sin(seconds * 0.55 + phase);
    return static_cast<quint16>(std::clamp(std::lround(raw), 0L, 1023L));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("SixMag Motor Emulator"));

    QTextStream output(stdout);
    QTcpServer server;
    QElapsedTimer clock;
    clock.start();
    std::array<double, motorCount> positions{};
    std::array<double, motorCount> goals{};
    std::array<int, motorCount> speedRegisters{};
    std::array<qint64, motorCount> lastUpdates{};
    std::array<bool, motorCount> goalMode{};
    speedRegisters.fill(315);
    const auto updateMotor = [&](int index) {
        const qint64 now = clock.elapsed();
        if (!goalMode[index]) {
            positions[index] = simulatedPosition(index + 1, now);
        } else {
            const double seconds = std::max<qint64>(0, now - lastUpdates[index])
                / 1000.0;
            const double rpm = speedRegisters[index] == 0 ? 97.0
                : std::min(97.0, speedRegisters[index] * 0.111);
            const double maxRawStep = rpm * 6.0 * 1023.0 / 300.0 * seconds;
            positions[index] += std::clamp(
                goals[index] - positions[index], -maxRawStep, maxRawStep);
        }
        lastUpdates[index] = now;
    };
    QHash<QTcpSocket *, QByteArray> buffers;

    QObject::connect(&server, &QTcpServer::newConnection, &application, [&] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            buffers.insert(socket, {});
            output << "GUI connected\n" << Qt::flush;

            QObject::connect(socket, &QTcpSocket::readyRead,
                             &application, [&, socket] {
                QByteArray &buffer = buffers[socket];
                buffer.append(socket->readAll());
                DynamixelProtocol::Packet request;
                while (DynamixelProtocol::takePacket(buffer, request)) {
                    if (request.id == broadcastId
                        && request.code == DynamixelProtocol::syncWriteInstruction) {
                        const QByteArray &parameters = request.parameters;
                        if (parameters.size() == 2 + motorCount * 3
                            && static_cast<quint8>(parameters.at(0)) == goalPositionAddress
                            && static_cast<quint8>(parameters.at(1)) == 2) {
                            for (int entry = 0; entry < motorCount; ++entry) {
                                const int offset = 2 + entry * 3;
                                const int id = static_cast<quint8>(parameters.at(offset));
                                if (id < 1 || id > motorCount)
                                    continue;
                                const int index = id - 1;
                                updateMotor(index);
                                goals[index] = static_cast<quint8>(parameters.at(offset + 1))
                                    | (static_cast<quint16>(static_cast<quint8>(
                                           parameters.at(offset + 2))) << 8);
                                goals[index] = std::clamp(goals[index], 0.0, 1023.0);
                                goalMode[index] = true;
                            }
                        }
                        continue; // Broadcast sync writes have no status packet.
                    }
                    if (request.id == 0 || request.id > motorCount)
                        continue;

                    QByteArray responseParameters;
                    bool shouldRespond = false;
                    if (request.code == DynamixelProtocol::pingInstruction) {
                        shouldRespond = true;
                    } else if (request.code == DynamixelProtocol::readInstruction
                               && request.parameters.size() >= 2) {
                        const quint8 address = static_cast<quint8>(
                            request.parameters.at(0));
                        const quint8 length = static_cast<quint8>(
                            request.parameters.at(1));
                        if (address == presentPositionAddress && length == 2) {
                            const int index = request.id - 1;
                            updateMotor(index);
                            const quint16 position = static_cast<quint16>(
                                std::lround(positions[index]));
                            responseParameters.append(char(position & 0xff));
                            responseParameters.append(char((position >> 8) & 0xff));
                            shouldRespond = true;
                        }
                    } else if (request.code == DynamixelProtocol::writeInstruction
                               && request.parameters.size() == 3
                               && static_cast<quint8>(request.parameters.at(0))
                                   == movingSpeedAddress) {
                        const int index = request.id - 1;
                        speedRegisters[index] =
                            static_cast<quint8>(request.parameters.at(1))
                            | (static_cast<quint16>(static_cast<quint8>(
                                   request.parameters.at(2))) << 8);
                        shouldRespond = true;
                    }

                    if (shouldRespond) {
                        socket->write(DynamixelProtocol::makePacket(
                            request.id, 0, responseParameters));
                    }
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected,
                             &application, [&, socket] {
                buffers.remove(socket);
                socket->deleteLater();
                output << "GUI disconnected\n" << Qt::flush;
            });
        }
    });

    if (!server.listen(QHostAddress::LocalHost, serverPort)) {
        output << "Unable to start emulator: " << server.errorString()
               << "\n" << Qt::flush;
        return 1;
    }

    output << "Six AX-18A motors (IDs 1-6) emulated on localhost:"
           << serverPort << "\nKeep this window open while testing the GUI.\n"
           << Qt::flush;
    return application.exec();
}
