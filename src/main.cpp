// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include <QApplication>
#include "logging.h"
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QWindow>
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
#include <QQuickItem>
#include <QKeyEvent>
#include <QKeySequence>
#include <QAccessible>
#include <QMetaEnum>

int main(int argc, char *argv[])
{
    KIconTheme::initTheme();
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kmixdeck"));
    // UX-5: catalogs come from <prefix>/share/locale via KI18n's normal lookup. For running out of the build tree
    // (tests, developers) KMIXDECK_LOCALE_DIR points at build/locale; it is not read in a packaged install.
    if (const QByteArray dir = qgetenv("KMIXDECK_LOCALE_DIR"); !dir.isEmpty()) KLocalizedString::addDomainLocaleDir(QByteArrayLiteral("kmixdeck"), QString::fromLocal8Bit(dir));
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
    // live UI (--screenshot once silently exited 0 because Unique handed the call to the running instance)
    const bool headless = parser.isSet(selfTest) || parser.isSet(shot) || parser.isSet(gestureArg) || parser.isSet(probeArg);
    KDBusService service(headless ? KDBusService::Multiple | KDBusService::NoExitOnFailure : KDBusService::Unique);

    // One client shared by QML (as the "Mixer" singleton) and by the KDE integration (tray, shortcuts).
    auto *client = new kmixdeck::frontend::MixerClient(&app);
    MixerForeign::setInstance(client);
    kmixdeck::frontend::KdeIntegration kde(client);
    if (headless) client->setHideToTray(false);

    QQmlApplicationEngine engine;
    // --self-test also fails on QML *warnings* (ReferenceError, TypeError, unresolved bindings): the
    // `band is not defined` in Fader.qml loaded fine and only broke at runtime — exit 0 would have hidden it.
    int qmlWarnings = 0;
    // Only OUR files count (org/kmixdeck/); Kirigami's own binding-loop notices are not ours to fix.
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&qmlWarnings](const QList<QQmlError> &w) { for (const auto &e : w) { qCWarning(lcFrontend).noquote() << "QML:" << e.toString(); if (e.url().toString().contains(QLatin1String("/org/kmixdeck/"))) ++qmlWarnings;
        // Fehler AUS Kirigami zaehlen NUR, wenn wir sie ausgeloest haben: eine nicht-Page an
        // pushDialogLayer stirbt in PageRow.qml mit "Value is null", also unter fremder URL — der Filter
        // auf /org/kmixdeck/ hat genau diesen Fall uebersehen, weshalb sich FxPanel monatelang nicht
        // oeffnen liess, waehrend --self-test gruen blieb (gemessen 2026-09-21). Pauschal alles aus
        // Kirigami zu zaehlen geht NICHT: die Bibliothek hat eigene Binding-Loops, die nichts mit uns zu
        // tun haben und den Test sofort dauerhaft rot faerben (auch gemessen). Also praezise diese Signatur.
        else if (e.url().toString().contains(QLatin1String("/kirigami/controls/PageRow.qml"))
                 && e.description().contains(QLatin1String("Value is null"))) ++qmlWarnings; } });
    auto *l10n = new KLocalizedContext(&engine);
    l10n->setTranslationDomain(QStringLiteral("kmixdeck"));   // UX-5: without this the QML i18n() calls look in the empty default domain
    engine.rootContext()->setContextObject(l10n);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("org.kmixdeck", "Main");
    if (!engine.rootObjects().isEmpty()) kde.setMainWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    // --self-test: load every QML file, then quit — ctest runs this offscreen so a broken binding
    // (a duplicate `font` assignment once made the UI exit 1 without a message) fails the build, not the user.
    if (parser.isSet(selfTest)) {
        if (engine.rootObjects().isEmpty()) return 1;
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        win->resize(1152, 676); win->show();   // laptop size: bindings that only run once items are laid out
        QTimer::singleShot(900, &app, [win] { QMetaObject::invokeMethod(win, "addDialogOpen", Q_ARG(QVariant, QStringLiteral("channel"))); });
        QTimer::singleShot(1500, &app, [win] { QMetaObject::invokeMethod(win, "showRouting"); });
        QTimer::singleShot(2100, &app, [win] { QMetaObject::invokeMethod(win, "showPatchbay"); });
        // The global drawer and the tray popover were NOT part of the smoke test until 2026-09-19 — a broken
        // Kirigami.Action child in the drawer still let this exit 0 while every --probe test failed with a
        // KeyError. Touch both surfaces here so a QML mistake in them fails THIS 5-second test instead of a
        // 150-second frontend suite.
        QTimer::singleShot(2400, &app, [win] { QMetaObject::invokeMethod(win, "openDrawerForSelfTest"); });
        QTimer::singleShot(2700, &app, [win] { QMetaObject::invokeMethod(win, "showTrayOverview", Q_ARG(QVariant, 100), Q_ARG(QVariant, 100)); });
        // Die beiden Dialog-Layer AUCH hier pushen (FX-8/FX-9): pushDialogLayer mit einer nicht-Page stirbt
        // in Kirigamis PageRow.qml, und das sah man vorher nur, wenn man das Panel im Fenster von Hand
        // aufmachte — kein Test tat das. Jetzt faellt es in diesen 5 Sekunden auf.
        QTimer::singleShot(2900, &app, [win] { QMetaObject::invokeMethod(win, "fxPanelOpen", Q_ARG(QVariant, QStringLiteral("channel")), Q_ARG(QVariant, QStringLiteral("voice"))); });
        QTimer::singleShot(3100, &app, [win] { QMetaObject::invokeMethod(win, "duckPanelOpen", Q_ARG(QVariant, QStringLiteral("game"))); });
        QTimer::singleShot(3400, &app, [&qmlWarnings] { QCoreApplication::exit(qmlWarnings > 0 ? 2 : 0); });
    }
    // --gesture "connect:<fromCard>|<fromPos>|<toCard>|<toPos>" / "remove:<kind>|<channel|mix>|<ref>" — the patchbay's
    // drag/click, driven from the shell so the integration tests can prove the gestures reach the daemon (DV-24).
    // --open gehoert an EINE Stelle: vorher stand derselbe Zielabgleich zweimal da — einmal im
    // gesture-Zweig (Zeile ~120, aber uebersprungen wenn --probe gesetzt war) und einmal im
    // probe-Zweig. Wer `--open X --gesture Y --probe Z` kombinierte, bekam ein Fenster, in dem X
    // NIE geoeffnet wurde: die Geste lief gegen die nackte Mixer-Seite, die Probes sagten
    // "<not found>", und nichts im Output verriet, dass das Ziel fehlte. Gekostet hat mich das
    // ~40 Minuten Suche in der QML, wo gar kein Fehler war (FX-8, 2026-09-21).
    const auto openZiel = [](QQuickWindow *win, const QString &open) {
        if (open.isEmpty()) return;
        if (open == QLatin1String("apps")) QMetaObject::invokeMethod(win, "showApps");
        else if (open == QLatin1String("routing")) QMetaObject::invokeMethod(win, "showRouting");
        else if (open == QLatin1String("patchbay")) QMetaObject::invokeMethod(win, "showPatchbay");
        else if (open == QLatin1String("channel-ports")) QMetaObject::invokeMethod(win, "addDialogOpenPorts");
        // FX-8: Effekt-Panel, Form "fx/channel/<slug>" oder "fx/mix/<slug>".
        else if (open.startsWith(QLatin1String("fx/"))) {
            const QStringList t = open.split(QLatin1Char('/'));
            if (t.size() == 3) {
                // Rueckgabewert PRUEFEN: ein Tippfehler im Methodennamen laesst invokeMethod
                // still false zurueckgeben — genau die Sorte Schweigen, die diese Suche teuer
                // gemacht hat.
                if (!QMetaObject::invokeMethod(win, "fxPanelOpen", Q_ARG(QVariant, t[1]), Q_ARG(QVariant, t[2])))
                    fprintf(stderr, "kmixdeck: --open %s: fxPanelOpen() not invokable on the root window\n", qPrintable(open));
            }
            else fprintf(stderr, "kmixdeck: --open fx needs fx/channel|mix/<slug>, got '%s'\n", qPrintable(open));
        }
        // FX-9: Ducking-Panel, Form "duck/<slug>" — kanalgebunden, ein Mix hat keinen Trigger.
        else if (open.startsWith(QLatin1String("duck/"))) {
            const QStringList t = open.split(QLatin1Char('/'));
            if (t.size() == 2 && !t[1].isEmpty()) {
                if (!QMetaObject::invokeMethod(win, "duckPanelOpen", Q_ARG(QVariant, t[1])))
                    fprintf(stderr, "kmixdeck: --open %s: duckPanelOpen() not invokable on the root window\n", qPrintable(open));
            }
            else fprintf(stderr, "kmixdeck: --open duck needs duck/<slug>, got '%s'\n", qPrintable(open));
        }
        // Ein unbekanntes Ziel MUSS auffallen, statt lautlos zu verschwinden.
        else fprintf(stderr, "kmixdeck: --open: unknown target '%s' (apps|routing|patchbay|channel-ports|fx/channel|mix/<slug>|duck/<slug>)\n",
                     qPrintable(open));
    };

    if (parser.isSet(gestureArg)) {
        if (engine.rootObjects().isEmpty()) return 1;
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QStringList gestures = parser.values(gestureArg);
        const QString openG = parser.value(openArg);
        // Ziel IMMER oeffnen, auch mit --probe: genau diese Bedingung war der Fehler.
        QTimer::singleShot(900, &app, [win, openG, openZiel] { openZiel(win, openG); });
        QTimer::singleShot(1200, &app, [win, gestures, &kde] {
            for (const QString &g : gestures) {
                const QString op = g.section(QLatin1Char(':'), 0, 0), rest = g.section(QLatin1Char(':'), 1);
                const QStringList a = rest.split(QLatin1Char('|'));
                QVariant ret;
                QCoreApplication::processEvents();   // let the previous gesture's bindings/animations settle before the next one reads
                if (op == QLatin1String("connect") && a.size() == 4) QMetaObject::invokeMethod(win, "gestureConnect", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2]), Q_ARG(QVariant, a[3]));
                else if (op == QLatin1String("remove") && a.size() == 3) QMetaObject::invokeMethod(win, "gestureRemove", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2]));
                else if (op == QLatin1String("drop") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureDrop", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                else if (op == QLatin1String("fader") && a.size() == 3) QMetaObject::invokeMethod(win, "gestureFader", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2]));
                else if (op == QLatin1String("hear") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureHear", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                else if (op == QLatin1String("mixoutput") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureMixOutput", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                else if (op == QLatin1String("mute") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureMute", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                // CT-9: scene save/recall/delete as the window does them. saveScene goes through the real dialog
                // handler (like gestureDuplicate) so the test covers the dialog, not a shortcut around it.
                else if (op == QLatin1String("saveScene") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureSaveScene", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                else if (op == QLatin1String("recallScene") && a.size() >= 1) QMetaObject::invokeMethod(win, "gestureRecallScene", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a.value(1) != QLatin1String("add")));
                else if (op == QLatin1String("deleteScene") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureDeleteScene", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                // CT-7: the file dialogs are native and cannot be scripted offscreen — the gesture takes the path the
                // dialog would have returned and runs the SAME onAccepted handler (root.exportTo / root.importFrom)
                else if (op == QLatin1String("key") && a.size() >= 1) {   // UX-4: a real key press on the focused item, e.g. key:Tab|Tab|Right|Right
                    for (const QString &k : a) {
                        const QKeySequence seq(k); if (seq.isEmpty()) { ret = QStringLiteral("<unknown key %1>").arg(k); break; }
                        const int key = seq[0].key(); const Qt::KeyboardModifiers mods = seq[0].keyboardModifiers();
                        QKeyEvent press(QEvent::KeyPress, key, mods, QKeySequence(seq[0]).toString()), release(QEvent::KeyRelease, key, mods);
                        QCoreApplication::sendEvent(win, &press); QCoreApplication::sendEvent(win, &release);
                        QCoreApplication::processEvents();
                    }
                    if (ret.isNull()) {
                        QQuickItem *f = win->activeFocusItem();
                        if (!f) ret = QStringLiteral("<no focus>");
                        else if (!f->objectName().isEmpty()) ret = f->objectName();
                        else ret = QStringLiteral("<%1 text=%2 parent=%3>").arg(QString::fromLatin1(f->metaObject()->className()), f->property("text").toString(), f->parentItem() ? f->parentItem()->objectName() : QString());
                    }
                }
                else if (op == QLatin1String("a11y") && a.size() == 1) {   // UX-4: what a screen reader would announce for an item
                    QQuickItem *item = nullptr;
                    if (a[0] == QLatin1String("focus")) item = win->activeFocusItem();
                    else { QVariant v; QMetaObject::invokeMethod(win, "itemByName", Q_RETURN_ARG(QVariant, v), Q_ARG(QVariant, a[0])); item = qobject_cast<QQuickItem *>(v.value<QObject *>()); }
                    if (!item) ret = QStringLiteral("<no item %1>").arg(a[0]);
                    else if (QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item)) {
                        static const QMetaEnum roles = QMetaEnum::fromType<QAccessible::Role>();
                        ret = QStringLiteral("%1|%2|%3").arg(QString::fromLatin1(roles.valueToKey(iface->role())), iface->text(QAccessible::Name), iface->text(QAccessible::Description));
                    } else ret = QStringLiteral("<no accessible interface on %1>").arg(a[0]);
                }
                else if (op == QLatin1String("shot") && a.size() == 1) {
                    // popups close through an exit animation that only completes when frames render; offscreen renders
                    // none until grabWindow() — so pump events with time passing, then grab (a closed dialog stays closed)
                    QElapsedTimer t; t.start(); while (t.elapsed() < 350) { QCoreApplication::processEvents(QEventLoop::AllEvents, 50); QThread::msleep(10); }
                    win->grabWindow(); QCoreApplication::processEvents();
                    ret = win->grabWindow().save(a[0]) ? QString() : QStringLiteral("<save failed>");
                }
                else if (op == QLatin1String("trim") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureTrim", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                else if (op == QLatin1String("group") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureGroup", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                else if (op == QLatin1String("firstrun") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureFirstRun", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                else if (op == QLatin1String("focus") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureFocus", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                else if (op == QLatin1String("hide") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureHide", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                else if (op == QLatin1String("duplicate") && a.size() == 2) QMetaObject::invokeMethod(win, "gestureDuplicate", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]));
                else if (op == QLatin1String("export") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureExport", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                else if (op == QLatin1String("import") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureImport", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                else if (op == QLatin1String("traymenu") && a.size() == 1) { for (const QString &t : kde.trayMenuTexts()) fprintf(stdout, "traymenu %s\n", qPrintable(t)); ret = QString(); }
                else if (op == QLatin1String("trayclick") && a.size() == 1) {   // UX-17: N clicks on the tray icon within the double-click interval
                    const int n = a[0].toInt(); for (int i = 0; i < n; ++i) kde.trayClick(QPoint(100, 100)); ret = QString();
                }
                else if (op == QLatin1String("wirepopup") && a.size() == 3) QMetaObject::invokeMethod(win, "gestureWirePopup", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2]));
                // Argumentlose Geste: rest ist "", und "".split('|') ergibt EINE leere Zeichenkette,
                // nie size()==0 — darum auf leeres erstes Element pruefen, nicht auf leere Liste.
                else if (op == QLatin1String("fxaddopen") && a.value(0).isEmpty()) {
                    // Das Panel liegt in einem eigenen Fenster: dessen contentItem als Suchwurzel
                    // mitgeben, sonst sucht die Geste im Hauptfenster und findet nichts.
                    QQuickItem *wurzel = nullptr;
                    for (QWindow *w : QGuiApplication::topLevelWindows()) {
                        auto *qw = qobject_cast<QQuickWindow *>(w);
                        if (qw && qw != win && qw->isVisible()) { wurzel = qw->contentItem(); break; }
                    }
                    QMetaObject::invokeMethod(win, "gestureFxAddOpen", Q_RETURN_ARG(QVariant, ret),
                                              Q_ARG(QVariant, QVariant::fromValue(wurzel)));
                }
                else if (op == QLatin1String("monitors") && a.size() == 1) QMetaObject::invokeMethod(win, "gestureMonitors", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, a[0]));
                else { fprintf(stderr, "kmixdeck: --gesture: unknown or wrong-arity '%s'\n", qPrintable(g)); ret = QStringLiteral("<unknown gesture>"); }
                // ret leer = "ok" ist nur richtig, wenn die Geste WIRKLICH lief. Vorher fiel ein
                // unbekannter Name (oder eine falsche Argumentzahl) durch alle Zweige, ret blieb
                // leer, und die Ausgabe sagte "ok" fuer eine Geste, die es nicht gibt. Ich habe
                // darauf eine halbe Stunde Fehlersuche verschwendet: die Geste meldete Erfolg,
                // die Probes fanden nichts, und ich habe den Fehler in der QML gesucht.
                fprintf(stdout, "gesture %s -> %s\n", qPrintable(g), qPrintable(ret.toString().isEmpty() ? QStringLiteral("ok") : ret.toString()));
            }
            fflush(stdout);
        });
        // Leave only after the bus has ROUND-TRIPPED: every gesture ends in asyncCall()s, and under load the 1 s that
        // used to remain before exit was not enough — the calls died with the process (ux14 red in the suite, green
        // alone). A blocking Ping after the gestures is ordered behind our own calls on the same connection.
        if (!parser.isSet(probeArg)) QTimer::singleShot(1500, &app, [] {
            QDBusInterface(QStringLiteral("org.kmixdeck1"), QStringLiteral("/org/kmixdeck1"), QStringLiteral("org.freedesktop.DBus.Peer"), QDBusConnection::sessionBus()).call(QStringLiteral("Ping"));
            QCoreApplication::exit(0);
        });
    }
    if (parser.isSet(probeArg)) {
        if (engine.rootObjects().isEmpty()) return 1;
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QString open = parser.value(openArg); const QStringList probes = parser.values(probeArg);
        { const QStringList wh = parser.value(sizeArg).split(QLatin1Char('x')); win->resize(wh.size() == 2 ? wh[0].toInt() : 1280, wh.size() == 2 ? wh[1].toInt() : 760); }
        win->show();
        QTimer::singleShot(900, &app, [win, open, openZiel] { openZiel(win, open); });
        QTimer::singleShot(2000, &app, [win, probes] {
            // Alle Top-Level-Fenster fragen, nicht nur das Hauptfenster: pushDialogLayer oeffnet
            // auf dem Desktop ein eigenes QQuickWindow (Kirigami PageRow.qml, Zweig "open as a new
            // window"). Ein Probe auf ein Element im Effekt-Panel fand darum nie etwas, obwohl das
            // Panel offen war — und das sah nach einem QML-Fehler aus, wo keiner war.
            for (const QString &p : probes) {
                QVariant ret;
                // invokeMethod-Rueckgabe PRUEFEN. Nach dem Umbau auf probe(spec, startItem) war
                // die QML-Funktion zweiargumentig, der Aufruf mit einem Argument schlug fehl, ret
                // blieb LEER — und weil mein Fallback nur bei "<not found" ansprang, lief er nie.
                // Jeder Probe gab einen Leerstring zurueck, was nach "Property ist leer" aussah
                // statt nach "Aufruf ist gescheitert". Zweite Runde derselben Lektion an einem Tag.
                if (!QMetaObject::invokeMethod(win, "probe", Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, p), Q_ARG(QVariant, QVariant())))
                    ret = QStringLiteral("<probe() not invokable — signature changed?>");
                if (ret.toString().startsWith(QLatin1String("<not found"))) {
                    for (QWindow *w : QGuiApplication::topLevelWindows()) {
                        auto *qw = qobject_cast<QQuickWindow *>(w);
                        if (!qw || qw == win) continue;
                        QVariant r2;
                        // Das Dialog-Fenster ist ein nacktes QQuickWindow ohne probe()-Methode:
                        // ueber contentItem suchen lassen, indem das Hauptfenster den Baum bekommt.
                        if (!QMetaObject::invokeMethod(win, "probeIn", Q_RETURN_ARG(QVariant, r2),
                                                       Q_ARG(QVariant, QVariant::fromValue(qw->contentItem())), Q_ARG(QVariant, p)))
                            continue;
                        if (!r2.toString().startsWith(QLatin1String("<not found"))) { ret = r2; break; }
                    }
                }
                fprintf(stdout, "probe %s = %s\n", qPrintable(p), qPrintable(ret.toString()));
            }
            fflush(stdout);
            // gestures (if any) ended in asyncCall()s — round-trip the bus before leaving, same as the gesture-only path
            QDBusInterface(QStringLiteral("org.kmixdeck1"), QStringLiteral("/org/kmixdeck1"), QStringLiteral("org.freedesktop.DBus.Peer"), QDBusConnection::sessionBus()).call(QStringLiteral("Ping"));
            QCoreApplication::exit(0);
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
            else if (open == QLatin1String("tray")) {   // UX-17: render the tray popover itself
                QMetaObject::invokeMethod(win, "showTrayOverview", Q_ARG(QVariant, 400), Q_ARG(QVariant, 700));
                QTimer::singleShot(900, win, [win, file] {
                    QVariant v = win->property("trayOverview"); auto *pop = v.value<QQuickWindow *>();
                    const bool ok = pop && pop->grabWindow().save(file);
                    qCInfo(lcFrontend, "%s %s", ok ? "screenshot written:" : "screenshot FAILED:", qPrintable(file));
                    QCoreApplication::exit(ok ? 0 : 1);
                });
                return;
            }
            else if (!open.isEmpty()) QMetaObject::invokeMethod(win, "addDialogOpen", Q_ARG(QVariant, open));
            QTimer::singleShot(900, win, [win, file] {
                const bool ok = win->grabWindow().save(file);
                qCInfo(lcFrontend, "%s %s", ok ? "screenshot written:" : "screenshot FAILED:", qPrintable(file));
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
