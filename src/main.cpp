// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QTimer>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QCommandLineParser>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KIconTheme>

#include "kmixdeck_version.h"
#include "frontend/mixerclient.h"
#include "frontend/kdeintegration.h"
#include "qmltypes.h"
#include <QQuickWindow>

int main(int argc, char *argv[])
{
    KIconTheme::initTheme();
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kmixdeck"));
    QApplication::setOrganizationName(QStringLiteral("kmixdeck"));
    QApplication::setOrganizationDomain(QStringLiteral("kmixdeck.org"));
    QApplication::setApplicationName(QStringLiteral("kmixdeck-kde"));
    QApplication::setDesktopFileName(QStringLiteral("org.kmixdeck.kmixdeck"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("audio-card")));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    KAboutData about(QStringLiteral("kmixdeck"), i18n("kmixdeck"), QStringLiteral(KMIXDECK_VERSION_STRING),
                     i18n("A streamer's audio mixer for KDE Plasma on PipeWire"),
                     KAboutLicense::GPL_V3, i18n("© 2026 kmixdeck contributors"));
    about.setHomepage(QStringLiteral("https://github.com/TensorRaya/kmixdeck"));
    about.setBugAddress("https://github.com/TensorRaya/kmixdeck/issues");
    about.addAuthor(i18n("Raya Elena Solano"), i18n("Maintainer"));
    KAboutData::setApplicationData(about);
    KCrash::initialize();

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption selfTest(QStringLiteral("self-test"), QStringLiteral("Load the UI, then exit (used by ctest)."));
    const QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("Render the window to <file>.png and exit (works offscreen)."), QStringLiteral("file"));
    const QCommandLineOption openArg(QStringLiteral("open"), QStringLiteral("With --screenshot: open this dialog first (channel|mix)."), QStringLiteral("what"));
    const QCommandLineOption sizeArg(QStringLiteral("size"), QStringLiteral("With --screenshot: window size WxH (default 1280x760)."), QStringLiteral("wxh"));
    const QCommandLineOption gestureArg(QStringLiteral("gesture"), QStringLiteral("Patchbay gesture to perform, then exit (tests)."), QStringLiteral("spec"));
    const QCommandLineOption probeArg(QStringLiteral("probe"), QStringLiteral("Print <objectName>.<property> of a UI item after --open, then exit (tests)."), QStringLiteral("spec"));
    parser.addOption(selfTest); parser.addOption(shot); parser.addOption(openArg); parser.addOption(sizeArg); parser.addOption(gestureArg); parser.addOption(probeArg);
    parser.process(app);
    about.processCommandLine(&parser);

    // one instance; a second launch raises the window — except for the headless modes, which must run next to a
    // live UI (2026-09-16: --screenshot silently exited 0 because Unique handed the call to the running instance)
    const bool headless = parser.isSet(selfTest) || parser.isSet(shot) || parser.isSet(gestureArg) || parser.isSet(probeArg);
    KDBusService service(headless ? KDBusService::Multiple | KDBusService::NoExitOnFailure : KDBusService::Unique);

    // One client shared by QML (as the "Mixer" singleton) and by the KDE integration (tray, shortcuts).
    auto *client = new kmixdeck::frontend::MixerClient(&app);
    MixerForeign::setInstance(client);
    kmixdeck::frontend::KdeIntegration kde(client);

    QQmlApplicationEngine engine;
    // --self-test also fails on QML *warnings* (ReferenceError, TypeError, unresolved bindings): the 2026-09-16
    // `band is not defined` in Fader.qml loaded fine and only broke at runtime — exit 0 would have hidden it.
    int qmlWarnings = 0;
    // Only OUR files count (org/kmixdeck/); Kirigami's own binding-loop notices are not ours to fix.
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&qmlWarnings](const QList<QQmlError> &w) { for (const auto &e : w) { qWarning().noquote() << "QML:" << e.toString(); if (e.url().toString().contains(QLatin1String("/org/kmixdeck/"))) ++qmlWarnings; } });
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("org.kmixdeck", "Main");
    if (!engine.rootObjects().isEmpty()) kde.setMainWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    // --self-test: load every QML file, then quit — ctest runs this offscreen so a broken binding
    // (2026-09-16: a duplicate `font` assignment made the UI exit 1 without a message) fails the build, not the user.
    if (parser.isSet(selfTest)) {
        if (engine.rootObjects().isEmpty()) return 1;
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        win->resize(1152, 676); win->show();   // laptop size: bindings that only run once items are laid out
        QTimer::singleShot(900, &app, [win] { QMetaObject::invokeMethod(win, "addDialogOpen", Q_ARG(QVariant, QStringLiteral("channel"))); });
        QTimer::singleShot(1500, &app, [win] { QMetaObject::invokeMethod(win, "showRouting"); });
        QTimer::singleShot(2100, &app, [win] { QMetaObject::invokeMethod(win, "showPatchbay"); });
        QTimer::singleShot(3000, &app, [&qmlWarnings] { QCoreApplication::exit(qmlWarnings > 0 ? 2 : 0); });
    }
    // --gesture "connect:<fromCard>|<fromPos>|<toCard>|<toPos>" / "remove:<kind>|<channel|mix>|<ref>" — the patchbay's
    // drag/click, driven from the shell so the integration tests can prove the gestures reach the daemon (DV-24).
    if (parser.isSet(gestureArg)) {
        if (engine.rootObjects().isEmpty()) return 1;
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QStringList gestures = parser.values(gestureArg);
        const QString openG = parser.value(openArg);
        if (!parser.isSet(probeArg)) QTimer::singleShot(900, &app, [win, openG] {
            if (openG == QLatin1String("apps")) QMetaObject::invokeMethod(win, "showApps");
            else if (openG == QLatin1String("routing")) QMetaObject::invokeMethod(win, "showRouting");
            else if (openG == QLatin1String("patchbay")) QMetaObject::invokeMethod(win, "showPatchbay");
        });
        QTimer::singleShot(1200, &app, [win, gestures] {
            for (const QString &g : gestures) {
                const QString op = g.section(QLatin1Char(':'), 0, 0), rest = g.section(QLatin1Char(':'), 1);
                const QStringList a = rest.split(QLatin1Char('|'));
                QVariant ret;
                if (op == QLatin1String("connect") && a.size() == 4) QMetaObject::invokeMethod(win, "gestureConnect", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2]), Q_ARG(QVariant, a[3]));
                else if (op == QLatin1String("remove") && a.size() == 3) QMetaObject::invokeMethod(win, "gestureRemove", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2]));
                else if (op == QLatin1String("drop") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureDrop", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                else if (op == QLatin1String("wirepopup") && a.size() == 3) QMetaObject::invokeMethod(win, "gestureWirePopup", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2]));
                else if (op == QLatin1String("monitors") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureMonitors", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                fprintf(stdout, "gesture %s -> %s\n", qPrintable(g), qPrintable(ret.toString().isEmpty() ? QStringLiteral("ok") : ret.toString()));
            }
            fflush(stdout);
        });
        if (!parser.isSet(probeArg)) QTimer::singleShot(2200, &app, [] { QCoreApplication::exit(0); });
    }
    if (parser.isSet(probeArg)) {
        if (engine.rootObjects().isEmpty()) return 1;
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QString open = parser.value(openArg); const QStringList probes = parser.values(probeArg);
        win->resize(1280, 760); win->show();
        QTimer::singleShot(900, &app, [win, open] {
            if (open == QLatin1String("apps")) QMetaObject::invokeMethod(win, "showApps");
            else if (open == QLatin1String("routing")) QMetaObject::invokeMethod(win, "showRouting");
            else if (open == QLatin1String("patchbay")) QMetaObject::invokeMethod(win, "showPatchbay");
        });
        QTimer::singleShot(2000, &app, [win, probes] {
            for (const QString &p : probes) { QVariant ret; QMetaObject::invokeMethod(win, "probe", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, p)); fprintf(stdout, "probe %s = %s\n", qPrintable(p), qPrintable(ret.toString())); }
            fflush(stdout); QCoreApplication::exit(0);
        });
    }
    // --screenshot: the UI as a reviewable artefact without a compositor (docs, PR review, "what does it look like
    // on the laptop" without grabbing 3×4K HDR outputs). Waits for the first frames, optionally opens a dialog.
    if (parser.isSet(shot)) {
        if (engine.rootObjects().isEmpty()) return 1;
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QString file = parser.value(shot), open = parser.value(openArg);
        const QStringList wh = parser.value(sizeArg).split(QLatin1Char('x'));
        win->resize(wh.size() == 2 ? wh[0].toInt() : 1280, wh.size() == 2 ? wh[1].toInt() : 760); win->show();
        QTimer::singleShot(1200, &app, [win, file, open] {
            if (open == QLatin1String("routing")) QMetaObject::invokeMethod(win, "showRouting");
            else if (open == QLatin1String("patchbay")) QMetaObject::invokeMethod(win, "showPatchbay");
            else if (open == QLatin1String("apps")) QMetaObject::invokeMethod(win, "showApps");
            else if (open == QLatin1String("channel-ports")) QMetaObject::invokeMethod(win, "addDialogOpenPorts");
            else if (!open.isEmpty()) QMetaObject::invokeMethod(win, "addDialogOpen", Q_ARG(QVariant, open));
            QTimer::singleShot(900, win, [win, file] {
                const bool ok = win->grabWindow().save(file);
                qInfo("%s %s", ok ? "screenshot written:" : "screenshot FAILED:", qPrintable(file));
                QCoreApplication::exit(ok ? 0 : 1);
            });
        });
    }

    // Closing the window keeps the tray item (and the shortcuts) alive; quit via the tray menu (CT-4).
    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&service, &KDBusService::activateRequested, &engine, [&engine] {
        for (auto *o : engine.rootObjects()) { QMetaObject::invokeMethod(o, "show"); QMetaObject::invokeMethod(o, "raise"); }
    });
    return app.exec();
}
