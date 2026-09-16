// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include <QApplication>
#include <QQmlApplicationEngine>
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
    parser.addOption(selfTest);
    parser.process(app);
    about.processCommandLine(&parser);

    KDBusService service(KDBusService::Unique);   // one instance; second launch raises the window

    // One client shared by QML (as the "Mixer" singleton) and by the KDE integration (tray, shortcuts).
    auto *client = new kmixdeck::frontend::MixerClient(&app);
    MixerForeign::setInstance(client);
    kmixdeck::frontend::KdeIntegration kde(client);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("org.kmixdeck", "Main");
    if (!engine.rootObjects().isEmpty()) kde.setMainWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    // --self-test: load every QML file, then quit — ctest runs this offscreen so a broken binding
    // (2026-09-16: a duplicate `font` assignment made the UI exit 1 without a message) fails the build, not the user.
    if (parser.isSet(selfTest)) {
        if (engine.rootObjects().isEmpty()) return 1;
        QTimer::singleShot(1500, &app, [] { QCoreApplication::exit(0); });
    }

    // Closing the window keeps the tray item (and the shortcuts) alive; quit via the tray menu (CT-4).
    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&service, &KDBusService::activateRequested, &engine, [&engine] {
        for (auto *o : engine.rootObjects()) { QMetaObject::invokeMethod(o, "show"); QMetaObject::invokeMethod(o, "raise"); }
    });
    return app.exec();
}
