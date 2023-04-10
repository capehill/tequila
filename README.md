# Tequila

Tequila is a simple CPU profiler which samples current running tasks. At the moment it's not very
accurate or feature-rich, but it will detect if some task is using CPU.


## Command-line parameters / icon tooltypes

SAMPLES [99, 10000] - how often running task is sampled (Hz). Default is 999.

INTERVAL [1, 5] - display update interval (seconds). Default is 1.

DEBUG - some additional logging.

PROFILE - try to collect symbol data. Note: it doesn't work properly yet.

GUI - start in window mode.


## Keyboard shortcuts

Control-C: quit in shell mode.

ESC: quit in GUI mode.


## License

Tequila is under PUBLIC DOMAIN.


## FAQ

Q) Is it compatible with CPU dockies, CPU Watcher and similar?

A) Should be, Tequila doesn't busyloop or spawn any "idler" tasks.

Q) idle.task is spending all my CPU!

A) Yes, that's normal when system is idling, when there are no more important tasks running.


## See also

CPU Watcher: http://os4depot.net/?function=showfile&file=utility/workbench/cpuwatcher.lha

Hieronymus: http://os4depot.net/?function=showfile&file=development/debug/hieronymus.lha

Profyler: http://os4depot.net/?function=showfile&file=development/debug/profyler.lha

