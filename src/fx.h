// SPDX-License-Identifier: GPL-3.0-or-later
// Effects model + filter-chain renderer (ADR 0008). Pure data, no PipeWire calls — testable without a graph.
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace kmixdeck::fx {

/// One insert in a chain. `params` are in the effect's own units (dB, ms, Hz); unknown keys are refused by validate().
struct Effect {
    QString type;                  // highpass | gate | compressor | eq | limiter | noise | ladspa
    bool enabled = true;           // FX-5 per-effect bypass: a disabled effect is left out of the rendered graph
    QMap<QString, double> params;
    QString plugin, label;         // type == ladspa only: e.g. "sc4m_1916", "sc4m"
    QJsonObject toJson() const;
    static Effect fromJson(const QJsonObject &o);
};

struct Chain {
    bool enabled = true;           // FX-5 whole-chain bypass: keep the DSP, route around it
    QVector<Effect> effects;       // ordered, input → output
    /// True when the rendered graph would contain at least one node: chain on AND ≥1 effect enabled (FX-5).
    bool isActive() const { if (!enabled) return false; for (const auto &e : effects) if (e.enabled) return true; return false; }
    QJsonObject toJson() const;
    static Chain fromJson(const QJsonObject &o);
    bool operator==(const Chain &o) const { return toJson() == o.toJson(); }
};

/// What a built-in effect type looks like: label for UIs, parameter spec, which LADSPA file it needs.
struct ParamSpec { QString key; QString label; QString unit; double min, max, def; };
struct TypeSpec {
    QString type, label, description;
    QVector<ParamSpec> params;
    QString ladspaFile;            // "" = builtin, else the .so base name that must be installed
};
const QVector<TypeSpec> &builtinTypes();
const TypeSpec *typeSpec(const QString &type);

/// One-click chains, name → chain JSON (FX-4). Presets are just starting points — editable like any chain.
QJsonObject presetChains();

/// Is the LADSPA library present (LADSPA_PATH, /usr/lib/ladspa, /usr/lib64/ladspa, multiarch)? "" for builtin = true.
bool ladspaAvailable(const QString &file);
/// Distro-agnostic hint for the CLI/UI: which package brings this file.
QString packageHint(const QString &file);

/// Validate a chain: known type, params in range, plugin present. Returns "" when fine, else the first problem.
QString validate(const Chain &c);

/// Render the `args` of libpipewire-module-filter-chain for this chain (disabled effects skipped).
/// Streams target entryNode → chain → exitNode → targetSink. mediaName rides on BOTH sides so restored
/// stream state keeps working across toggling effects. `description` shows up in pavucontrol-like tools.
QString renderFilterChainArgs(const Chain &c, const QString &description, const QString &entryNode,
                              const QString &exitNode, const QString &mediaName, const QString &targetSink,
                              const QString &idPrefix);
/// The Props param name filter-chain uses for a control at runtime, e.g. "gate:Threshold (dB)".
/// Returns {name → value} for every enabled effect of the chain (for live updates without a reload).
QVector<QPair<QString, double>> controlValues(const Chain &c, const QString &idPrefix);

} // namespace kmixdeck::fx
