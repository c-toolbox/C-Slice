# C-Job

A generic job scheduler for managing job queues across multiple worker programs.

## Overview

C-Job operates as a **client-server architecture** where:

- **C-Job (Server)** — A standalone Qt application that runs as a local TCP server using `QLocalServer`. It manages the job queue, tracks connected instances, and dispatches jobs by unique ID only.
- **Worker Programs (Clients)** — Applications like C-Stitch connect to C-Job via `QLocalSocket` to submit jobs and receive launch commands. Each worker owns its own job parameters and retrieves them locally when a job is launched.

## Design Principle: Generic Job IDs

C-Job does **not** store any job logic or parameters. It only tracks unique job identifiers. When C-Job launches a job, it sends the job's unique ID to trigger the correct program (e.g., C-Stitch), which then retrieves its own stored parameters by that ID.

## Communication Protocol

C-Job and workers communicate over a **local TCP socket** using newline-delimited JSON messages:

```json
{"type":"register_instance","instance_id":"C-Stitch-12345"}
{"type":"submit_job","job_id":"job-uuid","job_name":"..."}
{"type":"launch_job","job_id":"job-uuid"}
{"type":"complete_job","job_id":"job-uuid"}
```

## Building

### Prerequisites

- Qt 6.6 or later
- KDE Frameworks 6 (KF6 Kirigami, ColorScheme, Config, CoreAddons, I18n, IconThemes)
- CMake 3.25 or later

### Build Commands

```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --config Release
```

## License

GPL-3.0-or-later