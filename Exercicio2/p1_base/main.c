#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>


#include "constants.h"
#include "operations.h"
#include "parser.h"
#include <linux/limits.h>

int processFile(const char *inputPath, const char *outputPath) {
    // Open input file
    int inputFile = open(inputPath, O_RDONLY);
    if (inputFile == -1) {
        perror("Error opening input file");
        return EXIT_FAILURE;
    }

    // Open or create output file
    int outputFile = open(outputPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (outputFile == -1) {
        perror("Error opening or creating output file");
        close(inputFile);
        return EXIT_FAILURE;
    }

    // Seek to the beginning of the input file
    if (lseek(inputFile, 0, SEEK_SET) == -1) {
        perror("Error positioning file pointer");
        close(inputFile);
        close(outputFile);
        return EXIT_FAILURE;
    }

    while (1) {
        unsigned int event_id, delay;
        size_t num_rows, num_columns, num_coords;
        size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

        int commandResult = get_next(inputFile);

        if (commandResult == EOC) {
            break;
        }

        switch (commandResult) {
            case CMD_CREATE:
                if (parse_create(inputFile, &event_id, &num_rows, &num_columns) != 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }

                if (ems_create(event_id, num_rows, num_columns)) {
                    fprintf(stderr, "Failed to create event\n");
                }

                break;

            case CMD_RESERVE:
                num_coords = parse_reserve(inputFile, MAX_RESERVATION_SIZE, &event_id, xs, ys);

                if (num_coords == 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }

                if (ems_reserve(event_id, num_coords, xs, ys)) {
                    fprintf(stderr, "Failed to reserve seats\n");
                }

                break;

            case CMD_SHOW:
                if (parse_show(inputFile, &event_id) != 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }

                if (ems_show(event_id, outputFile)) {
                    fprintf(stderr, "Failed to show event\n");
                }

                break;

            case CMD_LIST_EVENTS:
            if (ems_list_events(outputFile)) {
              fprintf(stderr, "Failed to list events\n");
            }

            break;

          case CMD_WAIT:
            if (parse_wait(inputFile, &delay, NULL) == -1) {  // thread_id is not implemented
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            if (delay > 0) {
              ems_wait(delay);
            }

            break;

          case CMD_INVALID:
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            break;

          case CMD_HELP:
            printf(
                "Available commands:\n"
                "  CREATE <event_id> <num_rows> <num_columns>\n"
                "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                "  SHOW <event_id>\n"
                "  LIST\n"
                "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
                "  BARRIER\n"                      // Not implemented
                "  HELP\n");

            break;

          case CMD_BARRIER:  // Not implemented
          case CMD_EMPTY:
            break;

          case EOC:
            break;
        }
    }

    // Close the input and output files
    close(inputFile);
    close(outputFile);

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  int active_processes = 0;

    // Obtém o caminho da diretoria a partir dos argumentos da linha de comando
    char *path = argv[1];
  // Open the directory stream
    DIR *dir = opendir(path);

    // Check if the directory stream was opened successfully
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    // Max number of child processes
    char *max_proc_input = argv[2];
    int MAX_PROC = atoi(max_proc_input);


    if (MAX_PROC <= 0) {
      fprintf(stderr, "Invalid number of processes\n");
      return 1;
    }

  if (argc > 3) {
    char *endptr;
    unsigned long int delay = strtoul(argv[3], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

// Process the contents of the directory using readdir
  struct dirent *entry;
  //int inputFile, outputFile;
  while ((entry = readdir(dir)) != NULL) {//iterar sobre files da mesma dir
    // Use basename to get the filename
    const char *filename = basename(entry->d_name);

    // Check if the filename has a .jobs extension
    if (strstr(filename, ".jobs") != NULL) {
        char *filenameCopy = strdup(filename);
        char *outputFileName = strtok(filenameCopy, ".");
        char inputPath[PATH_MAX];
        char outputPath[PATH_MAX];
        snprintf(inputPath, sizeof(inputPath), "%s/%s", path, entry->d_name);
        snprintf(outputPath, sizeof(outputPath), "%s/%s.out", path, outputFileName);
      
        while (active_processes >= MAX_PROC) {
            // Wait for any child process to finish
            int status;
            pid_t childPid = wait(&status);
            if (childPid == -1) {
                perror("Error waiting for child process");
                exit(EXIT_FAILURE);
            }
            printf("Child process %d exited with status %d\n", childPid, WEXITSTATUS(status));
            active_processes--;
         }

        if(active_processes < MAX_PROC){
          pid_t pid = fork();
          active_processes++;

          if (pid == -1) {
                perror("Erro ao criar processo filho");
                exit(EXIT_FAILURE);
          } 
          else if (pid == 0){
            if (ems_init(state_access_delay_ms)) {
              fprintf(stderr, "Failed to initialize EMS\n");
              return 1;
            }
              int result = processFile(inputPath, outputPath);
              free(filenameCopy);
              ems_terminate();
              exit(result);
          }
        }
    
      // Close the input and output files
      free(filenameCopy);
    }
  }
    closedir(dir);
    printf("dir closed\n");
    // Wait for all child processes to complete
    int status;
    pid_t child_pid;

    while ((child_pid = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            printf("Child process %d exited with status %d\n", child_pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Child process %d terminated by signal %d\n", child_pid, WTERMSIG(status));
        } else {
            printf("Child process %d terminated with unknown status\n", child_pid);
        }
    }
  return 0;
}
