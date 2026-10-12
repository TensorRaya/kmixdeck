// ops-h3y5z: Graph::destroyObject destroys the node it was given, never the object that inherited its id.
//
// PipeWire hands a freed global id to the very next object (pw_map's free list is LIFO). A registry destroy carries
// nothing but the id, so a destroy for a node that is already gone lands on its successor. This probe makes that
// situation on purpose and reports what happened, one line, for tests/integration/test_audio_graph.py:
//   1. create null sink A, wait until the graph mirrors it          → NodeInfo a
//   2. destroy A, wait until the graph saw it go
//   3. create null sink B, wait until mirrored                      → NodeInfo b  (b.id == a.id: the id was reused)
//   4. destroyObject(a) again — the stale request a reconciler makes when its snapshot is a moment behind
//   5. wait 1 s, then: is B still there?
// Output: `reused=<0|1> stale_destroy_sent=<0|1> b_alive=<0|1>`. Exit 0 when B survived, 1 when it was destroyed,
// 2 when the setup itself failed. Built with the tests, never installed.
#include "pipewire/graph.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>
#include <functional>

using kmixdeck::pw::Graph;
using kmixdeck::pw::NodeInfo;

static bool waitFor(const std::function<bool()> &pred, int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        if (pred()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return pred();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Graph g;
    if (!g.connect()) { std::fprintf(stderr, "no PipeWire\n"); return 2; }
    const QString an = QStringLiteral("probe.identity.a"), bn = QStringLiteral("probe.identity.b");

    g.createNullSink(an, QStringLiteral("identity probe A"), true);
    if (!waitFor([&] { return g.node(an).has_value(); }, 5000)) { std::fprintf(stderr, "A never appeared\n"); return 2; }
    const NodeInfo a = *g.node(an);

    if (!g.destroyObject(a)) { std::fprintf(stderr, "first destroy of A refused\n"); return 2; }
    if (!waitFor([&] { return !g.node(an).has_value(); }, 5000)) { std::fprintf(stderr, "A never went away\n"); return 2; }

    g.createNullSink(bn, QStringLiteral("identity probe B"), true);
    if (!waitFor([&] { return g.node(bn).has_value(); }, 5000)) { std::fprintf(stderr, "B never appeared\n"); return 2; }
    const NodeInfo b = *g.node(bn);

    const bool sent = g.destroyObject(a);   // stale: A is gone, its id may belong to B now
    waitFor([] { return false; }, 1000);
    const bool alive = g.node(bn).has_value();
    std::printf("reused=%d stale_destroy_sent=%d b_alive=%d a.id=%u b.id=%u a.serial=%u b.serial=%u\n",
                a.id == b.id, sent, alive, a.id, b.id, a.serial, b.serial);
    std::fflush(stdout);
    if (alive) g.destroyObject(*g.node(bn));   // linger node: leave the sandbox as we found it
    waitFor([&] { return !g.node(bn).has_value(); }, 2000);
    return alive ? 0 : 1;
}
