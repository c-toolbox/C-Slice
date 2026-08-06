/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSLICE_SLICENODE_H
#define CSLICE_SLICENODE_H

namespace CSlice::Node {

bool isNodeMode(int argc, char **argv);
int run(int argc, char **argv);

} // namespace CSlice::Node

#endif // CSLICE_SLICENODE_H
