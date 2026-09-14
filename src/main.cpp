// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include <QApplication>
#include <QQmlApplicationEngine>
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
    parser.process(app);
    about.processCommandLine(&parser);

    KDBusService service(KDBusService::Unique);   // one instance; second launch raises the window

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("org.kmixdeck", "Main");

    QObject::connect(&service, &KDBusService::activateRequested, &engine, [&engine] {
        for (auto *o : engine.rootObjects()) QMetaObject::invokeMethod(o, "raise");
    });
    return app.exec();
}
