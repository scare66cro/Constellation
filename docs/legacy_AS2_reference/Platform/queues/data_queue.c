// pbuf_queue.c

#include <stdlib.h>
#include <stdio.h>

#include "data_queue.h"
#include "debug.h"
void data_queue_init(_data_queue *queue, void ** ptrs, unsigned int num_arrays)
{
    queue->arrays=ptrs;
    queue->num_arrays=num_arrays;
    queue->read_index=0;
    queue->write_index=0;
    queue->available=0;
}

unsigned int data_size(_data_queue *queue)
{
	return queue->available;
}

void *data_peek(_data_queue *queue)
{
	if (queue->read_index==queue->write_index) return NULL;
    return queue->arrays[queue->read_index];
}

void *data_dequeue(_data_queue *queue)
{
    unsigned int read_index;
    unsigned int write_index;
    unsigned char *ret;
    
    read_index=queue->read_index;
    write_index=queue->write_index;
    
    if (read_index==write_index) return NULL;
    
    ret=queue->arrays[read_index];
    read_index++;
    if (read_index>=queue->num_arrays) read_index=0;
    
    queue->read_index=read_index;
    if (queue->available==0)
    {
    	debug_printf("\r\n\r\n!!!!!!!!!!!!!!!!!!! queue = %d\r\n", queue->available);
    	while(1);
    }
    queue->available--;
    	
    return ret;
}

int data_enqueue(_data_queue *queue, void *data)
{
    unsigned int read_index;
    unsigned int write_index;
    unsigned int next;
    
    if (data==NULL) return -1;
    if (queue==NULL) return -1;
    
    read_index=queue->read_index;
    write_index=queue->write_index;
    
    next=write_index+1;
    if (next>=queue->num_arrays) next=0;
    
    if (next==read_index) return -1;
    
    queue->arrays[write_index]=data;
    
    queue->write_index=next;
    queue->available++;
    return 0;
}


/***   End Of File   ***/
