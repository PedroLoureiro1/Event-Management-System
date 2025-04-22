#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "eventlist.h"

#define BUFFER_SIZE 1024

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;
pthread_rwlock_t Lock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t outputLock = PTHREAD_RWLOCK_INITIALIZER;

//pthread_mutex_t Lock = PTHREAD_MUTEX_INITIALIZER;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->data[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_ms) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_ms = delay_ms;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  free_list(event_list);
  event_list = NULL;
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  pthread_rwlock_rdlock(&Lock);
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    pthread_rwlock_unlock(&Lock);
    return 1;
  }
  pthread_rwlock_unlock(&Lock);
  pthread_rwlock_rdlock(&Lock);
  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    pthread_rwlock_unlock(&Lock);
    return 1;
  }
  pthread_rwlock_unlock(&Lock);

  pthread_rwlock_rdlock(&Lock);
  struct Event* event = malloc(sizeof(struct Event));
  pthread_rwlock_unlock(&Lock);

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  event->data = malloc(num_rows * num_cols * sizeof(unsigned int));
  // Initialize the eventMutex after memory allocation
  if (pthread_mutex_init(&(event->eventMutex), NULL) != 0) {
    fprintf(stderr, "Error initializing mutex for event\n");
    free(event);
    return 1;
  }

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event);
    return 1;
  }

  for (size_t i = 0; i < num_rows * num_cols; i++) {
    pthread_rwlock_wrlock(&Lock);
    event->data[i] = 0;
    pthread_rwlock_unlock(&Lock);
  }

  pthread_rwlock_wrlock(&Lock);

  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    free(event->data);
    free(event);
    pthread_rwlock_unlock(&Lock);
    return 1;
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->data);
    free(event);
    pthread_rwlock_unlock(&Lock);
    return 1;
  }

  pthread_rwlock_unlock(&Lock);

  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {

  pthread_rwlock_rdlock(&Lock);
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    pthread_rwlock_unlock(&Lock);
    return 1;
  }
  pthread_rwlock_unlock(&Lock);

  pthread_rwlock_wrlock(&Lock);
  struct Event* event = get_event_with_delay(event_id);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    pthread_rwlock_unlock(&Lock);
    return 1;
  }
  pthread_rwlock_unlock(&Lock);
  pthread_mutex_lock(&event->eventMutex);

  pthread_rwlock_wrlock(&Lock);
  unsigned int reservation_id = ++event->reservations;
  pthread_rwlock_unlock(&Lock);

  size_t i = 0;
  for (; i < num_seats; i++) {
    size_t row = xs[i];
    size_t col = ys[i];

    pthread_rwlock_rdlock(&Lock);
    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      fprintf(stderr, "Invalid seat\n");
      pthread_rwlock_unlock(&Lock);
      break;
    }
    pthread_rwlock_unlock(&Lock);

    pthread_rwlock_rdlock(&Lock);
    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      fprintf(stderr, "Seat already reserved\n");
      pthread_rwlock_unlock(&Lock);
      break;
    }
    pthread_rwlock_unlock(&Lock);

    pthread_rwlock_wrlock(&Lock);
    *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
    pthread_rwlock_unlock(&Lock);
  }

  // If the reservation was not successful, free the seats that were reserved.
  if (i < num_seats) {
    event->reservations--;
    for (size_t j = 0; j < i; j++) {
      pthread_rwlock_rdlock(&Lock);
      *get_seat_with_delay(event, seat_index(event, xs[j], ys[j])) = 0;
      pthread_rwlock_unlock(&Lock);
    }
    pthread_mutex_unlock(&event->eventMutex);
    return 1;
  }
  pthread_mutex_unlock(&event->eventMutex);
  return 0;
}

int ems_show(unsigned int event_id, int fd) {
    pthread_rwlock_rdlock(&Lock);
    if (event_list == NULL) {
        fprintf(stderr, "EMS state must be initialized\n");
        pthread_rwlock_unlock(&Lock);
        return 1;
    }
    pthread_rwlock_unlock(&Lock);

    pthread_rwlock_rdlock(&Lock);
    struct Event* event = get_event_with_delay(event_id);
    pthread_rwlock_unlock(&Lock);

    if (event == NULL) {
        fprintf(stderr, "Event not found\n");
        return 1;
    }
    //pthread_mutex_lock(&Lock);
    pthread_rwlock_wrlock(&outputLock);

    char buffer[5 * event->cols * event->rows];  // Adjust the size as needed
    size_t buffer_len = 0;

    for (size_t i = 1; i <= event->rows; i++) {
        for (size_t j = 1; j <= event->cols; j++) {
            unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));

            // Use snprintf to format the seat content into seat_str
            char seat_str[5];
            pthread_rwlock_rdlock(&Lock); //lock recente
            int len = snprintf(seat_str, sizeof(seat_str), "%u", *seat);
            pthread_rwlock_unlock(&Lock);

            // Check if snprintf was successful
            if (len > 0) {
                // Check if there is enough space in the buffer for the seat_str
                if (buffer_len + (size_t)len + 1 < (size_t)sizeof(buffer)) {  // +1 for the space separator
                    // Append the seat_str to the buffer
                    if (buffer_len > 0 && j > 1) {
                        buffer[buffer_len++] = ' ';  // Add space separator if not the first element
                    }
                    memcpy(buffer + buffer_len, seat_str, (size_t)len);
                    buffer_len += (size_t)len;
                } else {
                    fprintf(stderr, "Buffer overflow\n");
                    pthread_rwlock_unlock(&outputLock);
                    return 1;
                }
            }
        }
        // Add newline character at the end of each row
        if (buffer_len + 1 < sizeof(buffer)) {
            buffer[buffer_len++] = '\n';
        } else {
            fprintf(stderr, "Buffer overflow\n");
            pthread_rwlock_unlock(&outputLock);
            return 1;
        }
    }

    // Write the entire buffer to the file
    if (write(fd, buffer, buffer_len) < 0) {
        perror("Write to file failed");
        pthread_rwlock_unlock(&outputLock);
        return 1;
    }

    pthread_rwlock_unlock(&outputLock);
    return 0;
}

int ems_list_events(int fd) {
  pthread_rwlock_rdlock(&Lock);
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    pthread_rwlock_unlock(&Lock);
    return 1;
  }
  pthread_rwlock_unlock(&Lock);

  pthread_rwlock_rdlock(&Lock);
  if (event_list->head == NULL) {
    fprintf(stderr, "No events\n");
    pthread_rwlock_unlock(&Lock);
    return 0;
  }
  pthread_rwlock_unlock(&Lock);

  pthread_rwlock_wrlock(&outputLock);

  char buffer[BUFFER_SIZE];
  size_t offset = 0;

  pthread_rwlock_wrlock(&Lock);
  struct ListNode* current = event_list->head;
  pthread_rwlock_unlock(&Lock);
  while (current != NULL) {
    // Assuming the maximum length of each line is 50 characters
    char eventID_str[50];
    pthread_rwlock_rdlock(&Lock);
    int len = snprintf(eventID_str, sizeof(eventID_str), "Event: %u\n", current->event->id);
    pthread_rwlock_unlock(&Lock);
    // Check if there is enough space in the buffer
    if (offset + (size_t)len < BUFFER_SIZE) {
      // Copy the content to the buffer
      strncpy(buffer + offset, eventID_str, (size_t)len);
      offset += (size_t)len;
    } else {
      // Buffer is full, write its content to the file descriptor
      write(fd, buffer, offset);
      // Reset the offset for the next iteration
      offset = 0;
    }
    pthread_rwlock_wrlock(&Lock);
    current = current->next;
    pthread_rwlock_unlock(&Lock);
  }

  // Write any remaining content in the buffer
  if (offset > 0) {
    write(fd, buffer, offset);
  }
  pthread_rwlock_unlock(&outputLock);

  return 0;
}

void ems_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}
