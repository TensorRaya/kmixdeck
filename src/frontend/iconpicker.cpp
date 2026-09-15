// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "iconpicker.h"
#include <KIconDialog>
#include <KLocalizedString>
#include <QFileDialog>
#include <QStandardPaths>

using namespace kmixdeck::frontend;

// Names verified against Breeze (every entry exists in breeze-icons 6.x). Channels are sources — what
// is playing; mixes are destinations — who is listening.
QStringList IconPicker::suggestions(const QString &kind) const {
    if (kind == QLatin1String("mix"))
        return {QStringLiteral("audio-headphones"), QStringLiteral("audio-headset"), QStringLiteral("audio-speakers"),
                QStringLiteral("media-record"), QStringLiteral("camera-video"), QStringLiteral("video-television"),
                QStringLiteral("video-display"), QStringLiteral("call-start"), QStringLiteral("im-user"),
                QStringLiteral("internet-web-browser"), QStringLiteral("audio-card"), QStringLiteral("computer")};
    return {QStringLiteral("input-gaming"), QStringLiteral("applications-games"), QStringLiteral("folder-music"),
            QStringLiteral("view-media-artist"), QStringLiteral("audio-input-microphone"), QStringLiteral("audio-headset"),
            QStringLiteral("internet-web-browser"), QStringLiteral("dialog-messages"), QStringLiteral("im-user"),
            QStringLiteral("call-start"), QStringLiteral("preferences-desktop-notification-bell"), QStringLiteral("computer"),
            QStringLiteral("camera-web"), QStringLiteral("media-playback-start"), QStringLiteral("applications-multimedia"),
            QStringLiteral("audio-radio"), QStringLiteral("system-run"), QStringLiteral("audio-card")};
}

QString IconPicker::pickFromTheme(const QString &current) const {
    KIconDialog dlg;
    dlg.setup(KIconLoader::Desktop, KIconLoader::Any, /*strictIconSize*/ false, /*iconSize*/ 48, /*user*/ false, /*lockUser*/ false, /*lockCustomDir*/ true);
    dlg.setSelectedIcon(current.startsWith(QLatin1Char('/')) ? QString() : current);
    return dlg.openDialog();
}

QString IconPicker::pickFile() const {
    return QFileDialog::getOpenFileName(nullptr, i18n("Choose an image"),
                                        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                        i18n("Images (*.png *.svg *.svgz *.jpg *.jpeg *.webp)"));
}
