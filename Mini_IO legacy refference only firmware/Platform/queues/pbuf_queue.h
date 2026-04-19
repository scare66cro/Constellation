#ifndef PBUF_QUEUE_H
#define PBUF_QUEUE_H

#include "lwip/pbuf.h"

typedef struct
{
    struct pbuf   **p;
    unsigned int   num_pbufs;
    unsigned int   read_index;
    unsigned int   write_index;
    unsigned int   pbufs_available;
}_pbuf_queue;

void pbuf_queue_init(_pbuf_queue *queue, struct pbuf **p, unsigned int num_p);
struct pbuf *pbuf_dequeue(_pbuf_queue *queue);
int pbuf_enqueue(_pbuf_queue *queue, struct pbuf *p);


#endif
/***   End Of File   ***/
