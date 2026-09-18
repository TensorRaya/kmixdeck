// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// Peak meters (ADR 0006): one tiny capture stream per metered node, in "resample.peaks" mode at a fixed
// tick rate — the same mechanism pipewire-pulse uses for PA_STREAM_PEAK_DETECT. Runs on the Graph's
// PipeWire thread loop; publishes one batch of peaks per tick on the Qt thread.
#include <QObject>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace kmixdeck::pw {

class Graph;

class Meters : public QObject {
    Q_OBJECT
public:
    static constexpr int kRateHz = 25;

    Meters(Graph *graph, QObject *parent = nullptr);
    ~Meters() override;

    /// Meter exactly these node names (others are torn down). Empty list = no streams at all.
    void setTargets(const QStringList &nodeNames);
    QStringList targets() const;

Q_SIGNALS:
    /// node.name → peak (linear 0…1) for every metered node, once per tick.
    void peaks(const QHash<QString, float> &peaks);

private:
    void publish();
    struct Impl;
    std::unique_ptr<Impl> d;
    QTimer m_tick;
};

} // namespace kmixdeck::pw
