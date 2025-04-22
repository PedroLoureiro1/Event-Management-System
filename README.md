IST Event Management System (IST-EMS)
This project is part of the Operating Systems course at Instituto Superior Técnico (2023-24). It consists of developing a multi-process event management system capable of handling commands from input files, with support for concurrent processing and process control using low-level POSIX interfaces.

The development is split into two main parts:

 Part 1 — Batch Command Processing from Files
The first part of the project focuses on adapting the system to read and execute commands from files rather than from the terminal.

 Main Features:
Reading .jobs Files:
The program takes a directory path as input and processes all files with the .jobs extension. Each file contains a sequence of commands to manage events.

Sequential Execution:
Commands inside each .jobs file are executed in order, simulating the manual command-line interaction from the earlier phase of the project.

Generating Output Files:
For each input file, an output file with the same name and the .out extension is created, containing the final state of all events after processing.

Use of POSIX File Descriptors:
File operations are performed exclusively using POSIX system calls (open, read, write, etc.), avoiding high-level C libraries like stdio.h.

 Part 2 — Parallel Processing with Multiple Processes
The second part introduces parallelism to improve performance by processing multiple input files simultaneously.

 Main Features:
Forking Child Processes:
A new child process is spawned for each .jobs file to execute its commands independently.

Process Limit Control (MAX_PROC):
The maximum number of concurrent child processes is limited by a constant MAX_PROC, which can be specified via the command line.

Independent Event Assumption:
Each .jobs file is assumed to contain commands that operate on distinct events, which removes the need for inter-process synchronization.

Process Monitoring:
The parent process waits for all child processes to finish and logs their termination status to the terminal.

This two-part structure enables a clean progression from basic batch processing to scalable multi-process execution, highlighting key concepts in operating systems such as file I/O, process management, and concurrency.
