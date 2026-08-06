/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sliceapplication.h"
#include "slicenode.h"

#include <cstdlib>

int main(int argc, char **argv)
{
    if (CSlice::Node::isNodeMode(argc, argv)) {
        return CSlice::Node::run(argc, argv);
    }

    SliceApplication app(argc, argv);
    return app.run();
}
