/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef COPYTARGETS_H
#define COPYTARGETS_H

#include <QString>

#include <atomic>

// Looks at and deletes what a copy writes to: a destination folder, the files of a pattern in
// it, or an opened source on this computer. Meant for worker threads: a stored account is
// applied to the calling thread as network-only credentials (like "runas /netonly", the way
// robocopy is started), and taken off again before returning. Junctions and symbolic links are
// counted and deleted as links, never followed.
namespace COpyTargets {

struct Target {
    QString folder;
    // "*": the folder itself with everything in it. Otherwise the files in the folder that
    // match the pattern (e.g. "movie.mp4"); the folder is kept.
    QString pattern = QStringLiteral("*");
    // With a pattern: also the matching files in subfolders.
    bool recursive = false;
    QString accountName;
};

struct Inspection {
    // False when the folder (or its share) could not be reached; error says why.
    bool reachable = false;
    bool exists = false;
    int fileCount = 0;
    int folderCount = 0;
    qint64 totalBytes = 0;
    // Also set when the folder was reached but parts of it could not be read.
    QString error;
};

Inspection inspect(const Target &target, const std::atomic_bool &cancel);
// True when nothing of the target is left, also when there was nothing to delete.
bool remove(const Target &target, const std::atomic_bool &cancel, QString *error);

} // namespace COpyTargets

#endif // COPYTARGETS_H
