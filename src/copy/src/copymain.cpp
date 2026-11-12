/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "copyapplication.h"

int main(int argc, char **argv)
{
    COpyApplication app(argc, argv);

    // A second launch hands its manifest paths to the running instance and exits.
    if (app.forwardedToRunningInstance()) {
        return 0;
    }

    return app.run();
}
