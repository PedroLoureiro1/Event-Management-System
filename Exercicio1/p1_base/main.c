#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include <linux/limits.h>

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

    // Dir arg
    char *path = argv[1];
  // Open the directory stream
    DIR *dir = opendir(path);

    // Check if the directory stream was opened successfully
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

  if (argc > 2) {
    char *endptr;
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

// Process the contents of the directory using readdir
  struct dirent *entry;
  int inputFile, outputFile;
  while ((entry = readdir(dir)) != NULL) {//iterar sobre files da mesma dir
    // Use basename to get the filename
    const char *filename = basename(entry->d_name);

    // Check if the filename has a .jobs extension
    if (strstr(filename, ".jobs") != NULL) {

        if (ems_init(state_access_delay_ms)) {
          fprintf(stderr, "Failed to initialize EMS\n");
          return 1;
        }

        char *filenameCopy = strdup(filename);
        char *outputFileName = strtok(filenameCopy, ".");
        char inputPath[PATH_MAX];
        char outputPath[PATH_MAX];
        snprintf(inputPath, sizeof(inputPath), "%s/%s", path, entry->d_name);
        snprintf(outputPath, sizeof(outputPath), "%s/%s.out", path, outputFileName);

        inputFile = open(inputPath, O_RDONLY);
        if (inputFile == -1) {
          perror("Erro ao abrir o arquivo de entrada");
          exit(EXIT_FAILURE);
        }

        outputFile = open(outputPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (outputFile == -1) {
          perror("Erro ao abrir ou criar o arquivo de saída");
          exit(EXIT_FAILURE);
        }

        // Seek to the beginning of the file
        if (lseek(inputFile, 0, SEEK_SET) == -1) {
          perror("Erro ao posicionar o ponteiro do arquivo");
          close(inputFile);
          close(outputFile);
          exit(EXIT_FAILURE);
        }  
      

      while (1) { //Iterate each file command       
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
      ems_terminate();
      close(inputFile);
      close(outputFile);
      free(filenameCopy);
    }
  }
  closedir(dir);
  return 0;
}
