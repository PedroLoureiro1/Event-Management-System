#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/io.h"
#include "eventlist.h"
#include "common/constants.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_us = 0;

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @param from First node to be searched.
/// @param to Last node to be searched.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id, struct ListNode* from, struct ListNode* to) {
  struct timespec delay = {0, state_access_delay_us * 1000};
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id, from, to);
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_us) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_us = delay_us;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  free_list(event_list);
  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  if (get_event_with_delay(event_id, event_list->head, event_list->tail) != NULL) {
    fprintf(stderr, "Event already exists\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  if (pthread_mutex_init(&event->mutex, NULL) != 0) {
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }
  event->data = calloc(num_rows * num_cols, sizeof(unsigned int));

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event->data);
    free(event);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwl);
  printf("fiz o create\n");
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }

  for (size_t i = 0; i < num_seats; i++) {
    if (xs[i] <= 0 || xs[i] > event->rows || ys[i] <= 0 || ys[i] > event->cols) {
      fprintf(stderr, "Seat out of bounds\n");
      pthread_mutex_unlock(&event->mutex);
      return 1;
    }
  }

  for (size_t i = 0; i < event->rows * event->cols; i++) {
    for (size_t j = 0; j < num_seats; j++) {
      if (seat_index(event, xs[j], ys[j]) != i) {
        continue;
      }

      if (event->data[i] != 0) {
        fprintf(stderr, "Seat already reserved\n");
        pthread_mutex_unlock(&event->mutex);
        return 1;
      }

      break;
    }
  }

  unsigned int reservation_id = ++event->reservations;

  for (size_t i = 0; i < num_seats; i++) {
    event->data[seat_index(event, xs[i], ys[i])] = reservation_id;
  }

  pthread_mutex_unlock(&event->mutex);
  printf("reserve sucedido\n");
  return 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  int Invalid = -1;
  char errorBuffer[pipeBuffer];
  char showSizeBuffer[pipeBuffer];
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    snprintf(errorBuffer, sizeof(errorBuffer), "%d", Invalid);
    write(out_fd, errorBuffer, strlen(errorBuffer));
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    snprintf(errorBuffer, sizeof(errorBuffer), "%d", Invalid);
    write(out_fd, errorBuffer, strlen(errorBuffer));
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    snprintf(errorBuffer, sizeof(errorBuffer), "%d", Invalid);
    write(out_fd, errorBuffer, strlen(errorBuffer));
    return 1;
  }

  //snprintf(showSizeBuffer, sizeof(showSizeBuffer), "%d ", size);
  //write(out_fd, showSizeBuffer, strlen(showSizeBuffer));

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    snprintf(errorBuffer, sizeof(errorBuffer), "%d", Invalid);
    write(out_fd, errorBuffer, strlen(errorBuffer));
    return 1;
  }
   memset(errorBuffer, 0, sizeof(errorBuffer));
  size_t size = sizeof(int) + (2* sizeof(size_t)) + event->cols * event->rows * sizeof(unsigned int);
  snprintf(errorBuffer, sizeof(errorBuffer), "%zu", size);

  if (write(out_fd, errorBuffer, strlen(errorBuffer)) == -1) {
    perror("Error writing to file descriptor");
    pthread_mutex_unlock(&event->mutex);
    return 1;
  }

  // Alocar o buffer para enviar na resp_pipe
  size_t buffer_size = sizeof(int) + (2* sizeof(size_t)) + event->cols * event->rows * sizeof(unsigned int);
  char *buffer = (char *)malloc(buffer_size);
  unsigned int seats[event->cols * event->rows];

  // Adicionar 0 ao buffer (primeiros sizeof(int) bytes)
  int answer = 0;
  memcpy(buffer, &answer, sizeof(int));

  // Adicionar o número de eventos ao buffer (primeiros sizeof(size_t) bytes)
  memcpy(buffer + sizeof(int), &event->rows, sizeof(size_t));
  memcpy(buffer + sizeof(int) + sizeof(size_t), &event->cols, sizeof(size_t));

  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      // Use snprintf para escrever no buffer
      seats[(i - 1) * event->cols + (j - 1)] = event->data[seat_index(event, i, j)];
    }
  }

  memcpy(buffer + sizeof(int) + sizeof(size_t) + sizeof(size_t) , seats, (event->cols * event->rows) * sizeof(unsigned int)); 
  for (size_t j = 0; j < buffer_size; j++) {
    //printf("N:%u \n", buffer[j]);
  }
  if (write(out_fd, buffer, buffer_size) == -1) {
    perror("Error writing to file descriptor");
    free(buffer);
    pthread_mutex_unlock(&event->mutex);
    return 1;
  }
  
  pthread_mutex_unlock(&event->mutex);
  return 0;
}

int ems_list_events(int out_fd) {
  size_t num_events = 0;
  int Invalid = 1;
  char errorBuffer[pipeBuffer];
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct ListNode* to = event_list->tail;
  struct ListNode* current = event_list->head;

  if (current == NULL) {
    fprintf(stderr, "No events\n");
    snprintf(errorBuffer, sizeof(errorBuffer), "%d", Invalid);
    write(out_fd, errorBuffer, strlen(errorBuffer));
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  while (1) {
    num_events++;

    if (current == to) {
      break;
    }

    current = current->next;
  }

  // Alocar um buffer para armazenar 0, o número de eventos e o array 'ids'
  size_t buffer_size = sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int);
  char *buffer = (char *)malloc(buffer_size);
  unsigned int ids[num_events];

  int i = 0;

  to = event_list->tail;
  current = event_list->head;

  while (1) {

    ids[i] = current->event->id;

    if (current == to) {
      break;
    }
    current = current->next;
    i++;
  }

  // Adicionar 0 ao buffer (primeiros sizeof(int) bytes)
  int answer = 0;
  memcpy(buffer, &answer, sizeof(int));

  // Adicionar o número de eventos ao buffer (primeiros sizeof(size_t) bytes)
  memcpy(buffer + sizeof(int), &num_events, sizeof(size_t));

  // Adicionar o array 'ids' ao buffer (restantes bytes)
  memcpy(buffer + sizeof(int) + sizeof(size_t), ids, num_events * sizeof(unsigned int));

  //for (size_t j = 0; j < buffer_size; ++j) {
  //      printf("Evento : %u\n", (char)buffer[j]);
  //}
  //
  if (write(out_fd, buffer, buffer_size) == -1) {
    perror("Error writing to file descriptor");
    free(buffer);
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  free(buffer);

  pthread_rwlock_unlock(&event_list->rwl);
  printf("list done\n");
  return 0;
}
int ems_signal_show(int out_fd, unsigned int event_id) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }

  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      char buffer[16];
      sprintf(buffer, "%u", event->data[seat_index(event, i, j)]);

      if (print_str(out_fd, buffer)) {
        perror("Error writing to file descriptor");
        pthread_mutex_unlock(&event->mutex);
        return 1;
      }

      if (j < event->cols) {
        if (print_str(out_fd, " ")) {
          perror("Error writing to file descriptor");
          pthread_mutex_unlock(&event->mutex);
          return 1;
        }
      }
    }

    if (print_str(out_fd, "\n")) {
      perror("Error writing to file descriptor");
      pthread_mutex_unlock(&event->mutex);
      return 1;
    }
  }

  pthread_mutex_unlock(&event->mutex);
  return 0;
}


int ems_program_status(){
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct ListNode* to = event_list->tail;
  struct ListNode* current = event_list->head;

  if (current == NULL) {
    fprintf(stderr, "No events\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  printf("ola\n");
  while (1) {
    printf("Event ID: %d\n", current->event->id);
    ems_signal_show(STDOUT_FILENO, current->event->id);

    if (current == to) {
      break;
    }

    current = current->next;
  }

  return 0;
}