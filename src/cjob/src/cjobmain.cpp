/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cjobapplication.h"

int main(int argc, char **argv)
{
    CJobApplication app(argc, argv);

    // Check if another instance is already running.
    // The constructor returns early with m_server = nullptr when another instance exists.
    // We use serverChanged signal check via the server pointer.
    if (!app.server()) {
        return 1;
    }

    return app.run();
}
