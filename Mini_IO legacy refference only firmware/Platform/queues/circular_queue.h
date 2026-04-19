#ifndef CIRCULAR_QUEUE_H
#define CIRCULAR_QUEUE_H

typedef struct
{
    unsigned char *data;
    unsigned int   data_size;
    unsigned int   read_index;
    unsigned int   write_index;
}_queue;

unsigned char queue_peek(_queue *queue);
void queue_init(_queue *queue, unsigned char *buf, unsigned int length);
void queue_drain(_queue *queue);
int dequeue(_queue *queue, unsigned char *buf, unsigned int length);
int enqueue(_queue *queue, unsigned char *data, unsigned int length, int force);
unsigned int sizeof_queue(_queue *queue);

#endif
/***   End Of File   ***/
