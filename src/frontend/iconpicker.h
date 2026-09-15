// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// Icon choice for channels and mixes (UX-8). Two KDE-native pickers behind one QML-callable object:
// KIconDialog for anything in the icon theme (thousands of names, search, categories) and a file dialog
// for a custom image. The daemon only ever sees a string: an icon name or an absolute path.
#include <QObject>
#include <QQmlEngine>
#include <QStringList>

namespace kmixdeck::frontend {

class IconPicker : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit IconPicker(QObject *parent = nullptr) : QObject(parent) {}
    /// Curated shortlist shown as tiles before the user reaches for the full theme dialog.
    Q_INVOKABLE QStringList suggestions(const QString &kind) const;
    /// Opens KIconDialog (modal). Returns the chosen icon name, "" if cancelled.
    Q_INVOKABLE QString pickFromTheme(const QString &current) const;
    /// Opens a file dialog for PNG/SVG/JPEG. Returns an absolute path, "" if cancelled.
    Q_INVOKABLE QString pickFile() const;
};

} // namespace kmixdeck::frontend
