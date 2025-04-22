#include "api.h"
#include "common/constants.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int req_pipe;                  //
int resp_pipe;                 //current client specs
int received_session_id = -1;  //
char const* resp_path;
char const* req_path;

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
  //printf("resp_fd_main_client-\n");
  //TODO: create pipes and connect to the server
  // Create a request named pipe
  if (mkfifo(req_pipe_path, 0666) == -1) {
      perror("Error creating request named pipe");
      return 1;
  }
  // Create a response named pipe
  if (mkfifo(resp_pipe_path, 0666) == -1) {
      perror("Error creating response named pipe");
      unlink(req_pipe_path);  // Remove request pipe if response pipe creation fails
      return 1;
  }
  resp_path = resp_pipe_path;
  req_path = req_pipe_path;
  // Open the server pipe
  int server_pipe = open(server_pipe_path, O_WRONLY);
  if (server_pipe == -1) {
      perror("Error opening server pipe");
      unlink(req_pipe_path);
      unlink(resp_pipe_path);
      return 1;
  }
  // Send a setup request to the server
  char setup_request[pipeBuffer];  // Adjust the buffer size as needed
  snprintf(setup_request, pipeBuffer, "%s %s", req_pipe_path, resp_pipe_path);
  if (write(server_pipe, setup_request, sizeof(setup_request)) == -1) {
      perror("Error sending setup request to server");
      close(server_pipe);
      unlink(req_pipe_path);
      unlink(resp_pipe_path);
      return 1;
  }
  //close(server_pipe); ultima cena q fiz
  // Open the response and request pipes for communication
  req_pipe = open(req_pipe_path, O_WRONLY);
  resp_pipe = open(resp_pipe_path, O_RDONLY);
  if (req_pipe == -1 || resp_pipe == -1) {
      perror("Error opening pipes for communication");
      if (req_pipe != -1) close(req_pipe);
      if (resp_pipe != -1) close(resp_pipe);
      unlink(req_pipe_path);
      unlink(resp_pipe_path);
      return 1;
  }
  //while ((bytes_read = read(resp_pipe, &received_session_id, sizeof(received_session_id))) > 0) {
  //  printf("resp_fd_main_client- %d\n", resp_pipe);
  //  // TODO: Process the received data as needed
  //} 
  //ssize_t bytes_read = read(resp_pipe, &received_session_id, sizeof(received_session_id));
  read(resp_pipe, &received_session_id, sizeof(received_session_id));
  if (received_session_id != -1) {
      printf("End of file. Received session ID: %d\n", received_session_id);
      return 0;
      // End of file, pipe closed by sender
      // Handle accordingly
  }
  // TODO: Save session ID and any other necessary information
  return 1;  // Success
}

int ems_quit(void) { 
  //TODO: close pipes
  printf("entered quit\n");

  if (write(req_pipe, "2", 1) == -1) {
      perror("Error sending create request to server");
      return 1;
  }
  //falta ler na pipe resp a resposta do ems create para o cliente saber se obteve sucesso ou nao, se sim a funcao retorna 0
  //se nao retorna o erro que deu 'event already exists' por ex, esse erro sera passado pela pipe claro
  close(req_pipe);
  close(resp_pipe);
  // Unlink (delete) the named pipe
  if (unlink(resp_path) == -1) {
      perror("Error unlinking named pipe");
      exit(EXIT_FAILURE);
  }
  // Unlink (delete) the named pipe
  if (unlink(req_path) == -1) {
      perror("Error unlinking named pipe");
      exit(EXIT_FAILURE);
  }
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  //TODO: send create request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("entered create\n");
  char create_request[pipeBuffer];
  snprintf(create_request, pipeBuffer, " 3 %u %zu %zu", event_id, num_rows, num_cols);

  if (write(req_pipe, create_request, sizeof(create_request)) == -1) {
      perror("Error sending create request to server");
      return 1;
  }

  char bufferCreate[pipeBuffer];
  int answer;
  read(resp_pipe, bufferCreate, sizeof(answer));
  sscanf(bufferCreate, "%d", &answer);
  printf("answerCreate: %d\n", answer);

  if (answer == 1)
    return 1;

  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  //TODO: send reserve request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("entered reserve\n");
  //for (size_t i = 0; i < MAX_RESERVATION_SIZE; ++i) {
  //  printf("xs[%zu], ys[%zu]\n", xs[i], ys[i]);
  //}

  // Prepare the reserve request string
  char reserve_request[pipeBuffer];
  int offset = snprintf(reserve_request, pipeBuffer, " 4 %u %zu", event_id, num_seats);  
  // Append xs and ys to the request string
  for (size_t i = 0; i < num_seats; ++i) {
   offset += snprintf(reserve_request + offset, pipeBuffer - offset, " %zu %zu", xs[i], ys[i]);
  }
    printf("Reserve Request: %s\n", reserve_request);

  if (write(req_pipe, reserve_request, (size_t)offset) == -1) {
      perror("Error sending reserve request to server");
      return 1;
  }
  char bufferReSERVA[pipeBuffer];
  int answer;
  read(resp_pipe, bufferReSERVA, sizeof(answer));
  printf("buffer reserve fim:\n%s\n", bufferReSERVA);
  //if (answer == 0)
  //  return 0;
  //return 1;
  return 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("entered show\n");
  char show_request[pipeBuffer];
  snprintf(show_request, pipeBuffer, " 5 %u", event_id);

  if (write(req_pipe, show_request, sizeof(show_request)) == -1) {
      perror("Error sending show request to server");
      return 1;
  }

  memset(show_request, 0, sizeof(show_request));
  if (read(resp_pipe, show_request, sizeof(show_request)) == -1) {
      perror("Error sending show request to server");
      return 1;
  }
  size_t size = 0;
  sscanf(show_request, "%zu", &size);
  printf("anSIZEEEEEr: %zu\n", size);
  char show_buffer[500];
  //printf("ola syze: %zu\n",strlen(show_buffer));
  memset(show_buffer, 0, sizeof(show_buffer));
  
  if (read(resp_pipe, show_buffer, sizeof(show_buffer)) == -1) {
      perror("Error sending show request to server");
      return 1;
  }

  int answer;
  memcpy(&answer, show_buffer, sizeof(int));
  printf("answer: %d\n", answer);

  if (answer == 1){
    return 1;
  }

  size_t num_rows, num_cols;
  memcpy(&num_rows, show_buffer + sizeof(int), sizeof(size_t));
  memcpy(&num_cols, show_buffer + sizeof(int) + sizeof(size_t), sizeof(size_t));
  printf("rows/cols: %zu, %zu\n", num_rows, num_cols);


  // Aloca espaço para o array de IDs do evento
  unsigned int *seats = (unsigned int *)malloc(num_rows * num_cols * sizeof(unsigned int));
  if (seats == NULL) {
    fprintf(stderr, "Erro na alocação de memória\n");
    return 1;
  }
  //int size2 = sizeof(int) + (2* sizeof(size_t)) + num_rows * num_cols * sizeof(unsigned int);
  //for (size_t j = 0; j < (size2); j++) {
  //  //printf("N:%u \n", show_buffer[j]);
  //}

  // Lê os IDs do evento
  size_t bufferSize = num_rows * num_cols * sizeof(unsigned int) + (num_rows) * (num_cols -1) * strlen(" ") + (num_rows) * strlen("\n") ; 
  char showOutputBuffer[bufferSize];
  memset(showOutputBuffer, 0, sizeof(showOutputBuffer));

  size_t offset = 0;

  for (size_t i = 0; i < num_rows * num_cols; ++i) {
    memcpy(&seats[i], show_buffer + sizeof(int) + sizeof(size_t) + sizeof(size_t) + (i * sizeof(unsigned int)), sizeof(unsigned int));
    //printf("seats: %u\n",seats[i] );
  }
  for (size_t i = 1; i <= num_rows; i++) {
    for (size_t j = 1; j <= num_cols; j++) {
      offset += sprintf(showOutputBuffer + offset, "%u", seats[(i - 1) * num_cols + (j - 1)]);
      //printf("seats 218: %u\n",seats[(i - 1) * num_cols + (j - 1)] );
      if (j < num_cols){
        strcat(showOutputBuffer + offset, " ");
        offset+= strlen(" ");
      }
      else{
        strcat(showOutputBuffer + offset, "\n");
        offset+= strlen("\n");
      }
    }
  }
  printf("Buffer sHOW:\n%s\n", showOutputBuffer);

  if (write(out_fd, showOutputBuffer, offset) == -1) {
    perror("Error sending list request to server");
    free(seats);
    return 1;
  }

  free(seats);
  
  return 0;
}

int ems_list_events(int out_fd) {
  //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
  printf("entered list\n");
  char show_request[pipeBuffer];
  snprintf(show_request, pipeBuffer, " 6");

  if (write(req_pipe, show_request, sizeof(show_request)) == -1) {
      perror("Error sending show request to server");
      return 1;
  }

  char list_buffer[pipeBuffer];
  memset(list_buffer, 0, sizeof(list_buffer));
  
  if (read(resp_pipe, list_buffer, sizeof(list_buffer)) == -1) {
      perror("Error sending show request to server");
      return 1;
  }

  int answer;
  memcpy(&answer, list_buffer, sizeof(int));
  printf("answer: %d\n", answer);

  if (answer == 1){
    return 1;
  }

  size_t num_events;
  memcpy(&num_events, list_buffer + sizeof(int), sizeof(size_t));
  printf("answer: %zu\n", num_events);


  // Aloca espaço para o array de IDs do evento
  unsigned int *ids = (unsigned int *)malloc(num_events * sizeof(unsigned int));
  if (ids == NULL) {
    fprintf(stderr, "Erro na alocação de memória\n");
    return 1;
  }

  // Lê os IDs do evento
  size_t bufferSize = num_events * sizeof(unsigned int) + num_events * strlen("Event: \n"); 
  char listOutputBuffer[bufferSize];

  size_t offset = 0;

  // Loop para cada evento
  for (size_t i = 0; i < num_events; ++i) {
    memcpy(&ids[i], list_buffer + sizeof(int) + sizeof(size_t) + (i * sizeof(unsigned int)), sizeof(unsigned int));

    // Copiar "Event: " para o buffer de saída
    strcpy(listOutputBuffer + offset, "Event: ");

    // Atualizar o deslocamento
    offset += strlen("Event: ");

    // Converter o ID do evento para uma string e copiar para o buffer de saída
    offset += sprintf(listOutputBuffer + offset, "%u", ids[i]);

    // Adicionar uma quebra de linha
    strcpy(listOutputBuffer + offset, "\n");

    // Atualizar o deslocamento para a próxima linha
    offset += 1;
  }

  printf("Buffer list: %s\n", listOutputBuffer);

  if (write(out_fd, listOutputBuffer, offset) == -1) {
    perror("Error sending list request to server");
    free(ids);
    return 1;
  }

  free(ids);

  return 0;
}
