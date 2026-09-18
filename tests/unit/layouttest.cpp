// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The persistence format (layout.json) and the generated PipeWire config are the two "no audio after reboot" surfaces.
// Both are pinned here in milliseconds, without PipeWire: a full round trip of every field, tolerance for partial and
// legacy documents, the `node:POS,POS>L` grammar, and a golden text of the conf for the starter layout.
// (Review 2026-09-18, UT-1 / UT-2.)
#include <QtTest>
#include <QJsonDocument>
#include "layout.h"

using namespace kmixdeck;

class LayoutTest : public QObject {
    Q_OBJECT
    static Layout full() {
        Layout l = Layout::starter();
        auto &game = l.channels[0]; game.color = QStringLiteral("#ff8800"); game.group = QStringLiteral("Media"); game.pan = -0.5;
        fx::Chain fxc; fx::Effect e; e.type = QStringLiteral("highpass"); e.params = {{QStringLiteral("freq"), 120.0}}; e.enabled = false; fxc.effects.push_back(e);
        game.fx = fxc;
        auto &stream = l.mixes[1]; stream.color = QStringLiteral("#00aaff");
        stream.outputs = {DeviceRef{QStringLiteral("alsa_output.usb-X"), QStringLiteral("USB X"), {QStringLiteral("AUX3"), QStringLiteral("AUX4")}, {}},
                          DeviceRef{QStringLiteral("alsa_output.pci-Y"), QStringLiteral("PCI Y"), {}, QStringLiteral("R")}};
        stream.outputs[0].trim = 0.5; stream.outputs[1].muted = true;
        stream.fallbackOutput = DeviceRef{QStringLiteral("alsa_output.fallback"), QStringLiteral("Fallback"), {}, {}};
        stream.fx = fxc;
        l.inputs.push_back({QStringLiteral("mic"), QStringLiteral("Mic"), DeviceRef{QStringLiteral("alsa_input.usb-M"), QStringLiteral("M"), {QStringLiteral("AUX1")}, QStringLiteral("L")}, QStringLiteral("voice")});
        l.links.push_back({QStringLiteral("game"), QStringLiteral("monitor"), QStringLiteral("stream")});
        l.apps.push_back({QStringLiteral("Firefox"), QStringLiteral("firefox"), {QStringLiteral("game"), QStringLiteral("system")}});
        l.knownApps = {QStringLiteral("Firefox")};
        l.hiddenDevices = {QStringLiteral("alsa_output.hdmi")};
        l.listeningDevice = QStringLiteral("alsa_output.usb-X");
        l.defaultChannel = QStringLiteral("system");
        LayoutVirtualDevice vd; vd.slug = QStringLiteral("desk"); vd.name = QStringLiteral("Desk"); vd.inputs = 4; vd.outputs = 2; l.virtualDevices.push_back(vd);
        return l;
    }

private Q_SLOTS:
    void roundTripKeepsEveryField() {
        const Layout a = full();
        const Layout b = Layout::fromJson(QJsonDocument::fromJson(QJsonDocument(a.toJson()).toJson()).object());
        QCOMPARE(QJsonDocument(b.toJson()).toJson(), QJsonDocument(a.toJson()).toJson());   // idempotent: json(from(json(a))) == json(a)
        QCOMPARE(b.channels.size(), 3); QCOMPARE(b.channels[0].color, QStringLiteral("#ff8800")); QCOMPARE(b.channels[0].group, QStringLiteral("Media"));
        QCOMPARE(b.channels[0].pan, -0.5); QCOMPARE(b.channels[0].fx.effects.size(), 1); QVERIFY(!b.channels[0].fx.effects[0].enabled); QCOMPARE(b.channels[0].fx.effects[0].params.value(QStringLiteral("freq")), 120.0);
        QCOMPARE(b.mixes[1].outputs.size(), 2); QCOMPARE(b.mixes[1].outputs[0].positions, QStringList({QStringLiteral("AUX3"), QStringLiteral("AUX4")}));
        QCOMPARE(b.mixes[1].outputs[1].side, QStringLiteral("R")); QVERIFY(b.mixes[1].outputs[1].muted); QCOMPARE(b.mixes[1].outputs[0].trim, 0.5);
        QCOMPARE(b.mixes[1].fallbackOutput.node, QStringLiteral("alsa_output.fallback"));
        QCOMPARE(b.inputs.size(), 1); QCOMPARE(b.inputs[0].device.side, QStringLiteral("L")); QCOMPARE(b.inputs[0].channel, QStringLiteral("voice"));
        QCOMPARE(b.links.size(), 1); QCOMPARE(b.links[0].follows, QStringLiteral("stream"));
        QCOMPARE(b.apps.size(), 1); QCOMPARE(b.apps[0].channels, QStringList({QStringLiteral("game"), QStringLiteral("system")}));
        QCOMPARE(b.knownApps, QStringList({QStringLiteral("Firefox")})); QCOMPARE(b.hiddenDevices, QStringList({QStringLiteral("alsa_output.hdmi")}));
        QCOMPARE(b.listeningDevice, QStringLiteral("alsa_output.usb-X")); QCOMPARE(b.defaultChannel, QStringLiteral("system"));
        QCOMPARE(b.virtualDevices.size(), 1); QCOMPARE(b.virtualDevices[0].inputs, 4); QCOMPARE(b.virtualDevices[0].outputs, 2);
    }
    void versionIsWritten() { QCOMPARE(Layout::starter().toJson().value(QStringLiteral("version")).toInt(), kLayoutVersion); }

    void partialDocumentGetsDefaults() {
        // CT-7 promise: a hand-edited file with only what the user cares about still loads
        const auto doc = QJsonDocument::fromJson(R"({"version":2,"channels":[{"slug":"a","name":"A"}],"mixes":[{"slug":"m","name":"M"}]})").object();
        const Layout l = Layout::fromJson(doc);
        QCOMPARE(l.channels.size(), 1); QCOMPARE(l.channels[0].icon, QString()); QCOMPARE(l.channels[0].pan, 0.0); QVERIFY(l.channels[0].fx.effects.isEmpty()); QVERIFY(l.channels[0].fx.enabled);
        QCOMPARE(l.mixes.size(), 1); QVERIFY(l.mixes[0].outputs.isEmpty()); QVERIFY(l.mixes[0].fallbackOutput.node.isEmpty());
        QVERIFY(l.inputs.isEmpty()); QVERIFY(l.links.isEmpty()); QVERIFY(l.apps.isEmpty()); QVERIFY(l.listeningDevice.isEmpty());
    }
    void legacyV1OutputDeviceIsRead() {
        const auto doc = QJsonDocument::fromJson(R"({"version":1,"mixes":[{"slug":"m","name":"M","outputDevice":"alsa_output.old"}]})").object();
        const Layout l = Layout::fromJson(doc);
        QCOMPARE(l.mixes[0].outputs.size(), 1); QCOMPARE(l.mixes[0].outputs[0].node, QStringLiteral("alsa_output.old"));
        QCOMPARE(l.toJson().value(QStringLiteral("version")).toInt(), kLayoutVersion);   // saved back as current
    }
    void newerVersionIsMovedAsideNotDowngraded() {
        QTemporaryDir dir; const QString path = dir.filePath(QStringLiteral("layout.json"));
        { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(R"({"version":99,"fromTheFuture":true,"channels":[]})"); }
        Layout l; QVERIFY(!l.load(path));
        QVERIFY(!QFile::exists(path));
        QVERIFY(QFile::exists(path + QStringLiteral(".v99-from-newer-kmixdeck")));
    }
    void corruptFileIsRefused() {
        QTemporaryDir dir; const QString path = dir.filePath(QStringLiteral("layout.json"));
        { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("{ not json"); }
        Layout l = Layout::starter(); QVERIFY(!l.load(path)); QCOMPARE(l.channels.size(), 3);   // untouched
        QVERIFY(QFile::exists(path));                                                             // and the file is kept
    }
    void saveIsAtomicAndLoadsBack() {
        QTemporaryDir dir; const QString path = dir.filePath(QStringLiteral("sub/layout.json"));
        QVERIFY(full().save(path));
        Layout l; QVERIFY(l.load(path)); QCOMPARE(l.channels[0].group, QStringLiteral("Media"));
        QVERIFY(!QFile::exists(path + QStringLiteral(".tmp")));   // QSaveFile leaves no temp behind
    }

    void deviceRefGrammar_data() {
        QTest::addColumn<QString>("ref"); QTest::addColumn<QString>("node"); QTest::addColumn<QStringList>("positions"); QTest::addColumn<QString>("side");
        QTest::newRow(("node only"))    << QStringLiteral("alsa_output.x") << QStringLiteral("alsa_output.x") << QStringList{} << QStringLiteral("");
        QTest::newRow(("one port"))     << QStringLiteral("dev:AUX3") << QStringLiteral("dev") << QStringList{QStringLiteral("AUX3")} << QStringLiteral("");
        QTest::newRow(("pair"))         << QStringLiteral("dev:AUX3,AUX4") << QStringLiteral("dev") << QStringList{QStringLiteral("AUX3"), QStringLiteral("AUX4")} << QStringLiteral("");
        QTest::newRow(("port + side"))  << QStringLiteral("dev:AUX5>R") << QStringLiteral("dev") << QStringList{QStringLiteral("AUX5")} << QStringLiteral("R");
        QTest::newRow(("side only"))    << QStringLiteral("dev>L") << QStringLiteral("dev") << QStringList{} << QStringLiteral("L");
        QTest::newRow(("spaces"))       << QStringLiteral(" dev : AUX1 , AUX2 ") << QStringLiteral("dev") << QStringList{QStringLiteral("AUX1"), QStringLiteral("AUX2")} << QStringLiteral("");
    }
    void deviceRefGrammar() {
        QFETCH(QString, ref); QFETCH(QString, node); QFETCH(QStringList, positions); QFETCH(QString, side);
        const DeviceRef d = DeviceRef::fromRef(ref);
        QCOMPARE(d.node, node); QCOMPARE(d.positions, positions); QCOMPARE(d.side, side);
        QCOMPARE(DeviceRef::fromRef(d.ref()).ref(), d.ref());   // print → parse → print is stable
    }
    void deviceRefRejectsBadSide() { QVERIFY(!DeviceRef::fromRef(QStringLiteral("dev>X")).sideValid()); QVERIFY(DeviceRef::fromRef(QStringLiteral("dev>L")).sideValid()); }

    void pipewireConfGolden() {
        // The conf PipeWire loads at login for the starter layout. If this changes, the change is deliberate: update the
        // golden file (tests/unit/golden/starter.conf) in the same commit and say why.
        const QString conf = Layout::starter().toPipewireConf();
        const QString goldenPath = QFINDTESTDATA("unit/golden/starter.conf");
        const QString actualPath = QDir::temp().filePath(QStringLiteral("kmixdeck-starter.conf.actual"));
        { QFile out(actualPath); if (out.open(QIODevice::WriteOnly)) out.write(conf.toUtf8()); }   // always: makes "update the golden" a cp
        QFile g(goldenPath);
        QVERIFY2(g.open(QIODevice::ReadOnly), qPrintable(QStringLiteral("golden file missing — review %1 and copy it to tests/unit/golden/starter.conf").arg(actualPath)));
        const QString want = QString::fromUtf8(g.readAll());
        if (conf != want) {
            const auto a = want.split(QLatin1Char('\n')), b = conf.split(QLatin1Char('\n'));
            for (int i = 0; i < qMax(a.size(), b.size()); ++i)
                if (a.value(i) != b.value(i)) { qWarning("first difference at line %d:\n  golden: %s\n  actual: %s", i + 1, qPrintable(a.value(i)), qPrintable(b.value(i))); break; }
        }
        QVERIFY2(conf == want, qPrintable(QStringLiteral("generated conf differs from the golden; actual at %1").arg(actualPath)));
    }
    void pipewireConfCoversEveryEntity() {
        const Layout l = full(); const QString conf = l.toPipewireConf();
        for (const auto &c : l.channels) QVERIFY2(conf.contains(QStringLiteral("kmixdeck.channel.") + c.slug), qPrintable(c.slug));
        for (const auto &m : l.mixes) { QVERIFY(conf.contains(QStringLiteral("kmixdeck.mix.") + m.slug)); QVERIFY(conf.contains(QStringLiteral("kmixdeck.source.") + m.slug)); }
        for (const auto &c : l.channels) for (const auto &m : l.mixes) QVERIFY(conf.contains(QStringLiteral("kmixdeck.link.%1.%2").arg(c.slug, m.slug)));
        QVERIFY(conf.contains(QStringLiteral("kmixdeck.virt.desk")));
    }
};
QTEST_GUILESS_MAIN(LayoutTest)
#include "layouttest.moc"
