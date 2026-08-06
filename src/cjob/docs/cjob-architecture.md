---
title: C-Job Architecture
nav_order: 6
---

# C-Job Job Execution Architecture

This document describes how C-Job jobs are run in C-Stitch when the queue is launched from the C-Job application.

## Overview

C-Job is a **generic job scheduler** that manages job queues across multiple worker programs. It operates as a **client-server architecture** where:

- **C-Job (Server)** — A standalone Qt application (`src/cjobmain.cpp`) that runs as a local TCP server using `QLocalServer`. It manages the job queue, tracks connected instances, and dispatches jobs by unique ID only.
- **Worker Programs (Clients)** — Applications like C-Stitch connect to C-Job via `QLocalSocket` to submit jobs and receive launch commands. Each worker owns its own job parameters and retrieves them locally when a job is launched.

## Design Principle: Generic Job IDs

C-Job does **not** store any job logic or parameters. It only tracks unique job identifiers. When C-Job launches a job, it sends the job's unique ID to trigger the correct program (e.g., C-Stitch), which then retrieves its own stored parameters by that ID.

```
┌──────────────┐                    ┌──────────────┐
│   C-Job      │  job_id only       │   C-Stitch   │
│   Server     │<==================>│   Client     │
│              │                    │              │
│ m_jobs:      │                    │ m_storedJobs │
│  {id → Info} │                    │  {id → Params}│
│  (no params) │                    │  (full data) │
└──────────────┘                    └──────────────┘
```

## Communication Protocol

C-Job and C-Stitch communicate over a **local TCP socket** using newline-delimited JSON messages:

```
{"type":"register_instance","instance_id":"C-Stitch-12345"}
{"type":"submit_job","job_id":"job-1690000000000","job_name":"..."}
{"type":"launch_job","job_id":"job-1690000000000"}
{"type":"complete_job","job_id":"job-1690000000000"}
```

### Message Types (Client → Server)

| Type | Description |
|------|-------------|
| `register_instance` | Worker registers itself with a unique instance ID when connecting |
| `submit_job` | Worker submits a job to the queue with only its unique ID and descriptive name |
| `progress_update` | Worker reports progress for a running job |
| `complete_job` | Worker notifies that a job has finished processing |

### Message Types (Server → Client)

| Type | Description |
|------|-------------|
| `launch_job` | Server tells a worker to start processing a specific job by ID |
| `job_complete` | Server confirms job completion in the queue |

## Job Queue Lifecycle

### 1. C-Job Server Startup

```
cjobmain.cpp → CJobApplication::run() → CJobServer::startListening("C-Job")
```

The server starts a `QLocalServer` listening on the name `"C-Job"`. It logs connections and manages registered instances.

### 2. Worker Connection & Registration

When a worker (e.g., C-Stitch) enables C-Job mode (`setCJobEnabled(true)`):

1. `StitchController::connectToCJobServer()` creates a `QLocalSocket` connecting to `"C-Job"`
2. On connection, `sendRegisterInstance()` sends a registration message with a unique instance ID like `"C-Stitch-42891"`
3. The server registers the socket in its `m_instances` map

### 3. Job Submission (Generic IDs Only)

When a user queues a job from C-Stitch:

1. A **unique random UUID** is generated as the job ID: `"job-<uuid>"`
2. Full job parameters are stored locally in `StitchController::m_storedJobs[jobId]`
3. If C-Job is enabled, only `{job_id, job_name}` is submitted to the server — **no parameters**

```json
{"type": "submit_job", "instance_id": "...", "job_id": "job-a1b2c3d4e5f6", "job_name": "MyScene - 100 frames @ 2048x2048"}
```

### 4. Queue Processing (Launch by ID)

When the user starts the queue in C-Job (`setQueueRunning(true)`):

1. **Server**: `CJobServer::startNextJob()` takes the first job ID from `m_jobQueue`
2. **Instance Selection**: The server finds which instance submitted the job, or falls back to any available connected instance
3. **Launch Command**: Server sends only the job ID:
   ```json
   {"type": "launch_job", "job_id": "job-a1b2c3d4e5f6"}
   ```
4. **Client**: C-Stitch receives the message, looks up `m_storedJobs[jobId]`, applies those parameters, then calls `launchStitch()`

### 5. Job Completion & Cleanup

When the worker finishes processing:

1. Worker sends a `complete_job` message with the job ID
2. Server receives it via `CJobSocket::processMessage()`, which calls `CJobServer::completeCurrentJob()`
3. Server marks the job as "Complete", clears `m_currentJobId`, and sets `m_processing = false`
4. If queue is still running, `startNextJob()` is called for the next queued job
5. **Client**: Worker removes the job from its local storage: `m_storedJobs.remove(jobId)`

## Key Data Structures

### CJobServer (Server-side) — Generic Job Tracking Only

```cpp
struct JobInfo {
    QString instanceId;   // Which instance submitted this job
    QString jobName;      // Display name only
    QString status;       // "Queued" | "Running" | "Complete" | "Failed" | "Cancelled"
    int completed = 0;    // Frames completed
    int total = 0;        // Total frames
};

// Server state (no job parameters stored):
QVector<QString> m_jobQueue;      // Ordered queue of job IDs only
QString m_currentJobId;           // Currently processing job ID
bool m_processing = false;        // Whether a job is active
bool m_queueRunning = false;      // Auto-start next job flag
QMap<QString, JobInfo> m_jobs;    // All tracked jobs (metadata only)
QMap<QString, CJobSocket*> m_instances;  // Connected worker instances
```

### Worker Local Storage (e.g., StitchController::m_storedJobs) — Full Parameter Ownership

Each worker program stores its own job parameters keyed by the unique job ID:

```cpp
// C-Stitch example:
QMap<QString, JobInfo> m_storedJobs;  // jobId → full parameters

struct JobInfo {
    QString jobId;                    // Unique UUID identifier
    QString jobName;                  // Display name
    // ... all stitch parameters ...
    QString leftFace, rightFace, ...; // Face paths
    int resolution, cubemapResolution;
    bool stereo, alpha, fxaa, cubic;
    // ... etc.
};
```

**This design means:**
- C-Job is completely generic — it doesn't know about stitch parameters
- Other programs can use C-Job by implementing their own `m_storedJobs` and parameter retrieval logic
- The same job ID works across the system: submitted → queued → launched → completed

## Queue Flow Diagram

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│  C-Stitch   │────────>│   C-Job      │<────────│  C-Stitch   │
│  (Instance 1)│submit   │   Server     │ launch  │  (Instance N)│
│              │────────>│              │────────>│             │
└─────────────┘         └──────────────┘         └─────────────┘
        │                         │                         │
        │    submit_job           │                         │
        │────────────────────────>│                         │
        │                         │  startNextJob()         │
        │                         │  (pick next in queue)   │
        │                         │                         │
        │                         │  launch_job             │
        │                         │────────────────────────>│
        │                         │                         │
        │                         │    launchStitch()       │
        │                         │                         │  (process frames)
        │                         │                         │
        │    complete_job         │                         │
        │<────────────────────────│                         │
        │                         │  completeCurrentJob()   │
        │                         │  (mark done, try next)  │
```

## Sequential Processing Model

C-Job processes jobs **sequentially** — one job at a time:

- `m_processing` flag prevents concurrent execution
- Queue auto-starts the next job only after the current one completes
- Jobs cannot be removed while actively processing
- If no instances are available, the job is marked as "Failed" and the next job is attempted

## C-Stitch Internal Queue (Fallback)

When C-Job is **disabled**, C-Stitch has its own internal queue (`m_queue` in `StitchController`) that:

- Stores jobs locally with all parameters
- Supports drag-and-drop reordering via `reorderInternalQueue()`
- Auto-starts next job when `setInternalQueueRunning(true)` is called
- Launches each job by applying its parameters, calling `launchStitch()`, then restoring original settings

## Files Reference

| File | Role |
|------|------|
| `src/cjobmain.cpp` | C-Job entry point |
| `src/cjobapplication.h/cpp` | C-Job application setup, server creation |
| `src/jobqueue/cjobserver.h/cpp` | Server: queue management, instance tracking |
| `src/jobqueue/cjobsocket.h/cpp` | Client socket wrapper for communication |
| `src/jobqueue/jobqueuemodel.h/cpp` | QML model for job queue display |
| `src/stitchcontroller.h/cpp` | C-Stitch: C-Job client integration, job submission |