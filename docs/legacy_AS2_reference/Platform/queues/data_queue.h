#ifndef DATA_QUEUE_H
#define DATA_QUEUE_H


typedef struct
{
    void 			**arrays;
    unsigned int    num_arrays;
    unsigned int    read_index;
    unsigned int    write_index;
    unsigned int    available;
}_data_queue;

void data_queue_init(_data_queue *queue, void ** ptrs, unsigned int num_arrays);

unsigned int data_size(_data_queue *queue);

void *data_peek(_data_queue *queue);
void *data_dequeue(_data_queue *queue);

int data_enqueue(_data_queue *queue, void *data);


#endif
/***   End Of File   ***/
