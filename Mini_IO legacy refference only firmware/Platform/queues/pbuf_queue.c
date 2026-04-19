// pbuf_queue.c

#include <stdlib.h>
#include <stdio.h>

#include "pbuf_queue.h"



void pbuf_queue_init(_pbuf_queue *queue, struct pbuf **p, unsigned int num_p)
{
    queue->p=p;
    queue->num_pbufs=num_p;
    queue->read_index=0;
    queue->write_index=0;
    queue->pbufs_available=0;  
}

struct pbuf *pbuf_dequeue(_pbuf_queue *queue)
{
    unsigned int read_index;
    unsigned int write_index;
    struct pbuf *p=NULL;
    
    read_index=queue->read_index;
    write_index=queue->write_index;
    
    if (read_index==write_index) return NULL;
    p=queue->p[read_index];
    read_index++;
    if (read_index>=queue->num_pbufs) read_index=0;
    
    queue->read_index=read_index;
    queue->pbufs_available--;  
    return p;
}

int pbuf_enqueue(_pbuf_queue *queue, struct pbuf *p)
{
    unsigned int read_index;
    unsigned int write_index;
    unsigned int next;
    
    read_index=queue->read_index;
    write_index=queue->write_index;
    next=write_index+1;
    if (next>=queue->num_pbufs) next=0;
    
    if (next==read_index) return -1;
    queue->p[write_index]=p;
    
    queue->write_index=next;
    queue->pbufs_available++;
    return 0;
}

/***   End Of File   ***/
