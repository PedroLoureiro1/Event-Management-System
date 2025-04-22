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
#include <pthread.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include <linux/limits.h>

// Structure to hold the thread arguments (id , input fd, output fd, array for wait/barrier)
struct ThreadArgs {
    int threadID;
    int inputFile;
    int outputFile;
    int *threadArray;
};

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t accessMemoryLock = PTHREAD_RWLOCK_INITIALIZER;

// function that processes every command in a .jobs file
int processFile(int fd, int fdOutput, int threadId, int* threadArray) {
    while (1) {
        unsigned int event_id, delay, wait_threadID;
        size_t num_rows, num_columns, num_coords;
        size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
        
        pthread_mutex_lock(&mutex); 
        int waitTime = threadArray[threadId]; //flag do wait
        if (waitTime != 0){
          ems_wait((unsigned int)waitTime);
          threadArray[threadId] = 0;
        }
        
        if (threadArray[0]){ //flag do barrier
          pthread_mutex_unlock(&mutex);    
          return 2;
        }

        int commandResult = get_next(fd);

        if (commandResult == EOC) {
            pthread_mutex_unlock(&mutex);
            break; 
        }

        switch (commandResult) {
            case CMD_CREATE:
                if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
                    fprintf(stderr, "Invalid create command. See HELP for usage\n");
                    pthread_mutex_unlock(&mutex);
                    continue;
                }
                pthread_mutex_unlock(&mutex);
                if (ems_create(event_id, num_rows, num_columns)) {
                    fprintf(stderr, "Failed to create event\n");
                }
                break;

            case CMD_RESERVE:
                num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
                pthread_mutex_unlock(&mutex);

                if (num_coords == 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }
                if (ems_reserve(event_id, num_coords, xs, ys)) {
                    fprintf(stderr, "Failed to reserve seats\n");
                }
                break;

            case CMD_SHOW:
                if (parse_show(fd, &event_id) != 0) {
                    pthread_mutex_unlock(&mutex);
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                    continue;
                }
                pthread_mutex_unlock(&mutex);
                if (ems_show(event_id, fdOutput)) {
                    fprintf(stderr, "Failed to show event\n");
                }
                break;

            case CMD_LIST_EVENTS:
                pthread_mutex_unlock(&mutex);
                if (ems_list_events(fdOutput)) {
                  fprintf(stderr, "Failed to list events\n");
                }
                break;

            case CMD_WAIT:
                if (parse_wait(fd, &delay, &wait_threadID) == -1) {  
                  fprintf(stderr, "Invalid command. See HELP for usage\n");
                  continue;
                }
                
                if (delay > 0) {
                    if (wait_threadID > 0){
                        threadArray[wait_threadID] = (int) delay;
                        pthread_mutex_unlock(&mutex);
                    }
                    else{
                        ems_wait(delay);
                        pthread_mutex_unlock(&mutex);
                    }
                }
                break;

            case CMD_INVALID:
                fprintf(stderr, "Invalid command 8. See HELP for usage\n");
                pthread_mutex_unlock(&mutex);
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

            case CMD_BARRIER:  
                threadArray[0] =  1; // This index will be used if a Barrier is found
                pthread_mutex_unlock(&mutex);
                return 2; // diff value than 0, means a barrier happened

            case CMD_EMPTY:
                pthread_mutex_unlock(&mutex);
                break;
        }
    }
    // Close the input and output files
    return EXIT_SUCCESS;
}

// Function definition for the thread function
void* processFileThread(void* arg) {
    // Cast the argument back to its original type
    struct ThreadArgs* threadArgs = (struct ThreadArgs*)arg;

    pthread_rwlock_rdlock(&accessMemoryLock);
    int result = processFile(threadArgs->inputFile, threadArgs->outputFile, threadArgs->threadID, threadArgs->threadArray);
    pthread_rwlock_unlock(&accessMemoryLock);

    pthread_exit((void*)(intptr_t)result);
}

int main(int argc, char *argv[]) {
    unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
    int active_processes = 0;

    // Obtain the dir arg
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

    // Max number of threads
    char *max_threads_input = argv[3];
    int MAX_THREADS = atoi(max_threads_input);
    int threadArray[MAX_THREADS + 1]; //a primeira thread tem o ID 1
    // Initialize the shared array before creating threads
    for (int i = 0; i < MAX_THREADS + 1; ++i) {
        threadArray[i] = 0;
    }

    if (MAX_PROC <= 0) {
      fprintf(stderr, "Invalid number of processes\n");
      return 1;
    }

    if (argc > 4) {
        char *endptr;
        unsigned long int delay = strtoul(argv[4], &endptr, 10);

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
                // Now, create threads within the child process
                pthread_t tid[MAX_THREADS];
                int threadResults[MAX_THREADS]; // Array to store results from each thread
                struct ThreadArgs args[MAX_THREADS];

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
                for (int i = 0; i < MAX_THREADS; ++i) {
                    // Prepare arguments for the thread
                    args[i].threadID = i + 1;  // Set the thread identifier
                    args[i].inputFile = inputFile;
                    args[i].outputFile = outputFile;
                    args[i].threadArray = threadArray;
                    // Create threads here 
                    if (pthread_create(&tid[i], NULL, processFileThread, (void*)&args[i]) != 0) {
                        perror("Error creating thread");
                        exit(EXIT_FAILURE);
                    }
                }
                int result = EXIT_SUCCESS;

              while (1){
                  int barrierFound = 0; //not found
                  // Wait for all threads to finish
                  for (int i = 0; i < MAX_THREADS; ++i) {
                      pthread_join(tid[i], (void**)&threadResults[i]);
                  }
                
                // Use the results from threads and the final result to determine the exit status
                  for (int j = 0; j < MAX_THREADS; ++j) {
                      if (threadResults[j] != EXIT_SUCCESS) {
                          barrierFound = 1;
                          threadArray[0] = 0;
                          for (int i = 0; i < MAX_THREADS; ++i) {
                              // Prepare arguments for the thread
                              args[i].threadID = i + 1;  // Set the thread identifier
                              args[i].inputFile = inputFile;
                              args[i].outputFile = outputFile;
                              args[i].threadArray = threadArray;
                              // Create threads here 
                              if (pthread_create(&tid[i], NULL, processFileThread, (void*)&args[i]) != 0) {
                                  perror("Error creating thread");
                                  exit(EXIT_FAILURE);
                              }
                          }
                      }
                  break;  // Exit loop if any thread had an error
                  }
                  if (!barrierFound) //No more BARRIERS
                      break;
              }
              free(filenameCopy);
              close(inputFile);
              close(outputFile);
              ems_terminate();
              exit(result);
          }
      }
    
      // Close the input and output files
      free(filenameCopy);
    }
  }
      closedir(dir);
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