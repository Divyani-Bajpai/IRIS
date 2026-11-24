# Member 2: Disk and process monitoring

Member 2's production collector is the `DiskProcessMonitor` class in `include/iris/disk_process_monitor.h` and `src/disk_process_monitor.cpp`. The root CMake target compiles these files directly. The older one-off programs were consolidated into this reusable collector; each call to `collect()` returns timestamped `MetricSample` values and warnings for the shared SQLite store.

## Live measurements

| Metric | Scope | Unit | Collection method |
|---|---|---:|---|
| `disk_queue_length` | All physical disks | requests | Windows PDH physical disk counter |
| `percent_disk_time` | All physical disks | percent | Windows PDH physical disk counter |
| `disk_read_mb_per_sec`, `disk_write_mb_per_sec` | Logical disks total | MiB/s | Windows PDH logical disk transfer counters |
| `disk_used_percent`, `disk_free_gb` | Combined fixed drives | percent, GiB | Windows drive capacity APIs |
| `process_cpu_percent` | Individual process | percent of total machine capacity | Difference between two Windows process CPU-time samples |
| `process_memory_mb` | Individual process | MiB working set | Windows process memory counters |
| `process_disk_read_mb_per_sec`, `process_disk_write_mb_per_sec` | Individual process | MiB/s | Difference between two Windows process I/O-counter samples |

Process CPU and I/O rates require two samples, so they are unavailable for a process until it survives across two collection cycles. The collector compares PID and process creation time to avoid confusing a restarted process with its previous instance. It drops processes that exit or cannot be opened. Access to some protected processes can be denied by Windows.

Process I/O counters report bytes attributed to a process by Windows; caching can make them differ from physical disk traffic. System-wide PDH counters show actual aggregate disk activity separately. Disk capacity values are combined across fixed drives, so the UI does not imply which individual volume is low on space.

## Assigned work status

- Process CPU math uses two samples and has separate unit tests.
- Memory is recorded in MiB with the metric name indicating memory.
- Exited/restarted processes are discarded safely between samples.
- The live collector test checks multiple temporary processes and memory/CPU readings.
- Disk values are returned as structured metrics, not printed directly by the collector.
- `app/main.cpp` writes Member 2 and Member 1 values with timestamps into the shared SQLite store every configured interval during each health check.
- The shared baseline and diagnosis modules consume system disk metrics; Member 2 does not create a competing schema or baseline implementation.

Tests are under the project-level `tests/` folder and use temporary fixtures only. Normal IRIS checks collect readings from the PC running the application.