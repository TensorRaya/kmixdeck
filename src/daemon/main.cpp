// SPDX-License-Identifier: GPL-3.0-or-later
// kmixdeckd — the service. No UI, no widgets: QCoreApplication + PipeWire + D-Bus.
#include <QCoreApplication>
#include "../logging.h"
#include <QCommandLineParser>
#include <QDebug>
#include <csignal>
#include <KSignalHandler>
#include <sys/resource.h>
#include "service.h"
#include "kmixdeck_version.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kmixdeckd"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KMIXDECK_VERSION_STRING));
    QCommandLineParser p; p.addHelpOption(); p.addVersionOption();
    p.setApplicationDescription(QStringLiteral("kmixdeck service: owns the PipeWire mixer graph, exports it on the session bus as org.kmixdeck1"));
    p.process(app);

    // Each graph edge is a PipeWire client with ~10 fds; a 32x32 desk needs ~1000. Lift the soft limit to the hard
    // one so a hand-started or D-Bus-activated daemon does not stop at 1024 (systemd unit sets LimitNOFILE too).
    rlimit rl{}; if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max) { rl.rlim_cur = rl.rlim_max; setrlimit(RLIMIT_NOFILE, &rl); }

    kmixdeck::daemon::Service service;
    if (!service.start()) return 2;
    // SIGTERM/SIGINT → clean quit. KSignalHandler turns the signal into a Qt signal on the event loop (self-pipe), so
    // nothing runs in signal context — QCoreApplication::quit() is not async-signal-safe by contract.
    KSignalHandler::self()->watchSignal(SIGTERM); KSignalHandler::self()->watchSignal(SIGINT);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &app, [](int) { QCoreApplication::quit(); });
    qCInfo(lcDbus) << "kmixdeckd" << KMIXDECK_VERSION_STRING << "on" << kmixdeck::daemon::kBusName;
    return app.exec();
}
