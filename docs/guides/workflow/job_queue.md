---
title: Job queue
layout: home
nav_order: 4
parent: Slice workflow
---

# Job queue

C-Slice provides two job queuing mechanisms for batch processing multiple slice jobs. The internal queue runs locally within C-Slice, while the C-Job integration enables networked job scheduling across multiple worker programs.

## Internal queue

The internal queue stores jobs locally with all parameters preserved between executions. Use it when you need to process a sequence of different slice configurations without manually re-entering settings for each job.

### Adding jobs to the queue

1. Configure your desired slice settings (input, output, mapping, encoding).
2. Press **Queue** (or call `queueJob()`) to add the current configuration as a new job to the internal queue.
3. Repeat steps 1-2 for each additional job you want to queue.

### Managing queued jobs

The internal queue supports the following operations:

| Operation | Description |
|-----------|-------------|
| **Reorder** | Drag and drop items in the queue list to change execution order, or call `reorderInternalQueue(start, destination)` |
| **Remove** | Remove a specific job by index using `removeJobFromInternalQueue(index)` |
| **Launch individually** | Start a specific queued job at any time using `launchJobAtQueueIndex(index)` |
| **Save queue** | Export the entire queue to a JSON file with `saveInternalQueue(filePath)` |
| **Load queue** | Import a previously saved queue from a JSON file with `loadInternalQueue(filePath)` |

### Running the internal queue

Enable auto-processing by setting `internalQueueRunning` to `true` (or calling `setInternalQueueRunning(true)`). When enabled:

1. C-Slice launches the first job in the queue.
2. After the job completes, the next job is automatically loaded and launched.
3. The `runningJobIndex` property tracks which job is currently executing.
4. When all jobs are complete, `internalQueueRunning` returns to `false`.

### Editing queued jobs

Before a queued job starts, you can modify its settings:

1. Call `beginQueuedJobEdit(index)` to start editing a specific job. This saves the current settings and loads the job's stored parameters.
2. Modify any settings in the UI — they will apply to this queued job.
3. Call `saveQueuedJobEdit()` to commit changes, or `cancelQueuedJobEdit()` to restore the original settings.

The `editingQueuedJob` property indicates whether an edit is in progress, and `editedQueuedJobName` shows which job is being edited.

## C-Job integration

C-Job is a generic job scheduler that manages job queues across multiple worker programs using a client-server architecture. When enabled, C-Slice connects to the C-Job server as a worker client.

### Architecture

```
┌──────────────┐                    ┌──────────────┐
│   C-Job      │  job_id only       │   C-Slice    │
│   Server     │<==================>│   Client     │
│              │                    │              │
│ m_jobs:      │                    │ m_storedJobs │
│  {id → Info} │                    │  {id → Params}│
│  (no params) │                    │  (full data) │
└──────────────┘                    └──────────────┘
```

C-Job stores only job metadata (ID, name, status). Full job parameters remain stored locally in C-Slice's `m_storedJobs` map. When C-Job launches a job, it sends only the unique job ID — C-Slice retrieves its own parameters by that ID.

### Communication protocol

C-Slice and C-Job communicate over a **local TCP socket** using newline-delimited JSON messages:

```json
{"type":"register_instance","instance_id":"C-Slice-12345"}
{"type":"submit_job","job_id":"job-a1b2c3d4e5f6","job_name":"..."}
{"type":"launch_job","job_id":"job-a1b2c3d4e5f6"}
{"type":"complete_job","job_id":"job-a1b2c3d4e5f6"}
```

### Message types

| Direction | Type | Description |
|-----------|------|-------------|
| Client → Server | `register_instance` | Worker registers itself with a unique instance ID when connecting |
| Client → Server | `submit_job` | Worker submits a job to the queue with its unique ID and descriptive name |
| Client → Server | `progress_update` | Worker reports progress for a running job |
| Client → Server | `complete_job` | Worker notifies that a job has finished processing |
| Server → Client | `launch_job` | Server tells a worker to start processing a specific job by ID |
| Server → Client | `job_complete` | Server confirms job completion in the queue |

### Enabling C-Job

Set `cjobEnabled` to `true` (or call `setCJobEnabled(true)`) to connect C-Slice as a worker client:

1. C-Slice creates a `QLocalSocket` and connects to the server named `"C-Job"`.
2. On connection, it sends a registration message with a unique instance ID like `"C-Slice-42891"`.
3. The server registers the socket in its instance map.

Check connection status with `isCJobConnected()`.

### Submitting jobs to C-Job

When C-Job is enabled, calling `queueJob()` submits the job to both the internal queue and the C-Job server:

1. A unique UUID is generated as the job ID (e.g., `"job-a1b2c3d4e5f6"`).
2. Full job parameters are stored locally in `m_storedJobs[jobId]`.
3. Only `{job_id, job_name}` is sent to the C-Job server — **no parameters** are transmitted.

You can also submit directly using `submitToCJob(jobId)`.

### Queue processing flow

1. **Server**: When `setQueueRunning(true)` is called on the C-Job server, it takes the first job ID from its queue.
2. **Instance selection**: The server finds which instance submitted the job, or falls back to any available connected worker.
3. **Launch command**: Server sends only the job ID: `{"type": "launch_job", "job_id": "job-a1b2c3d4e5f6"}`.
4. **Client**: C-Slice receives the message, looks up `m_storedJobs[jobId]`, applies those parameters, then launches the slice job.
5. **Completion**: When finished, C-Slice sends `complete_job` and removes the job from local storage.

### Sequential processing

C-Job processes jobs sequentially — one at a time:

- A `m_processing` flag prevents concurrent execution.
- The queue auto-starts the next job only after the current one completes.
- Jobs cannot be removed while actively processing.
- If no instances are available, the job is marked as "Failed" and the next job is attempted.

## JobInfo structure

Both queues store jobs using the `JobInfo` structure containing all slice parameters:

```cpp
struct JobInfo {
    QString jobId;              // Unique UUID identifier
    QString instanceId;         // Which instance submitted this job
    QString status;             // "Queued" | "Running" | "Complete" | "Failed" | "Cancelled"
    int progressCompleted = 0;  // Frames completed
    int progressTotal = 0;      // Total frames

    // Job parameters (copied from controller when queued)
    QString configuration;
    QString inputType;          // "Image sequence" or "Video"
    QString leftInput;
    QString rightInput;
    QString outputDirectory;
    QString outputName;
    bool stereo = false;
    bool upsideDown = false;
    int startIndex = 0;
    int stopIndex = 0;
    int steps = 1;
    int outputCount = 1;
    QStringList outputNames;
    QList<bool> outputEnabled;
    QString mappingMode;        // "Dome", "Sphere EQR", "Sphere EAC", "Plane"
    double surfaceRadius = 740.0;
    double surfaceFov = 165.0;
    QString codec;              // "H264", "H265", "FFV1", etc.
    int crf = 28;
    int cq = 22;
    int qscale = 12;
    int frameRateNum = 1;
    int frameRateDen = 30;
    // ... and all other slice settings
};
```

## When to use which queue

| Scenario | Recommended queue |
|----------|-------------------|
| Single machine, simple batch processing | Internal queue |
| Multiple machines sharing work | C-Job integration |
| Persistent job scheduling across sessions | C-Job server |
| Quick test without external dependencies | Internal queue |
| Production pipeline with dedicated scheduler | C-Job server |

## See also

- [Basic slice](basic_slice) — Workflow for a single slice job
- [Progress, command preview, and logs](monitoring) — Monitoring running jobs