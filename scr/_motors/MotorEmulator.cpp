#include "DynamixelProtocol.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {
constexpr int motorCount = 6;
constexpr quint16 serverPort = 45454;
constexpr quint8 presentPositionAddress = 36;
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
                            const quint16 position = simulatedPosition(
                                request.id, clock.elapsed());
                            responseParameters.append(char(position & 0xff));
                            responseParameters.append(char((position >> 8) & 0xff));
                            shouldRespond = true;
                        }
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
