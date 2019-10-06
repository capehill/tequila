# Tequila

Tequila is a simple CPU profiler which samples current running tasks. At the moment it's not very
accurate, but it will detect if some task is using CPU.

It was written for both fun and need; there aren't too many of such tools for AmigaOS 4.


## Command-line params

SAMPLES: 99 - 10000, meaning how often running task is sampled


## License

Tequila is under PUBLIC DOMAIN.


## FAQ

Q) Is it compatible with CPU dockies, CPU Watcher and similar?
  A) Should be, Tequila doesn't busyloop or spawn any "idler" tasks

Q) idle.task is spending all my CPU!
  A) Yes, that's normal when system is idling, when there are no more important tasks running
