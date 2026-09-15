// SPDX-License-Identifier: GPL-3.0-or-later
// kmixdeck — CLI. A pure D-Bus client of org.kmixdeck1; proves AR-3 (everything reachable from a shell).
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QMap>
#include <cmath>
#include <cstdio>
#include <QDBusMessage>

using InterfaceMap = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceMap>;
using StringMap = QMap<QString, QString>;
Q_DECLARE_METATYPE(StringMap)
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

namespace {
constexpr const char *BUS = "org.kmixdeck1";
constexpr const char *ROOT = "/org/kmixdeck1";
enum Exit { Ok = 0, Usage = 1, NoService = 2, NotFound = 3, Rejected = 4 };
QTextStream out(stdout), err(stderr);
bool g_json = false;

QVariant unwrap(const QVariant &v) {
    if (v.canConvert<QDBusVariant>()) return unwrap(v.value<QDBusVariant>().variant());
    if (v.canConvert<QDBusObjectPath>() && v.userType() == qMetaTypeId<QDBusObjectPath>()) return v.value<QDBusObjectPath>().path();
    return v;
}
QVariantMap plain(const QVariantMap &m) { QVariantMap r; for (auto it = m.cbegin(); it != m.cend(); ++it) r[it.key()] = unwrap(it.value()); return r; }

struct Objects { QMap<QString, QVariantMap> channels, mixes, cells, apps; QVariantMap mixer; };

int fail(Exit code, const QString &msg) { if (g_json) out << QJsonDocument(QJsonObject{{"error", msg}, {"code", int(code)}}).toJson(QJsonDocument::Compact) << "\n"; else err << "kmixdeck: " << msg << "\n"; return code; }

bool fetch(Objects &o, QString *error) {
    QDBusInterface om(BUS, ROOT, "org.freedesktop.DBus.ObjectManager", QDBusConnection::sessionBus());
    QDBusReply<ManagedObjects> r = om.call("GetManagedObjects");
    if (!r.isValid()) { *error = r.error().message(); return false; }
    for (auto it = r.value().cbegin(); it != r.value().cend(); ++it) {
        const QString p = it.key().path();
        for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
            const QVariantMap props = plain(jt.value());
            if (jt.key() == "org.kmixdeck1.Mixer") o.mixer = props;
            else if (jt.key() == "org.kmixdeck1.Channel") o.channels[p] = props;
            else if (jt.key() == "org.kmixdeck1.Mix") o.mixes[p] = props;
            else if (jt.key() == "org.kmixdeck1.Cell") o.cells[p] = props;
            else if (jt.key() == "org.kmixdeck1.App") o.apps[p] = props;
        }
    }
    return true;
}

QString db(double lin) { return lin <= 0 ? QStringLiteral("  -inf") : QString::number(20.0 * std::log10(lin), 'f', 1).rightJustified(6); }
QString cellPath(const QString &ch, const QString &mix) { return QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix); }

bool setProp(const QString &path, const QString &iface, const QString &name, const QVariant &v, QString *error) {
    QDBusInterface props(BUS, path, "org.freedesktop.DBus.Properties", QDBusConnection::sessionBus());
    QDBusMessage r = props.call("Set", iface, name, QVariant::fromValue(QDBusVariant(v)));
    if (r.type() == QDBusMessage::ErrorMessage) { *error = r.errorMessage(); return false; }
    return true;
}
// "0.25" (linear) | "-12dB" | "50%" (cubic, like the UI) → linear
bool parseLevel(const QString &s, double *lin) {
    bool ok = false;
    if (s.endsWith("dB", Qt::CaseInsensitive)) { double d = s.chopped(2).toDouble(&ok); if (ok) *lin = d > 0 ? 1.0 : std::pow(10.0, d / 20.0); }
    else if (s.endsWith('%')) { double p = s.chopped(1).toDouble(&ok) / 100.0; if (ok) *lin = std::clamp(p * p * p, 0.0, 1.0); }
    else { *lin = s.toDouble(&ok); }
    return ok && *lin >= 0.0 && *lin <= 1.0;
}
bool parseBool(const QStringList &a, int i, bool *b) { if (a.size() <= i) { *b = true; return true; } const QString s = a[i].toLower(); if (s == "on" || s == "1" || s == "true") *b = true; else if (s == "off" || s == "0" || s == "false") *b = false; else return false; return true; }

int cmdStatus(const Objects &o) {
    if (g_json) {
        QJsonObject j{{"version", o.mixer.value("Version").toString()}, {"connected", o.mixer.value("Connected").toBool()}};
        QJsonArray ch, mx, cells;
        for (auto it = o.channels.cbegin(); it != o.channels.cend(); ++it) ch.append(QJsonObject::fromVariantMap(it.value()));
        for (auto it = o.mixes.cbegin(); it != o.mixes.cend(); ++it) mx.append(QJsonObject::fromVariantMap(it.value()));
        for (auto it = o.cells.cbegin(); it != o.cells.cend(); ++it) { auto m = it.value(); m["Path"] = it.key(); cells.append(QJsonObject::fromVariantMap(m)); }
        j["channels"] = ch; j["mixes"] = mx; j["cells"] = cells;
        out << QJsonDocument(j).toJson(QJsonDocument::Indented); return Ok;
    }
    out << "kmixdeckd " << o.mixer.value("Version").toString() << (o.mixer.value("Connected").toBool() ? "  PipeWire: connected\n" : "  PipeWire: NOT CONNECTED\n");
    if (o.channels.isEmpty() || o.mixes.isEmpty()) { out << "(no channels or mixes)\n"; return Ok; }
    out << QStringLiteral("%1").arg("", -14);
    for (const auto &m : o.mixes) out << QStringLiteral("%1").arg(m.value("Name").toString().left(12), -14);
    out << "\n";
    for (auto c = o.channels.cbegin(); c != o.channels.cend(); ++c) {
        const QString cs = c.value().value("Slug").toString();
        out << QStringLiteral("%1").arg(c.value().value("Name").toString().left(12), -14);
        for (auto m = o.mixes.cbegin(); m != o.mixes.cend(); ++m) {
            const auto cell = o.cells.value(cellPath(cs, m.value().value("Slug").toString()));
            if (cell.isEmpty()) out << QStringLiteral("%1").arg("—", -14);
            else out << QStringLiteral("%1 %2").arg(cell.value("Muted").toBool() ? QStringLiteral(" muted") : db(cell.value("Volume").toDouble()), -7).arg("dB", -6);
        }
        out << "\n";
    }
    return Ok;
}
} // namespace

class Watcher : public QObject {
    Q_OBJECT
public Q_SLOTS:
    // Slot with a QDBusMessage parameter receives the raw message → we get the object path.
    void propertiesChanged(const QDBusMessage &msg) {
        const QString path = msg.path(); const auto args = msg.arguments();
        const QString iface = args.value(0).toString(); const QVariantMap changed = qdbus_cast<QVariantMap>(args.value(1));
        if (g_json) { QJsonObject j{{"event", "changed"}, {"path", path}, {"interface", iface}, {"properties", QJsonObject::fromVariantMap(plain(changed))}}; out << QJsonDocument(j).toJson(QJsonDocument::Compact) << "\n"; }
        else { out << path.mid(QString(ROOT).size() + 1) << ":"; const auto pl = plain(changed); for (auto it = pl.cbegin(); it != pl.cend(); ++it) out << " " << it.key() << "=" << it.value().toString(); out << "\n"; }
        out.flush();
    }
    void peaks(const QVariantMap &p) {
        if (g_json) { out << QJsonDocument(QJsonObject::fromVariantMap(p)).toJson(QJsonDocument::Compact) << "\n"; out.flush(); return; }
        QStringList keys = p.keys(); keys.sort();
        QString line;
        for (const auto &k : keys) {
            const double v = p.value(k).toDouble(); const double db = v > 0 ? 20 * std::log10(v) : -90;
            const int bar = std::clamp(static_cast<int>((db + 60) / 60 * 20), 0, 20);   // −60…0 dB → 0…20 chars
            line += QStringLiteral("%1 [%2%3] %4  ").arg(k, -16).arg(QString(bar, QLatin1Char('#'))).arg(QString(20 - bar, QLatin1Char(' '))).arg(v > 0 ? QStringLiteral("%1 dB").arg(db, 6, 'f', 1) : QStringLiteral("   -inf"));
        }
        out << "\r" << line; out.flush();
    }
    void interfacesAdded(const QDBusObjectPath &path, const InterfaceMap &ifaces) {
        if (g_json) out << QJsonDocument(QJsonObject{{"event", "added"}, {"path", path.path()}, {"interfaces", QJsonArray::fromStringList(ifaces.keys())}}).toJson(QJsonDocument::Compact) << "\n";
        else out << "+ " << path.path() << "\n";
        out.flush();
    }
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &) {
        if (g_json) out << QJsonDocument(QJsonObject{{"event", "removed"}, {"path", path.path()}}).toJson(QJsonDocument::Compact) << "\n";
        else out << "- " << path.path() << "\n";
        out.flush();
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<StringMap>(); qDBusRegisterMetaType<InterfaceMap>(); qDBusRegisterMetaType<ManagedObjects>();
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral(
        "kmixdeck — control the kmixdeck service (org.kmixdeck1) from the shell.\n\n"
        "Commands:\n"
        "  status                                     matrix overview\n"
        "  channel list|add <name>|remove <slug>|rename <slug> <name>|trim <slug> <level>|mute <slug> [on|off]|input <slug> <node.name|none>\n"
        "  mix     list|add <name>|remove <slug>|rename <slug> <name>|output <slug> <node.name|none>\n"
        "  devices [in]                               hardware outputs a mix can play to (in: sources a channel can be fed by)\n"
        "  levels                                     live peak meters (Ctrl-C to stop)\n"
        "  cell    get <ch> <mix>|set <ch> <mix> <level>|mute <ch> <mix> [on|off]\n"
        "  app     list|move <id|name> <channel>          running application streams\n"
        "  watch                                      print property changes as they happen\n\n"
        "Levels: linear 0..1, or NdB (e.g. -12dB), or N% (UI/cubic scale). Exit codes: 0 ok, 1 usage, 2 no service, 3 not found, 4 rejected."));
    p.addHelpOption(); p.addVersionOption();
    QCommandLineOption json({"j", "json"}, "machine-readable output"); p.addOption(json);
    p.addPositionalArgument("command", "see above");
    p.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);   // so "-12dB" is a value, not options
    p.process(app);
    g_json = p.isSet(json);
    QStringList a = p.positionalArguments();
    if (a.isEmpty()) { p.showHelp(Usage); }
    if (!QDBusConnection::sessionBus().isConnected()) return fail(NoService, "no session bus");

    Objects o; QString e;
    if (!fetch(o, &e)) return fail(NoService, "service not reachable: " + e);
    const QString cmd = a[0], sub = a.value(1);
    auto need = [&](int n) { if (a.size() < n) { fail(Usage, "missing arguments; see --help"); return false; } return true; };
    auto printPath = [&](const QDBusReply<QDBusObjectPath> &r) -> int {
        if (!r.isValid()) return fail(Rejected, r.error().message());
        if (g_json) out << QJsonDocument(QJsonObject{{"path", r.value().path()}}).toJson(QJsonDocument::Compact); else out << r.value().path() << "\n"; return Ok; };
    auto list = [&](const QMap<QString, QVariantMap> &m) {
        if (g_json) { QJsonArray arr; for (auto it = m.cbegin(); it != m.cend(); ++it) { auto v = it.value(); v["Path"] = it.key(); arr.append(QJsonObject::fromVariantMap(v)); } out << QJsonDocument(arr).toJson(); }
        else for (auto it = m.cbegin(); it != m.cend(); ++it) out << QStringLiteral("%1  %2\n").arg(it.value().value("Slug").toString(), -16).arg(it.value().value("Name").toString());
        return Ok; };
    QDBusInterface mixer(BUS, ROOT, "org.kmixdeck1.Mixer", QDBusConnection::sessionBus());

    if (cmd == "status") return cmdStatus(o);
    if (cmd == "devices") {
        const StringMap devs = qdbus_cast<StringMap>(o.mixer.value(sub == "in" ? "InputDevices" : "OutputDevices"));
        if (g_json) { QJsonObject j; for (auto it = devs.cbegin(); it != devs.cend(); ++it) j[it.key()] = it.value(); out << QJsonDocument(j).toJson(); return Ok; }
        for (auto it = devs.cbegin(); it != devs.cend(); ++it) out << QStringLiteral("%1  %2\n").arg(it.key(), -48).arg(it.value());
        return Ok;
    }
    if (cmd == "channel" || cmd == "mix") {
        const bool ch = cmd == "channel"; const auto &objs = ch ? o.channels : o.mixes; const QString iface = ch ? "org.kmixdeck1.Channel" : "org.kmixdeck1.Mix";
        auto pathOf = [&](const QString &slug) { return QStringLiteral("%1/%2/%3").arg(ROOT, ch ? "channel" : "mix", slug); };
        if (sub == "list") return list(objs);
        if (sub == "add") { if (!need(3)) return Usage; return printPath(QDBusReply<QDBusObjectPath>(mixer.call(ch ? "AddChannel" : "AddMix", a[2]))); }
        if (!need(3)) return Usage;
        if (!objs.contains(pathOf(a[2]))) return fail(NotFound, QStringLiteral("no %1 '%2'").arg(cmd, a[2]));
        if (sub == "remove") { QDBusMessage r = mixer.call(ch ? "RemoveChannel" : "RemoveMix", QVariant::fromValue(QDBusObjectPath(pathOf(a[2])))); return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok; }
        if (sub == "rename") { if (!need(4)) return Usage; return setProp(pathOf(a[2]), iface, "Name", a[3], &e) ? Ok : fail(Rejected, e); }
        if (sub == "trim" && ch) { if (!need(4)) return Usage; double l; if (!parseLevel(a[3], &l)) return fail(Usage, "bad level"); return setProp(pathOf(a[2]), iface, "Trim", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "mute" && ch) { bool b; if (!parseBool(a, 3, &b)) return fail(Usage, "on|off"); return setProp(pathOf(a[2]), iface, "Muted", b, &e) ? Ok : fail(Rejected, e); }
        if (sub == "input" && ch) {   // channel input <slug> <node.name|none>
            if (!need(4)) return Usage;
            const QString dev = a[3] == "none" ? QString() : a[3];
            const StringMap devs = qdbus_cast<StringMap>(o.mixer.value("InputDevices"));
            if (!dev.isEmpty() && !devs.contains(dev)) return fail(NotFound, "no input device '" + dev + "' (see `kmixdeck devices in`)");
            return setProp(pathOf(a[2]), iface, "InputDevice", dev, &e) ? Ok : fail(Rejected, e);
        }
        if (sub == "output" && !ch) {   // mix output <slug> <node.name|none>
            if (!need(4)) return Usage;
            const QString dev = a[3] == "none" ? QString() : a[3];
            const StringMap devs = qdbus_cast<StringMap>(o.mixer.value("OutputDevices"));
            if (!dev.isEmpty() && !devs.contains(dev)) return fail(NotFound, "no output device '" + dev + "' (see `kmixdeck devices`)");
            return setProp(pathOf(a[2]), iface, "OutputDevice", dev, &e) ? Ok : fail(Rejected, e);
        }
        return fail(Usage, "unknown subcommand '" + sub + "'");
    }
    if (cmd == "cell") {
        if (!need(4)) return Usage;
        const QString path = cellPath(a[2], a[3]);
        if (!o.cells.contains(path)) return fail(NotFound, QStringLiteral("no cell %1×%2").arg(a[2], a[3]));
        if (sub == "get") { const auto c = o.cells[path]; if (g_json) out << QJsonDocument(QJsonObject::fromVariantMap(c)).toJson(); else out << (c.value("Muted").toBool() ? "muted " : "") << db(c.value("Volume").toDouble()).trimmed() << " dB  (linear " << c.value("Volume").toDouble() << ")\n"; return Ok; }
        if (sub == "set") { if (!need(5)) return Usage; double l; if (!parseLevel(a[4], &l)) return fail(Usage, "bad level '" + a[4] + "'"); return setProp(path, "org.kmixdeck1.Cell", "Volume", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "mute") { bool b; if (!parseBool(a, 4, &b)) return fail(Usage, "on|off"); return setProp(path, "org.kmixdeck1.Cell", "Muted", b, &e) ? Ok : fail(Rejected, e); }
        return fail(Usage, "unknown subcommand '" + sub + "'");
    }
    if (cmd == "app") {
        if (sub == "list") {
            if (g_json) { QJsonArray arr; for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it) { auto v = it.value(); v["Path"] = it.key(); arr.append(QJsonObject::fromVariantMap(v)); } out << QJsonDocument(arr).toJson(); }
            else for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it) {
                const QString chPath = it.value().value("Channel").toString();
                out << QStringLiteral("%1  %2  %3  -> %4\n").arg(it.value().value("NodeId").toString(), 5).arg(it.value().value("Name").toString().left(24), -24)
                       .arg(it.value().value("MediaName").toString().left(20), -20).arg(chPath == "/" ? QStringLiteral("(not on a channel)") : chPath.section('/', -1));
            }
            return Ok;
        }
        if (sub == "move") {
            if (!need(4)) return Usage;
            QString appPath;
            for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it)
                if (it.value().value("NodeId").toString() == a[2] || it.value().value("Name").toString().compare(a[2], Qt::CaseInsensitive) == 0) appPath = it.key();
            if (appPath.isEmpty()) return fail(NotFound, "no app '" + a[2] + "'");
            const QString chPath = QStringLiteral("%1/channel/%2").arg(ROOT, a[3]);
            if (!o.channels.contains(chPath)) return fail(NotFound, "no channel '" + a[3] + "'");
            QDBusInterface appIf(BUS, appPath, "org.kmixdeck1.App", QDBusConnection::sessionBus());
            QDBusMessage r = appIf.call("MoveTo", QVariant::fromValue(QDBusObjectPath(chPath)));
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        return fail(Usage, "app list | app move <id|name> <channel>");
    }
    if (cmd == "levels") {   // live peaks, 25 Hz; Ctrl-C to stop. --json: one object per tick.
        QDBusInterface lv(BUS, ROOT, "org.kmixdeck1.Levels", QDBusConnection::sessionBus());
        QDBusReply<void> sub = lv.call("Subscribe");
        if (!sub.isValid()) return fail(NoService, sub.error().message());
        auto *w = new Watcher; w->setParent(&app);
        QDBusConnection::sessionBus().connect(BUS, ROOT, "org.kmixdeck1.Levels", "Peaks", w, SLOT(peaks(QVariantMap)));
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&lv] { lv.call("Unsubscribe"); });
        return app.exec();
    }
    if (cmd == "watch") {
        Watcher w; auto bus = QDBusConnection::sessionBus();
        bus.connect(BUS, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged", &w, SLOT(propertiesChanged(QDBusMessage)));
        bus.connect(BUS, ROOT, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", &w, SLOT(interfacesAdded(QDBusObjectPath,InterfaceMap)));
        bus.connect(BUS, ROOT, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", &w, SLOT(interfacesRemoved(QDBusObjectPath,QStringList)));
        return app.exec();
    }
    return fail(Usage, "unknown command '" + cmd + "'; see --help");
}

#include "main.moc"
