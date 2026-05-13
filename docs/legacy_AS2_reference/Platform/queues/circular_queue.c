
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "circular_queue.h"
#include "system.h"

void queue_init(_queue *queue, unsigned char *buf, unsigned int length)
{
    queue->read_index=0;
    queue->write_index=0;
    queue->data=buf;
    queue->data_size=length;
}

void queue_drain(_queue *queue)
{
    queue->read_index=0;
    queue->write_index=0;
}

unsigned char queue_peek(_queue *queue)
{
    return queue->data[queue->read_index];
}

int dequeue(_queue *queue, unsigned char *buf, unsigned int length)
{
    int read_index;
    int write_index;
    int copy1=0;
    int copy2=0;

    read_index=queue->read_index;
    write_index=queue->write_index;

    if (read_index==write_index) return 0;

    if (read_index<write_index)
    {
        copy1=write_index-read_index;
        copy2=0;
        if (copy1>length) copy1=length;
    }
    else
    {
        copy1=queue->data_size-read_index;
        copy2=write_index;
        
    }
    
    if ((copy1+copy2)>length)
    {
        if (copy1>length)
        {
            copy1=length;
            copy2=0;
        }
        else
        {
            copy2=(length-copy1);
        }
    }

    //if ((copy1+copy2)!=length)
    //{
        //debug_printf("ERROR:   copy1=%d, copy2=%d, length=%d\n", copy1, copy2, length);
    //}
    
    memcpy(buf, queue->data+read_index, copy1);
    length-=copy1;
    read_index+=copy1;
    if (read_index>=queue->data_size) read_index=0;
    
    memcpy(buf+copy1, queue->data+read_index, copy2);
    length-=copy2;
    read_index+=copy2;
    if (read_index>=queue->data_size) read_index=0;
    
    queue->read_index=read_index;
    
    return copy1+copy2;
}

// only updates the write index and is only called from socket_received
int enqueue(_queue *queue, unsigned char *data, unsigned int length, int force)
{
    int read_index;
    int write_index;
    int copy1=0;
    int copy2=0;
    int next;
    
    read_index=queue->read_index;
    write_index=queue->write_index;
    
    if (length==1)
    {
        next=write_index+1;
        if (next>=queue->data_size) next=0;
        if (next==read_index) return 0;
        queue->data[write_index]=data[0];
        queue->write_index=next;
        return 1;
    }
            
    // 1: copy from write_index first_copy bytes
    // 2: copy from 0           second_copy bytes
    
    if (read_index>write_index)
    {
        copy1=read_index-write_index-1;
        if (copy1>length) copy1=length;
        copy2=0;
    }
    else if (write_index>=read_index)
    {
        copy1=queue->data_size-write_index;
        copy2=read_index;
        
        if (read_index==0) copy1--;
        else               copy2--;
        
        if ((copy1+copy2)>length)
        {
            if (copy1>length)
            {
                copy1=length;
                copy2=0;
            }
            else
            {
                copy2=(length-copy1);
            }
        }
    }
    
    if ((force==0)&&((copy1+copy2)!=length)) return 0;
    
    memcpy(queue->data+write_index, data, copy1);
    data+=copy1;
    length-=copy1;
    write_index+=copy1;
    if (write_index>=queue->data_size) write_index=0;
    
    memcpy(queue->data+write_index, data, copy2);
    length-=copy2;
    write_index+=copy2;
    if (write_index>=queue->data_size) write_index=0;
    queue->write_index=write_index;
    return copy1+copy2;
}

// queue operations
unsigned int sizeof_queue(_queue *queue)
{
    int read_index;
    int write_index;
    unsigned int num_items;
    
    read_index=queue->read_index;
    write_index=queue->write_index;
    
    if (read_index<write_index)
    {
        num_items=write_index-read_index;
    }        
    else if (write_index<read_index)
    {
        num_items=queue->data_size-read_index;
        num_items+=write_index;
    }
    else
    {
        num_items=0;
    }
    return num_items;
}


/***   End Of File   ***/
