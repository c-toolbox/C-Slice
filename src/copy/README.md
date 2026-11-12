# C-Opy

A small robocopy front-end that copies each C-Slice output (one per SGCT window/node) to one or more machines.

C-Slice writes `<output name>.c-opy.json` next to its outputs when a slice finishes. C-Opy opens that manifest, maps every output identifier to its destination folders (from a saved cluster or edited in place), and runs `robocopy` for each output and destination.

- Opening files, folders or manifests replaces the previous source selection and clears the destination preview and node checks (steps 3 and 4). Items selected or dropped together stay together. The selected cluster's rules and existing copy tasks are kept.
- Single instance: launching `C-Opy.exe <manifest.c-opy.json>` while C-Opy is running opens the new selection in the existing window (over `QLocalServer` "C-Opy").
- Clusters and settings: `C-Opy/copy.conf` (KConfig, generated from `src/settings/copysettings.kcfg`) in the generic config folder (`%LOCALAPPDATA%` on Windows). Clusters can be imported from JSON cluster files.
- Destination tokens: `{folder}` (output directory name), `{identifier}`, `{job}`, `{date}`. Manifest outputs without their own destinations use the cluster's `manifestDestinations`, else the `*` default rule (which files and folders use too).
- Accounts for protected shares: user name and password in Windows Credential Manager as generic credentials `C-Opy/<name>` (`copycredentials.cpp`). Robocopy is started with them as network-only credentials via `CreateProcessWithLogonW(LOGON_NETCREDENTIALS_ONLY)` (`robocopyrunner.cpp`). Clusters store only the account name. When an output and the cluster's default rule name no account, the settings decide: this computer's account for the current cluster (`[ClusterAccounts]` in `copy.conf`), else `DefaultAccount`, else the current Windows user.
- Library (like C-Troll clusters and nodes): every `*.json` in the cluster folder (default `data/copy/clusters` next to the executable) is a read-only cluster that maps outputs to node *names*. The node folder (default `C-Opy/nodes` next to the settings file, seeded from `data/copy/nodes` on first start) has one file per machine with `name` and `ip`, in C-Troll's node format. `{ip}`, `{node}` and `{ip:<node>}` in destinations are resolved from it when copying starts, so each computer only sets its own node addresses. Both folders are settings (`ClusterFolder`, `NodeFolder`). Internally (`COpyController`, QML) a cluster is still called a preset.
- Files and folders (Open file…/Open folder…, the drop zone or anywhere on the window, or command line) are copied to every node of the current cluster (the node names it assigns to outputs) with the default rule's destinations. The node that is this computer (host name or local IP address match, or the `LocalNode` setting) is skipped when `SkipLocalNode` is on.
- Same path mode (`CopyToSamePath`, default on): a file or folder on `D:` goes to the same path on every node through the cluster's `driveShares` (`"D:": "\\\\{ip}\\d$"`); drives without a share are reported in step 3 and skipped. Otherwise files and folders use the default rule's destinations.
- Folders and manifest outputs are always mirrored (`/MIR`); single files are copied without it. Junctions and symbolic links are excluded (`/XJ`) so linked data outside the source is not copied. C-Opy refuses drive/share roots and overlapping source/destination folders, and skips duplicate copy jobs.
- Log summaries include their destination and column headings; `/BYTES` prints exact byte counts. `/FP` identifies files by their full paths so retries are counted once without conflating equal names in different subfolders. Counts are per destination: sending 1,000 files to two destinations copies 2,000 files in total. The `Total` column includes skipped files; `Copied` is the data actually transferred.
- Robocopy arguments come from profiles in `C-Opy/robocopy-profiles.json` next to the settings file (built-in: "Large files", "Many small files"). Single files (opened files, movie outputs) and folders (opened folders, image sequence outputs) each use a configured profile (`FileRobocopyProfile`, `FolderRobocopyProfile`).
- UI: the main window is five steps (what to copy, cluster, destinations with a preview from `COpyController::planCopies()`, check the nodes, copy). The tabbed settings dialog (`qml/COpySettingsDialog.qml`) holds startup options, this computer, folders, robocopy profiles, nodes and accounts.
- Check the nodes (`COpyController::checkTargets()`/`deleteTargets()`, `copytargets.cpp`): every planned destination, and the source of an opened file or folder, is inspected on a worker thread for reachability, files and size, and can be deleted there. Stored accounts are applied to the worker thread as network-only credentials (`LogonUserW(LOGON32_LOGON_NEW_CREDENTIALS)`), like robocopy's. Junctions and links are deleted as links, never followed; drive and share roots are never deleted; nothing is deleted while copying.
- Example cluster for `configs/nrkp_mono_2026.json`: `data/copy/clusters/nrkp_2026_cluster.json` with the nodes `data/copy/nodes/nrkp-*.json`.

User documentation, including the manifest and cluster formats: `docs/docs/workflow/copy-outputs.md`.

## Building

C-Opy builds as part of C-Slice (`add_subdirectory(copy/src)`), or standalone from this folder:

```bash
cmake -S . -B build
cmake --build build --config Release
```

Requires Qt 6.6+ (Core, Gui, Network, Qml, Quick, QuickControls2) and KF6 Kirigami and ColorScheme.

## Regression tests

The Windows integration suite uses isolated temporary settings and folders and runs real Robocopy copies, including a 1,000-file mirror to two destinations, unchanged reruns, removals, junctions, duplicate destinations, hidden/system attributes and cancellation. It additionally requires Qt Test.

```powershell
cmake -S src/copy/tests -B build/copy-tests -DCMAKE_PREFIX_PATH=<Qt-and-KF-prefix> -DCMAKE_CONFIGURATION_TYPES=RelWithDebInfo
cmake --build build/copy-tests --config RelWithDebInfo
ctest --test-dir build/copy-tests -C RelWithDebInfo --output-on-failure
```

Run these commands from the repository root, with the Qt/KF runtime DLLs on `PATH`.

## License

GPL-3.0-or-later
