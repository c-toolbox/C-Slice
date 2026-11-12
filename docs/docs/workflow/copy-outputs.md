---
title: 'Copying outputs with C-Opy'
sidebar_position: 5
---

# Copying outputs with C-Opy

A slice produces one output per SGCT window (for example `Node1` … `Node6`), and each output usually has to end up on a different playback machine. C-Opy is a small companion application that copies every output to its machines with `robocopy`, using destinations you set up once and save as clusters.

## How it fits together

1. When a slice finishes successfully, C-Slice writes a **copy manifest** named `<output name>.c-opy.json` into the output directory. It lists every enabled output, the folder it was written to and which files belong to it.
2. C-Opy opens the manifest, looks up the destinations for each output and runs one `robocopy` per output and destination.

To open every finished slice in C-Opy automatically, enable **Preferences → After slicing: Open outputs in C-Opy**. This also works for jobs run from the internal queue or C-Job. Only one C-Opy window runs at a time, so manifests from later slices open in the existing window and replace the previous selection.

Verification runs and runs without encoding produce no files, so they write no manifest.

## Using C-Opy

The window walks you through five steps, top to bottom:

1. **What to copy**: **Open file…**, **Open folder…** or **Open C-Slice manifest…**. The chosen kind stays highlighted. You can also drop files, folders or manifests on the drop zone below the buttons (or anywhere on the window), or pass it on the command line (`C-Opy.exe show.c-opy.json` or `C-Opy.exe D:\Media\show`). C-Slice sends its manifests here by itself when **Open outputs in C-Opy** is on.

   Opening a new selection replaces the previous sources, rebuilds **Destinations** (step 3), and clears **Check the nodes** (step 4). Files or manifests selected together stay together. The selected cluster's destination rules and existing copy tasks are kept. An invalid selection leaves the current lists intact.
   - A **file** is copied to every node of the cluster with the *single files* robocopy profile (default **Large files**).
   - A **folder** is mirrored to every node of the cluster with the *folders* profile (default **Many small files**); choose the profiles under **Settings → Robocopy**.
   - A **manifest** copies each output to its own node. Movie outputs use the single files profile, image sequences the folders profile.
2. **Cluster to copy to**: choose the cluster in the combo box. Below it you see its nodes, nodes that are missing from the node folder, its description, and whether this computer is one of them (see [Clusters and this computer](#clusters-and-this-computer)). Two examples come with C-Opy: `nrkp_2026_cluster` (the GUI machine and the six render nodes) and `nrkp_2026_render_nodes` (the render nodes only). **Save as…** stores your changes as your own cluster, **Import…** loads clusters from a JSON file.
3. **Destinations**: for a file or folder, choose **Same path on every node** (through the cluster's [drive shares](#same-path-on-every-node)) or **A fixed destination on every node** (the default rule; `\\{ip}\media\{folder}` sends a folder `MyShow` to `\\<node address>\media\MyShow`), and the account to copy with. For a manifest you see the default rule, the cluster's manifest destination and every output, each with its node, account and destinations. Below them, **Planned copies** lists every copy that **Start** will make (source, node, destination, mirror and profile) and every copy that is skipped, with the reason.
4. **Check the nodes**: **Check nodes** connects to every node of the planned copies and shows, per node, whether it can be reached, and per destination what is already there (files, folders and size). For an opened file or folder it also lists the source on this computer. See [Checking and deleting on the nodes](#checking-and-deleting-on-the-nodes).
5. **Copy**: **Start** queues the planned copies. **Stop**, **Retry failed** and **Clear finished** manage the running and finished copies.

Details of the destinations:

- An output can have several destinations, such as UNC paths (`\\node1\media\shows`) or local paths. Outputs without their own destinations use the cluster's **manifest destination** (`manifestDestinations` in a cluster file), or, when it has none, the **default rule**. The manifest destination applies only to manifest outputs; opened files and folders keep using the default rule or the drive shares. Outputs with no destination at all are skipped.
- Each output can be assigned a node, a named machine from the [node folder](#library-clusters-and-nodes). `{ip}` in a destination becomes that node's IP address, so `\\{ip}\media\{folder}` works on every network once the node addresses are set. Outputs without their own node use the default rule's node.
- **Tokens**: `{folder}` (the name of the slice output directory, i.e. the parent folder of `GUI`, `Node1`, …; for an opened folder its own name, for an opened file the name of the folder it is in), `{identifier}` (e.g. `Node3`), `{job}` (the output name), `{date}` (`yyyy-MM-dd`), `{ip}` and `{node}` (address and name of the node), and `{ip:<node name>}` (address of a named node, e.g. `\\{ip:backup}\shows\{folder}` for an extra copy). A destination whose node is missing or has no address is skipped.
- **Accounts**: a rule can copy with a stored account instead of your own Windows user; see [Password-protected shares](#password-protected-shares).
- Clusters from the cluster folder are read-only in C-Opy: **Save as…** makes your own copy, or edit the file. Your working destinations, including unsaved edits, are kept between sessions.

## Checking and deleting on the nodes

Step 4 checks the planned copies before anything is copied. **Check nodes** looks at every destination of the planned copies, with the same account the copy would use:

- A node is shown as reachable (green) when at least one of its destinations, or the share it is on, can be opened. Otherwise it is shown in red; hover over it, or look at its destinations, for the reason (for example *The network path was not found* or *Access is denied*).
- Each destination shows what is already there: for a folder or an image sequence the whole destination folder, for a file or a movie output only that file in the destination folder.
- For an opened file or folder (not for a manifest), the first row is the source on **this computer**.

Anything found can be deleted, on the nodes and on this computer at the same time:

- The delete button of a row deletes that destination on its node, or the source on this computer.
- **Delete all…** deletes everything found on the nodes. When an opened file or folder was found too, the confirmation offers **Also delete the source on this computer**; it is off by default.

Deleting is permanent: files are not moved to the Recycle Bin. Junctions and symbolic links are removed as links; what they point to is left alone. C-Opy never deletes a drive root or a share root, deletes nothing while copies are running, and does not start a copy while it is deleting. After deleting, each row is checked again, and a deleted source is removed from step 1. When the planned copies change after a check, step 4 asks you to check again.

## Clusters and this computer

A cluster is also the list of machines that files and folders are copied to: its **cluster nodes** are all nodes it assigns to outputs (including the default rule's node). For the [nrkp_2026 cluster](#example-the-nrkp_2026-cluster) that is `nrkp-gui` and `nrkp-node1` … `nrkp-node6`. An opened file or folder is copied once per cluster node to each of the default rule's destinations, with `{ip}` and `{node}` set to that node. A destination without `{ip}` or `{node}` is copied to only once.

When C-Opy runs on one of the cluster's machines, typically the master, that machine already has the files, so it is skipped: files and folders go only to the other nodes, and manifest outputs whose destination uses this computer's node are not copied. Step 2 shows which node this computer is and how many nodes receive copies, and **Planned copies** marks the skipped node.

C-Opy recognizes this computer when a node's name equals the host name (or its first part, e.g. `nrkp-gui` for `nrkp-gui.lab.local`), or when the node's address is one of this computer's IP addresses or its host name. If neither matches, for example because the node names differ from the host names, choose the node under **Settings → General → This computer is node**. Turn off **Do not copy to this computer** to copy to this computer as well.

## Same path on every node

On a cluster, the master and the nodes usually have the same drives and folders, and media opened at `D:\Media\Show` on the master should end up at `D:\Media\Show` on every node. For that, a cluster lists a **drive share** per drive: the share on each node that is that drive, typically the administrative shares `\\{ip}\d$` and `\\{ip}\e$`:

```json
"driveShares": {
  "D:": "\\\\{ip}\\d$",
  "E:": "\\\\{ip}\\e$"
}
```

With **Same path on every node** selected in step 3, a folder `D:\Media\Show` is mirrored to `\\<node address>\d$\Media\Show` on every node, and a file `E:\Clips\intro.mp4` is copied to `\\<node address>\e$\Clips\intro.mp4`. The shares are listed and editable in step 3, and saved with the cluster.

If an opened file or folder is on a drive the cluster has no share for (for example `F:`), step 3 warns about it and offers to add `\\{ip}\f$`, and **Planned copies** shows it as skipped. Sources on a network path (`\\server\share\…`) have no drive letter and cannot be copied to the same path; use **A fixed destination on every node** for them.

Administrative shares (`d$`, `e$`) are only open to administrators of the node, so they normally need a stored [account](#password-protected-shares) that is an administrator on the nodes (e.g. `NRKP-NODE1\Administrator`, or a domain account in the nodes' Administrators group). Choose it as the account in step 3. For local accounts on workgroup machines, Windows may also require the `LocalAccountTokenFilterPolicy` registry setting on the nodes for remote administrative access.

## Robocopy profiles and mirroring

Each copy runs:

```text
robocopy <sourceDirectory> <destination> <filePattern> /MIR <profile arguments> /XJ /BYTES /FP
```

Folders and manifest outputs are always **mirrored** (`/MIR`): robocopy copies only files that are new or changed (by size and time stamp), and removes files and subfolders from the destination that are no longer in the source, so the destination becomes an exact copy. Unchanged files are not copied again, so re-running a copy after a small change is fast. A single opened file is copied without `/MIR` (robocopy still skips it when it is unchanged), so that nothing else in its destination folder is removed.

Because mirroring removes everything else in the destination, C-Opy refuses to mirror into a drive root (`D:\`) or a share root (`\\machine\media`); add a subfolder such as `\{folder}`.

C-Opy also refuses overlapping source and destination folders, including destinations inside the source or its parents. Duplicate source/destination jobs are skipped, including copies already queued or running. Junctions and symbolic links are excluded (`/XJ`), preventing links from expanding the copy into other folders or back into the source.

The log labels each summary with its destination and the columns **Total**, **Copied**, **Skipped**, **Mismatch**, **FAILED** and **Extras**. **Total** includes files Robocopy skipped; **Copied** is what it transferred. Byte counts are exact (`/BYTES`). Each destination receives its own copy: 1,000 source files sent to two destinations means 2,000 copied files across both tasks.

While copying, step 5 shows the elapsed time and the current speed for every task and for the whole copy. The speed is measured from the bytes robocopy has written over the last 20 seconds (read from the process's I/O counters, so it also works with `/MT` and `/NP`), and follows changes in network speed. When a task or the whole copy finishes, step 5 and the log show how much was copied, how long it took and the average speed.

Profiles without `/MT` (such as **Large files**) also show an estimate of the time left. Files robocopy skips as unchanged count as done; when most recently passed files were unchanged, the estimate assumes the rest are unchanged too. Unchanged files that robocopy has not reached yet are counted as still to copy, so after a small change the estimate can be high until robocopy passes them. Profiles with `/MT` (such as **Many small files**) show no time left: robocopy lists files there when it queues them, not when it copies them, so the time left cannot be estimated from its output. The whole copy shows a time left only when every task estimates one.

The profile arguments come from a **robocopy profile**. Single files (opened files and movie outputs) and folders (opened folders and image sequence outputs) each have their own profile, chosen under **Settings → Robocopy**. There are two built-in profiles:

| Profile | Arguments | Use it for |
| --- | --- | --- |
| Large files | `/J /R:2 /W:5 /NDL` | Video files and other large files. Unbuffered I/O (`/J`), progress per file. |
| Many small files | `/MT:16 /R:2 /W:5 /NDL /NP` | Image sequences and folders with many small files. 16 copy threads per robocopy (`/MT:16`), no per-file progress. |

Both retry twice, five seconds apart, and leave out the directory listing. Edit, add or remove profiles under **Settings**. `/MIR` and `/PURGE` in a profile are ignored, since C-Opy decides about mirroring. Add `/FFT` when the destinations are NAS or Samba shares that round time stamps, so unchanged files are not copied again. Robocopy exit codes below 8 count as success.

The profiles are saved in `robocopy-profiles.json` next to the settings file (`%LOCALAPPDATA%\C-Opy\robocopy-profiles.json`). Until you save them, the built-in profiles are used.

```json
{
  "format": "c-opy-robocopy-profiles",
  "version": 1,
  "profiles": [
    { "name": "Large files", "arguments": "/J /R:2 /W:5 /NDL", "description": "Video files and other large files" },
    { "name": "Many small files", "arguments": "/MT:16 /R:2 /W:5 /NDL /NP", "description": "Image sequences" }
  ]
}
```

## Settings

**Settings** opens a dialog with four tabs:

- **General**: **Cluster on startup** (or *Last used*), **Parallel copies**, **Start copying automatically** when a file, folder or manifest arrives, **Skip hidden and system files** (robocopy `/XA:SH`; mirrored folders on the nodes lose theirs, files inside hidden folders are still copied), **This computer is node** and **Do not copy to this computer** (see [Clusters and this computer](#clusters-and-this-computer)), and the cluster and node folders.
- **Robocopy**: the profile for single files and the profile for folders, and the profiles themselves (name, arguments, description). **Restore built-in profiles** brings back *Large files* and *Many small files*.
- **Nodes**: the address of each node, and new nodes. See [Library clusters and nodes](#library-clusters-and-nodes).
- **Accounts**: accounts for protected shares, the **Default account** and an account per cluster. See [Password-protected shares](#password-protected-shares).

General and Robocopy take effect when you press **Save**. Node and account changes are written immediately.

## Library clusters and nodes

Like C-Troll's clusters and nodes, C-Opy separates *what goes where* from *where the machines are*:

- The **cluster folder** holds one JSON file per cluster. A cluster maps every output to a node *name* and uses `{ip}` in its destinations, so the same file works on every network. Every `*.json` file in the folder and its subfolders is listed as a library cluster. The default is `data\copy\clusters` next to `C-Opy.exe`, which comes with the [nrkp_2026 example](#example-the-nrkp_2026-cluster). A file can hold a single cluster object (without a `name`, the file name is used) or a `clusters` array.
- The **node folder** holds one JSON file per machine, with its name and IP address (or host name). The addresses differ between networks, so every computer has its own node folder: by default `C-Opy\nodes` next to the settings file (`%LOCALAPPDATA%\C-Opy\nodes`). On the first start it is filled with the bundled example nodes from `data\copy\nodes`.

```json
{
  "name": "nrkp-node1",
  "ip": "10.10.0.11",
  "description": "NRKP render node 1"
}
```

This is the same format as C-Troll's `nodes` folder (C-Opy ignores `port` and `secret`), so the node folder can point to C-Troll's node folder to share one set of addresses. Node names match regardless of case.

Under **Settings → Nodes** you can change a node's address, which rewrites its file and keeps all other keys, and add nodes, which writes `<node folder>\<name>.json`. To rename or remove a node, edit or delete its file, then **Reload**. Both folders are set under **Settings → General**. Both can point to a shared network folder, and leaving one empty uses its default. Edits to the files take effect after **Reload** or a restart.

If a saved cluster has the same name as a library cluster, the saved one is listed and the file is skipped (see the log). Delete the saved cluster to use the file.

## Password-protected shares

When the destination shares need a different account than the Windows user running C-Opy, store that account under **Settings → Accounts** and select it as the **Account** of an output, or of the default rule to use it for all outputs.

Under **Settings → Accounts** you can also choose the **Default account** for this computer and, optionally, a different account for each cluster. These apply to every cluster whose default rule names no account, including library clusters, whose files stay unchanged. A copy uses the first account that is set, in this order:

1. The output's own account.
2. The cluster's default rule account.
3. This computer's account for the cluster.
4. The **Default account**.
5. The current Windows user.

When the default rule has no account of its own, step 3 shows it as **From settings (*account*)**.

- **Storage**: the user name and password are saved in **Windows Credential Manager** as a *generic* credential named `C-Opy/<account name>`. Windows encrypts it for your Windows user on this machine. Clusters and the settings file only contain the account *name*, never the password, so cluster files can be shared safely. Each machine and user that runs C-Opy stores the account once.
- **Scope**: Windows never uses generic credentials to sign in to anything by itself. C-Opy reads the password only when it starts a copy, and starts that robocopy process with *network-only* credentials, the same mechanism as `runas /netonly`. Only that robocopy process authenticates with the account, and only towards network shares. No mapped drive or `net use` connection is created, and other programs cannot use the account to reach the share.
- **User names**: `DOMAIN\user` for a domain account, `MACHINE\user` for a local account on the target machine (e.g. `NRKP-NODE1\media`), or `user@domain`.
- **Limitation**: the password is protected per Windows user, like all Credential Manager entries. Other programs running as the *same* Windows user could read it. Run C-Opy under a dedicated Windows user if that matters.

The account is first checked when robocopy connects to the share. A wrong password therefore shows up as a failed copy with a robocopy logon error, not when you store the account. Copies to a share on the same computer (`\\localhost\…`) always use your own Windows user, because Windows bypasses the supplied credentials for local connections.

You can also manage the entries in **Control Panel → Credential Manager → Windows Credentials → Generic Credentials**, or with `cmdkey /generic:C-Opy/<name> /user:<user> /pass`.

## Example: the nrkp_2026 cluster

`data/copy/clusters/nrkp_2026_cluster.json` is a library cluster for slices made with `configs/nrkp_mono_2026.json`, which has the windows `GUI` and `Node1` … `Node6`. Each window folder's contents are copied to a folder named after the output directory on that window's machine:

```json
{
  "name": "nrkp_2026_cluster",
  "nodes": {
    "GUI":   "nrkp-gui",
    "Node1": "nrkp-node1",
    "Node2": "nrkp-node2",
    "Node3": "nrkp-node3",
    "Node4": "nrkp-node4",
    "Node5": "nrkp-node5",
    "Node6": "nrkp-node6"
  },
  "manifestDestinations": ["\\\\{ip}\\media\\{folder}"],
  "destinations": {
    "*": ["\\\\{ip}\\media\\{folder}"]
  },
  "driveShares": {
    "D:": "\\\\{ip}\\d$",
    "E:": "\\\\{ip}\\e$"
  },
  "accounts": {
    "*": "nrkp_2026_cluster"
  }
}
```

The machines are the nodes `nrkp-gui` and `nrkp-node1` … `nrkp-node6` from `data/copy/nodes`. Their addresses (`192.168.0.10` … `192.168.0.16`) are placeholders: set the real addresses once per computer under **Settings → Nodes**. With the output directory `D:\Renders\NRKP_Show_2026` and `nrkp-node1` at `10.10.0.11`, the slice writes `D:\Renders\NRKP_Show_2026\Node1\show.mp4`, which is copied to `\\10.10.0.11\media\NRKP_Show_2026\show.mp4`. The same applies to `GUI` and the other nodes. If the share is not called `media` on your machines, use **Save as…** for your own copy with a different destination.

All outputs copy with the account `nrkp_2026_cluster`. Store an account with that name under **Settings → Accounts**, or clear the default rule's account (**Current Windows user**, or **From settings** when a default or cluster account is set) if the shares are open to you or you use another account. An output can override the default with its own account, e.g. `"Node6": "nrkp_node6_admin"`.

## Manifest format

```json
{
  "format": "c-opy-manifest",
  "version": 1,
  "generator": "C-Slice 1.0.2",
  "created": "2026-10-18T14:03:12",
  "jobName": "show",
  "configuration": "D:\\C-Slice\\data\\configs\\nrkp_stereo_2026.json",
  "outputRoot": "D:\\Renders\\show",
  "outputs": [
    { "identifier": "Node1", "sourceDirectory": "D:\\Renders\\show\\Node1", "filePattern": "show.mp4" },
    { "identifier": "Node2", "sourceDirectory": "D:\\Renders\\show\\Node2", "filePattern": "show.mp4" }
  ]
}
```

For image-sequence codecs (PNG, JPEG, TGA), `filePattern` is `<name>_*.<ext>`. A relative `sourceDirectory` is resolved against the manifest's folder. If an absolute `sourceDirectory` no longer exists but `<manifest folder>/<identifier>` does, C-Opy uses that folder instead, so you can move an output folder together with its manifest.

## Settings and cluster files

C-Opy stores its settings like C-Slice does, in a KConfig file: `C-Opy\copy.conf` in your local application data folder (`%LOCALAPPDATA%\C-Opy\copy.conf` on Windows). The full path is shown in the window footer. The file holds the robocopy profiles chosen for files and folders, the [startup options](#settings), this computer's node (`LocalNode`, when not detected automatically), **Parallel copies**, **Start automatically**, the cluster and node folders (`ClusterFolder`, `NodeFolder`, when not the default), the current cluster, the last folders used by the file dialogs, your saved clusters and your working destinations. Library clusters and nodes stay in their own files:

```ini
[ApplicationSettings]
AutoStart=true
CurrentCluster=Dome
FolderRobocopyProfile=Many small files
StartupCluster=Dome

[SystemSettings]
LastManifestDialogLocation=D:\\Renders\\show

[CurrentOutputs][*]
Account=dome
Destinations=\\\\\\\\{identifier}\\\\media\\\\{job}

[CurrentOutputs][Node1]
Node=dome-node1

[Clusters][Dome]
Description=

[Clusters][Dome][Outputs][*]
Account=dome
Destinations=\\\\\\\\{identifier}\\\\media\\\\{job}
```

Settings left at their default are not written. KConfig escapes backslashes, and destination lists escape them once more, so a UNC path such as `\\node1\media` is written with four times as many backslashes; edit clusters in C-Opy rather than by hand.

To share clusters between machines, put JSON cluster files in the cluster folder, or **Import…** a file to copy its clusters into the settings file. A cluster file holds a `clusters` array, or a single cluster object. `nodes` (output identifier → node name), `manifestDestinations` (destinations of the manifest outputs without their own, ahead of the default rule `*`), `driveShares` (drive → share on every node) and `description` are optional:

```json
{
  "clusters": [
    {
      "name": "Dome",
      "manifestDestinations": ["\\\\{ip}\\e$\\Videos\\Splits\\{folder}"],
      "destinations": {
        "*": ["\\\\{identifier}\\media\\{job}"],
        "Node1": ["\\\\{ip}\\media\\{job}", "\\\\{ip:dome-backup}\\shows\\{job}\\Node1"]
      },
      "nodes": { "Node1": "dome-node1" },
      "accounts": { "*": "dome" }
    }
  ]
}
```
