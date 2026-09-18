// SPDX-License-Identifier: GPL-3.0-or-later
// kmixdeckd — the service. No UI, no widgets: QCoreApplication + PipeWire + D-Bus.
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <csignal>
#include <KSignalHandler>
#include "service.h"
#include "kmixdeck_version.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kmixdeckd"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KMIXDECK_VERSION_STRING));
    QCommandLineParser p; p.addHelpOption(); p.addVersionOption();
    p.setApplicationDescription(QStringLiteral("kmixdeck service: owns the PipeWire mixer graph, exports it on the session bus as org.kmixdeck1"));
    p.process(app);

    kmixdeck::daemon::Service service;
    if (!service.start()) return 2;
    // SIGTERM/SIGINT → clean quit. KSignalHandler turns the signal into a Qt signal on the event loop (self-pipe), so
    // nothing runs in signal context — QCoreApplication::quit() is not async-signal-safe by contract.
    KSignalHandler::self()->watchSignal(SIGTERM); KSignalHandler::self()->watchSignal(SIGINT);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &app, [](int) { QCoreApplication::quit(); });
    qInfo() << "kmixdeckd" << KMIXDECK_VERSION_STRING << "on" << kmixdeck::daemon::kBusName;
    return app.exec();
}
