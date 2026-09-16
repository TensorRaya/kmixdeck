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
using PortMap = QMap<QString, QStringList>;
Q_DECLARE_METATYPE(StringMap)
Q_DECLARE_METATYPE(PortMap)
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
// One rule for every syntax: nothing above unity is silently clamped — the bus refuses it, so do we.
// (Gain above 0 dB is a channel-trim/FX question, not a fader question; ADR 0002.)
bool parseLevel(const QString &s, double *lin) {
    bool ok = false;
    if (s.endsWith("dB", Qt::CaseInsensitive)) { double d = s.chopped(2).toDouble(&ok); if (ok) *lin = std::pow(10.0, d / 20.0); }
    else if (s.endsWith('%')) { double p = s.chopped(1).toDouble(&ok) / 100.0; if (ok) *lin = p * p * p; }
    else { *lin = s.toDouble(&ok); }
    return ok && *lin >= 0.0 && *lin <= 1.0 + 1e-9;
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
    qDBusRegisterMetaType<StringMap>(); qDBusRegisterMetaType<PortMap>(); qDBusRegisterMetaType<InterfaceMap>(); qDBusRegisterMetaType<ManagedObjects>();
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral(
        "kmixdeck — control the kmixdeck service (org.kmixdeck1) from the shell.\n\n"
        "Commands:\n"
        "  status                                     matrix overview\n"
        "  undo                                    restore the last removed channel or mix (CH-9)\n"
        "  channel list|add <name>|remove <slug>|rename <slug> <name>|icon <slug> <icon|none>|move <slug> <index|up|down|top|bottom>|trim <slug> <level>|mute <slug> [on|off]|input <slug> <node.name|none>\n"
        "  channel default [<slug>|none]         where never-seen applications land (CH-5)\n"
        "  mix     list|add <name>|remove <slug>|rename <slug> <name>|icon <slug> <icon|none>|move <slug> <index|up|down|top|bottom>|output <slug> <node.name|none>|volume <slug> <level>|mute <slug> [on|off]\n"
        "  mix     outputs <slug>                 list all hardware outputs of a mix (MX-9)\n"
        "  mix     output-add|output-remove <slug> <node.name>\n"
        "  mix     fallback <slug> <node.name|none>   played while every output is unplugged (DV-15)\n"
        "  devices [in]                               hardware outputs a mix can play to (in: sources a channel can be fed by)\n"
        "  levels                                     live peak meters (Ctrl-C to stop)\n"
        "  cell    get <ch> <mix>|set <ch> <mix> <level>|mute <ch> <mix> [on|off]\n"
        "  cell    link <ch> <mix> <other-mix|none>   MX-7: this cell follows the other mix's cell (volume+mute)\n"
        "  fx      types                                  built-in effect catalog (JSON)\n"
        "  fx      presets                                one-click chains (FX-4), editable starting points\n"
        "  fx      get|set|clear <channel|mix> <slug> ['<json>']   ordered insert chain on one object\n"
        "  fx      copy <channel|mix> <from> <kind> <to>          copy the chain to another object\n"
        "  fx      control <channel|mix> <slug> <node:Control> <value>   live tweak, no reload\n"
        "  app     list|move <id|name> <channel>          running application streams\n"
        "  app     assign <id|name> <ch>[,<ch>...]   CH-12: several channels at once (first = primary)\n"
        "  listen  [<node.name>|none]            UX-2: the device I listen on + which mixes play there\n"
        "  audition <channel|mix> <slug>|none    UX-12: solo one entity on the main output; none restores\n"
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
    if (cmd == "undo") {
        const QString what = unwrap(o.mixer.value("UndoDescription")).toString();
        if (what.isEmpty()) return fail(NotFound, "nothing to undo");
        const QDBusMessage r = mixer.call("Undo");
        if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
        out << "restored " << what << "\n"; return Ok;
    }
    // ADR 0009: "node[:POS,POS]" — the daemon logs a refusal but a D-Bus property Set cannot carry an error, so
    // the CLI checks the reference against Mixer.DevicePorts first and verifies the write by reading back.
    auto checkRef = [&](QString ref, bool source, QString *why) {
        int arrow = ref.lastIndexOf(QLatin1Char('>')); if (arrow < 0) arrow = ref.lastIndexOf(QLatin1Char('<'));   // ADR 0009 A2 side
        bool hasSide = false;
        if (arrow > 0) { const QString side = ref.mid(arrow + 1).toUpper(); if (side != "L" && side != "R") { *why = QStringLiteral("side must be L or R: node:POS>L"); return false; } ref = ref.left(arrow); hasSide = true; }
        if (hasSide && ref.count(QLatin1Char(',')) > 0) { *why = QStringLiteral("a side (>L / >R) takes exactly one port: node:POS>L"); return false; }
        const QString node = ref.section(QLatin1Char(':'), 0, 0);
        const StringMap devs = qdbus_cast<StringMap>(o.mixer.value(source ? "InputDevices" : "OutputDevices"));
        if (!devs.contains(node)) { *why = QStringLiteral("no %1 device '%2' (see `kmixdeck devices%3`)").arg(source ? "input" : "output", node, source ? " in" : ""); return false; }
        if (!ref.contains(QLatin1Char(':'))) return true;
        const QStringList want = ref.section(QLatin1Char(':'), 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (want.isEmpty() || want.size() > 2) { *why = QStringLiteral("a virtual device is mono (1 port) or stereo (2 ports): node:POS or node:POS,POS"); return false; }
        QStringList have; for (const auto &t : qdbus_cast<PortMap>(o.mixer.value("DevicePorts")).value(node)) have << t.section(QLatin1Char('|'), 0, 0);
        for (const auto &w : want) if (!have.contains(w)) { *why = QStringLiteral("device '%1' has no port '%2' (see `kmixdeck devices ports %1`)").arg(node, w); return false; }
        return true;
    };
    if (cmd == "devices") {
        if (sub == "virtual") {   // DV-23: devices virtual list | add <name> [--in N] [--out N] | remove <slug|node>
            const QString op = a.value(2);
            if (op == "list" || op.isEmpty()) { for (const auto &n : unwrap(o.mixer.value("VirtualDevices")).toStringList()) out << n << "\n"; return Ok; }
            if (op == "add") {
                if (!need(4)) return Usage;
                int in = 8, outN = 8;
                for (int i = 4; i + 1 < a.size(); i += 2) { if (a[i] == "--in") in = a[i + 1].toInt(); else if (a[i] == "--out") outN = a[i + 1].toInt(); else return fail(Usage, "devices virtual add <name> [--in N] [--out N]"); }
                const QDBusMessage r = mixer.call("AddVirtualDevice", a[3], in, outN);
                if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
                out << r.arguments().value(0).toString() << "\n"; return Ok;
            }
            if (op == "remove") { if (!need(4)) return Usage; const QDBusMessage r = mixer.call("RemoveVirtualDevice", a[3]); return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok; }
            return fail(Usage, "devices virtual list|add|remove");
        }
        if (sub == "ports") {   // devices ports <node> — ADR 0009 D4: what "node:POS,POS" may name, and who uses it
            if (!need(3)) return Usage;
            const PortMap pm = qdbus_cast<PortMap>(o.mixer.value("DevicePorts"));
            if (!pm.contains(a[2])) return fail(NotFound, "no device '" + a[2] + "' (see `kmixdeck devices` / `devices in`)");
            QHash<QString, QStringList> users;   // position → "channel voice" / "mix stream"
            for (auto it = o.channels.cbegin(); it != o.channels.cend(); ++it) {
                const QString ref = unwrap(it.value().value("InputDevice")).toString();
                if (ref.section(QLatin1Char(':'), 0, 0) != a[2]) continue;
                const QString posList = ref.section(QLatin1Char(':'), 1);
                for (const auto &p2 : posList.split(QLatin1Char(','), Qt::SkipEmptyParts)) users[p2] << "channel " + it.key().section(QLatin1Char('/'), -1);
                if (posList.isEmpty()) users[QStringLiteral("*")] << "channel " + it.key().section(QLatin1Char('/'), -1);
            }
            for (auto it = o.mixes.cbegin(); it != o.mixes.cend(); ++it)
                for (const auto &ref : unwrap(it.value().value("Outputs")).toStringList()) {
                    if (ref.section(QLatin1Char(':'), 0, 0) != a[2]) continue;
                    const QString posList = ref.section(QLatin1Char(':'), 1);
                    for (const auto &p2 : posList.split(QLatin1Char(','), Qt::SkipEmptyParts)) users[p2] << "mix " + it.key().section(QLatin1Char('/'), -1);
                    if (posList.isEmpty()) users[QStringLiteral("*")] << "mix " + it.key().section(QLatin1Char('/'), -1);
                }
            if (g_json) {
                QJsonArray arr;
                for (const auto &t : pm.value(a[2])) { const auto f = t.split(QLatin1Char('|')); arr.append(QJsonObject{{"position", f.value(0)}, {"port", f.value(1)}, {"alias", f.value(2)}, {"usedBy", QJsonArray::fromStringList(users.value(f.value(0)) + users.value(QStringLiteral("*")))}}); }
                out << QJsonDocument(arr).toJson(); return Ok;
            }
            for (const auto &t : pm.value(a[2])) {
                const auto f = t.split(QLatin1Char('|'));
                const QStringList u = users.value(f.value(0)) + users.value(QStringLiteral("*"));
                out << QStringLiteral("%1  %2%3\n").arg(f.value(0), -8).arg(f.value(1), -28).arg(u.isEmpty() ? QString() : QStringLiteral("in use by ") + u.join(QStringLiteral(", ")));
            }
            return Ok;
        }
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
        if (sub == "default" && ch) {   // channel default [<slug>|none]
            if (a.size() < 3) { const QString p = unwrap(o.mixer.value("DefaultChannel")).toString(); out << (p == "/" ? QStringLiteral("none") : p.section(QLatin1Char('/'), -1)) << "\n"; return Ok; }
            if (a[2] != "none" && !objs.contains(pathOf(a[2]))) return fail(NotFound, QStringLiteral("no channel '%1'").arg(a[2]));
            const QDBusObjectPath p(a[2] == "none" ? QStringLiteral("/") : pathOf(a[2]));
            return setProp(QString::fromLatin1(ROOT), "org.kmixdeck1.Mixer", "DefaultChannel", QVariant::fromValue(p), &e) ? Ok : fail(Rejected, e);
        }
        if (!need(3)) return Usage;
        if (!objs.contains(pathOf(a[2]))) return fail(NotFound, QStringLiteral("no %1 '%2'").arg(cmd, a[2]));
        if (sub == "remove") { QDBusMessage r = mixer.call(ch ? "RemoveChannel" : "RemoveMix", QVariant::fromValue(QDBusObjectPath(pathOf(a[2])))); return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok; }
        if (sub == "rename") { if (!need(4)) return Usage; return setProp(pathOf(a[2]), iface, "Name", a[3], &e) ? Ok : fail(Rejected, e); }
        if (sub == "icon") { if (!need(4)) return Usage; return setProp(pathOf(a[2]), iface, "Icon", a[3] == "none" ? QString() : a[3], &e) ? Ok : fail(Rejected, e); }   // UX-8
        if (sub == "move") {   // UX-9: channel|mix move <slug> <index|up|down|top|bottom>
            if (!need(4)) return Usage;
            const QStringList order = unwrap(o.mixer.value(ch ? "ChannelOrder" : "MixOrder")).toStringList();
            const int cur = order.indexOf(a[2]); if (cur < 0) return fail(NotFound, QStringLiteral("no %1 '%2'").arg(ch ? "channel" : "mix", a[2]));
            int idx; bool okNum;
            if (a[3] == "up") idx = cur - 1; else if (a[3] == "down") idx = cur + 1; else if (a[3] == "top") idx = 0; else if (a[3] == "bottom") idx = order.size() - 1;
            else { idx = a[3].toInt(&okNum); if (!okNum) return fail(Usage, "index|up|down|top|bottom"); }
            const QDBusMessage r = mixer.call(ch ? "MoveChannel" : "MoveMix", QVariant::fromValue(QDBusObjectPath(pathOf(a[2]))), idx);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "trim" && ch) { if (!need(4)) return Usage; double l; if (!parseLevel(a[3], &l)) return fail(Usage, "bad level"); return setProp(pathOf(a[2]), iface, "Trim", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "mute" && ch) { bool b; if (!parseBool(a, 3, &b)) return fail(Usage, "on|off"); return setProp(pathOf(a[2]), iface, "Muted", b, &e) ? Ok : fail(Rejected, e); }
        if (sub == "input" && ch) {   // channel input <slug> [<node[:POS,POS]>|none] — ADR 0009 refs
            if (a.size() < 4) { const QString ref = unwrap(objs.value(pathOf(a[2])).value("InputDevice")).toString(); if (g_json) out << QJsonDocument(QJsonObject{{"InputDevice", ref}}).toJson(); else out << (ref.isEmpty() ? QStringLiteral("none") : ref) << "\n"; return Ok; }
            const QString dev = a[3] == "none" ? QString() : a[3];
            if (!dev.isEmpty() && !checkRef(dev, true, &e)) return fail(NotFound, e);
            if (!setProp(pathOf(a[2]), iface, "InputDevice", dev, &e)) return fail(Rejected, e);
            QDBusInterface chObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            if (chObj.property("InputDevice").toString() != dev) return fail(Rejected, QStringLiteral("daemon refused '%1' (see its log)").arg(a[3]));
            return Ok;
        }
        if (sub == "volume" && !ch) { if (!need(4)) return Usage; double l; if (!parseLevel(a[3], &l)) return fail(Usage, "bad level"); return setProp(pathOf(a[2]), iface, "Volume", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "mute" && !ch) { bool b; if (!parseBool(a, 3, &b)) return fail(Usage, "on|off"); return setProp(pathOf(a[2]), iface, "Muted", b, &e) ? Ok : fail(Rejected, e); }
        if (sub == "outputs" && !ch) {
            const QStringList outs = unwrap(objs.value(pathOf(a[2])).value("Outputs")).toStringList();
            if (g_json) out << QJsonDocument(QJsonArray::fromStringList(outs)).toJson(); else for (const auto &o2 : outs) out << o2 << "\n";
            return Ok;
        }
        if ((sub == "output-add" || sub == "output-remove") && !ch) {
            if (!need(4)) return Usage;
            QDBusInterface mixObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            const QDBusMessage r = mixObj.call(sub == "output-add" ? "AddOutput" : "RemoveOutput", a[3]);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "fallback" && !ch) {
            if (!need(4)) return Usage;
            return setProp(pathOf(a[2]), iface, "FallbackOutput", a[3] == "none" ? QString() : a[3], &e) ? Ok : fail(Rejected, e);
        }
        if (sub == "output" && !ch) {   // mix output <slug> <node[:POS,POS]|none> — ADR 0009 refs
            if (!need(4)) return Usage;
            const QString dev = a[3] == "none" ? QString() : a[3];
            if (!dev.isEmpty() && !checkRef(dev, false, &e)) return fail(NotFound, e);
            if (!setProp(pathOf(a[2]), iface, "OutputDevice", dev, &e)) return fail(Rejected, e);
            QDBusInterface mxObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            if (mxObj.property("OutputDevice").toString() != dev) return fail(Rejected, QStringLiteral("daemon refused '%1' (see its log)").arg(a[3]));
            return Ok;
        }
        if (sub == "get" && !ch) {   // mix get <slug> — all properties (JSON) for scripts/tests
            QJsonObject j; const auto props = objs.value(pathOf(a[2]));
            for (auto it = props.cbegin(); it != props.cend(); ++it) j[it.key()] = QJsonValue::fromVariant(unwrap(it.value()));
            out << QJsonDocument(j).toJson(); return Ok;
        }
        return fail(Usage, "unknown subcommand '" + sub + "'");
    }
    if (cmd == "fx") {   // effects per channel/mix (ADR 0008)
        // fx types | fx get <channel|mix> <slug> | fx set <channel|mix> <slug> '<json>' | fx control <channel|mix> <slug> <key> <value>
        if (sub == "types") {
            const QString t = unwrap(o.mixer.value("FxTypes")).toString();
            out << t << "\n"; return Ok;
        }
        if (sub == "presets") {   // FX-4: name → chain JSON
            const QString t = unwrap(o.mixer.value("FxPresets")).toString();
            out << t << "\n"; return Ok;
        }
        if (!need(4)) return Usage;
        const bool ch = a[2] == "channel";
        if (!ch && a[2] != "mix") return fail(Usage, "expected 'channel' or 'mix'");
        const auto &objs = ch ? o.channels : o.mixes;
        const QString iface = ch ? "org.kmixdeck1.Channel" : "org.kmixdeck1.Mix";
        const QString path = QStringLiteral("%1/%2/%3").arg(ROOT, ch ? "channel" : "mix", a[3]);
        if (!objs.contains(path)) return fail(NotFound, QStringLiteral("no %1 '%2'").arg(a[2], a[3]));
        QDBusInterface obj(BUS, path, iface, QDBusConnection::sessionBus());
        if (sub == "get") { out << unwrap(objs.value(path).value("FxChain")).toString() << "\n"; return Ok; }
        if (sub == "set") {
            if (!need(5)) return Usage;
            const QDBusMessage r = obj.call("SetFx", a[4]);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "clear") {
            const QDBusMessage r = obj.call("SetFx", "{}");
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "copy") {   // FX-7: same chain onto another object, even while its device is absent
            if (!need(5)) return Usage;
            const bool toCh = a[4] == "channel";
            if (!toCh && a[4] != "mix") return fail(Usage, "expected 'channel' or 'mix'");
            const QString toPath = QStringLiteral("%1/%2/%3").arg(ROOT, toCh ? "channel" : "mix", a.size() > 5 ? a[5] : QString());
            const auto &toObjs = toCh ? o.channels : o.mixes;
            if (!toObjs.contains(toPath)) return fail(NotFound, "target not found");
            const QString chain = unwrap(objs.value(path).value("FxChain")).toString();
            if (chain.isEmpty() || chain == "{}") return fail(NotFound, "source has no chain");
            QDBusInterface to(BUS, toPath, toCh ? "org.kmixdeck1.Channel" : "org.kmixdeck1.Mix", QDBusConnection::sessionBus());
            const QDBusMessage r = to.call("SetFx", chain);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "control") {   // live: full Props key, gate:Threshold (dB) — quoting is the shell's problem
            if (!need(6)) return Usage;
            bool okNum = false; const double v = a[5].toDouble(&okNum);
            if (!okNum) return fail(Usage, "value must be a number");
            const QDBusMessage r = obj.call("SetFxControl", a[4], v);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        return fail(Usage, "unknown fx subcommand '" + sub + "'");
    }
    if (cmd == "cell") {
        if (!need(4)) return Usage;
        const QString path = cellPath(a[2], a[3]);
        if (!o.cells.contains(path)) return fail(NotFound, QStringLiteral("no cell %1×%2").arg(a[2], a[3]));
        if (sub == "get") { const auto c = o.cells[path]; if (g_json) out << QJsonDocument(QJsonObject::fromVariantMap(c)).toJson(); else out << (c.value("Muted").toBool() ? "muted " : "") << db(c.value("Volume").toDouble()).trimmed() << " dB  (linear " << c.value("Volume").toDouble() << ")\n"; return Ok; }
        if (sub == "set") { if (!need(5)) return Usage; double l; if (!parseLevel(a[4], &l)) return fail(Usage, "bad level '" + a[4] + "'"); return setProp(path, "org.kmixdeck1.Cell", "Volume", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "mute") { bool b; if (!parseBool(a, 4, &b)) return fail(Usage, "on|off"); return setProp(path, "org.kmixdeck1.Cell", "Muted", b, &e) ? Ok : fail(Rejected, e); }
        if (sub == "link") {
            if (!need(5)) return Usage;
            if (a[4] != "none" && !o.mixes.contains(QStringLiteral("%1/mix/%2").arg(ROOT, a[4]))) return fail(NotFound, QStringLiteral("no mix '%1'").arg(a[4]));
            if (a[4] == a[3]) return fail(Usage, "a cell cannot follow its own mix");
            const QDBusObjectPath p(a[4] == "none" ? QStringLiteral("/") : QStringLiteral("%1/mix/%2").arg(ROOT, a[4]));
            return setProp(path, "org.kmixdeck1.Cell", "Follows", QVariant::fromValue(p), &e) ? Ok : fail(Rejected, e);
        }
        return fail(Usage, "unknown subcommand '" + sub + "'");
    }
    if (cmd == "app") {
        if (sub == "list") {
            if (g_json) { QJsonArray arr; for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it) { auto v = it.value(); v["Path"] = it.key(); arr.append(QJsonObject::fromVariantMap(v)); } out << QJsonDocument(arr).toJson(); }
            else for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it) {
                const QString chPath = it.value().value("Channel").toString();
                const QStringList all = it.value().value("Channels").toStringList();
                out << QStringLiteral("%1  %2 %3 %4  -> %5\n")
                       .arg(it.value().value("NodeId").toString(), 5)
                       .arg(it.value().value("Running").toBool() ? QStringLiteral("*") : QStringLiteral(" "), -1)
                       .arg(it.value().value("Name").toString().left(24), -24)
                       .arg(it.value().value("MediaName").toString().left(20), -20)
                       .arg(all.isEmpty() ? (chPath == "/" ? QStringLiteral("(not on a channel)") : chPath.section('/', -1)) : all.join(QStringLiteral(",")));
            }
            return Ok;
        }
        auto findApp = [&](const QString &needle) {
            QString appPath;
            for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it)
                if (it.value().value("NodeId").toString() == needle || it.value().value("Name").toString().compare(needle, Qt::CaseInsensitive) == 0) appPath = it.key();
            return appPath;
        };
        if (sub == "move" || sub == "assign") {   // app assign <id|name> <ch>[,<ch>…] — CH-12
            if (!need(4)) return Usage;
            const QString appPath = findApp(a[2]);
            if (appPath.isEmpty()) return fail(NotFound, "no app '" + a[2] + "'");
            QDBusInterface appIf(BUS, appPath, "org.kmixdeck1.App", QDBusConnection::sessionBus());
            if (sub == "move") {
                const QString chPath = QStringLiteral("%1/channel/%2").arg(ROOT, a[3]);
                if (!o.channels.contains(chPath)) return fail(NotFound, "no channel '" + a[3] + "'");
                QDBusMessage r = appIf.call("MoveTo", QVariant::fromValue(QDBusObjectPath(chPath)));
                return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
            }
            QStringList names = a[3].split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (names.isEmpty()) names << a[3];
            QStringList paths;
            for (const QString &n : names) {
                const QString chPath = QStringLiteral("%1/channel/%2").arg(ROOT, n);
                if (!o.channels.contains(chPath)) return fail(NotFound, "no channel '" + n + "'");
                paths << chPath;
            }
            const QDBusMessage r = appIf.call("Assign", paths, false);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        return fail(Usage, "app list | app move <id|name> <channel> | app assign <id|name> <ch>[,<ch>…]");
    }
    if (cmd == "listen") {   // UX-2: listen [<node.name>|none] — the device I hear on; "what am I hearing" = mixes routed to it
        if (a.size() < 2) {
            const QString dev = unwrap(o.mixer.value("ListeningDevice")).toString();
            QStringList heard; for (auto it = o.mixes.cbegin(); it != o.mixes.cend(); ++it) if (it.value().value("Outputs").toStringList().contains(dev)) heard << it.value().value("Slug").toString();
            if (g_json) { out << QJsonDocument(QJsonObject{{"device", dev}, {"mixes", QJsonArray::fromStringList(heard)}}).toJson(QJsonDocument::Compact) << "\n"; return Ok; }
            out << (dev.isEmpty() ? QStringLiteral("none") : dev) << "  " << (heard.isEmpty() ? QStringLiteral("(no mix routed here)") : heard.join(QLatin1String(", "))) << "\n"; return Ok;
        }
        return setProp(QString::fromLatin1(ROOT), "org.kmixdeck1.Mixer", "ListeningDevice", a[1] == "none" ? QString() : a[1], &e) ? Ok : fail(Rejected, e);
    }
    if (cmd == "audition") {   // UX-12: hold one entity on the main output; `none` restores
        if (!need(2)) return Usage;
        const QString target = (a[1] == QLatin1String("none"))
            ? QStringLiteral("/")
            : QStringLiteral("%1/%2/%3").arg(ROOT, a[1], a.value(2));
        if (a[1] != QLatin1String("none")) {
            const bool ok = (a[1] == QLatin1String("channel") && o.channels.contains(target))
                         || (a[1] == QLatin1String("mix") && o.mixes.contains(target));
            if (!ok) return fail(NotFound, "expected existing 'channel <slug>' or 'mix <slug>'");
        }
        const QDBusMessage r = mixer.call("Audition", QVariant::fromValue(QDBusObjectPath(target)));
        return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
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
