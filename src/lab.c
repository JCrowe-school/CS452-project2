#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include "lab.h"


struct queue {
    void **array;
    int front;
    int rear;
    int size;
    bool shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond_empty;
    pthread_cond_t cond_full;
};

queue_t queue_init(int capacity) {
    queue_t q = (queue_t)malloc(sizeof(struct queue));
    if(q == NULL) {perror("Failed to allocate memory for queue struct!"); exit(EXIT_FAILURE);}
    q->array = (void **)malloc(sizeof(void *) * capacity);
    if(q->array == NULL) {perror("Failed to allocate memory for queue!"); free(q); exit(EXIT_FAILURE);}
    q->front = -1; //-1 serves to help mark as empty
    q->rear = -1;
    q->size = capacity;
    q->shutdown = false;

    //initialize mutex and condition variables
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_empty, NULL);
    pthread_cond_init(&q->cond_full, NULL);

    return q;
}

void queue_destroy(queue_t q) {
    static pthread_mutex_t dmutex = PTHREAD_MUTEX_INITIALIZER; //temp mutex for queue_destroy
    pthread_mutex_lock(&dmutex);

    if(!is_shutdown(q)) {queue_shutdown(q);} //stop new items being added to the queue, if it wasn't already

    //if queue isn't empty, free what's left
    while(!is_empty(q)) {
        free(dequeue(q));
        printf("Fresh produce into the incinerator!"); //debugging
    }

    //free queue, mutex stuff, and then the struct itself
    free(q->array);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond_empty);
    pthread_cond_destroy(&q->cond_full);
    free(q);

    pthread_mutex_unlock(&dmutex);
}

void enqueue(queue_t q, void *data) {
    pthread_mutex_lock(&q->mutex); //lock to ensure only one producer adds an item at a time

    if(is_shutdown(q)) {pthread_mutex_unlock(&q->mutex); free(data); return;} //if in shutdown, free data exit immediately

    //check if the queue is full and wait if it is, using modulo to wrap back to the front if needed
    int nextRear = (q->rear + 1) % q->size;
    while(nextRear == q->front) {
        pthread_cond_signal(&q->cond_full); //signal consumers that there's a full queue
        pthread_cond_wait(&q->cond_empty, &q->mutex); //wait for queue to be empty
    }

    if(is_empty(q)) q->front = 0; //if queue is empty, set front to 0
    q->rear = nextRear;
    q->array[q->rear] = data;

    //signal consumers if the queue is now full, otherwise signal producers
    pthread_cond_signal(((q->rear + 1) % q->size) == q->front ? &q->cond_full : &q->cond_empty); 
    pthread_mutex_unlock(&q->mutex);
}

void *dequeue(queue_t q) {
    pthread_mutex_lock(&q->mutex);

    while(is_empty(q)) {
        if(is_shutdown(q)) {
            pthread_mutex_unlock(&q->mutex);
            return NULL;
        } else {
            pthread_cond_signal(&q->cond_empty); //if queue is empty and we're not shutting down, signal producers
            pthread_cond_wait(&q->cond_full, &q->mutex); //wait for queue to be full
        }
    }

    void *data = q->array[q->front];

    if(q->front == q->rear) { //if there's only one element, reset to empty state
        q->front = q->rear = -1;
    } else {
        q->front = (q->front + 1) % q->size; //otherwise increment front and use modulo for wrapping
    }

    //signal producers if the queue is now empty, otherwise signal consumers
    pthread_cond_signal(is_empty(q) ? &q->cond_empty : &q->cond_full);
    pthread_mutex_unlock(&q->mutex);

    return data;
}

void queue_shutdown(queue_t q) {
    pthread_mutex_lock(&q->mutex);

    q->shutdown = true;

    //signal waiting consumers to start again as producers should be done
    //pthread_cond_broadcast(&q->cond_empty); //producers
    pthread_cond_broadcast(&q->cond_full); //consumers

    pthread_mutex_unlock(&q->mutex);
}

bool is_empty(queue_t q) {
    return q->front == -1;
}

bool is_shutdown(queue_t q) {
    return q->shutdown;
}