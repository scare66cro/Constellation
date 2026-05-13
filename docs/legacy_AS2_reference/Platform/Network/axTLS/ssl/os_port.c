/*
 * Copyright (c) 2007, Cameron Rich
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * * Neither the name of the axTLS project nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software 
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file os_port.c
 *
 * OS specific functions.
 */
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include "os_port.h"

#include "lwip/sockets.h"
//#include "time_management.h"
#include "debug.h"

#define ALLOCA_BUFFER_SIZE 	1000
unsigned char alloca_buffer[ALLOCA_BUFFER_SIZE];

EXP_FUNC void * STDCALL alloca(size_t n)
{
	return alloca_buffer;
}

//EXP_FUNC void STDCALL gettimeofday(struct timeval* t, void* timezone)
//{
//	t->tv_sec = utc_get_sec();
//	t->tv_usec=0;
//}

#ifdef AX_MEM_TESTING

#undef malloc
#undef realloc
#undef calloc
#undef free


typedef struct
{
	void *malloced;
	unsigned int length;
}_malloc_tracker;

#define MAX_MALLOC_TRACKER	1000
_malloc_tracker malloc_tracker[MAX_MALLOC_TRACKER];

unsigned int num_mallocs_out = 0;
unsigned int max_mallocs_out = 0;
unsigned int num_realloc = 0;


void print_tracker_stats(void)
{
	int i;
	unsigned int max_unfreed=0;
	unsigned int num_items_unfreed=0;

	debug_printf("num_mallocs_out = %d\r\n", num_mallocs_out);
	debug_printf("max_mallocs_out = %d\r\n", max_mallocs_out);

	for(i=0; i<MAX_MALLOC_TRACKER; i++)
	{
		if (malloc_tracker[i].malloced!=0)
		{
			max_unfreed+=malloc_tracker[i].length;
			num_items_unfreed++;
			debug_printf("%08X : %u\r\n", malloc_tracker[i].malloced, malloc_tracker[i].length);
		}
	}

	debug_printf("max_unfreed: %u\r\n", max_unfreed);
	debug_printf("num_items_unfreed: %u\r\n", num_items_unfreed);
	debug_printf("num_reallocs: %u\r\n", num_realloc);
}

void track_malloc(void *val, unsigned int length)
{
	int i;
	//debug_printf("tracking: %X, %d\r\n", val, length);

	for(i=0; i<MAX_MALLOC_TRACKER; i++)
	{
		if (malloc_tracker[i].malloced==0)
		{
			malloc_tracker[i].malloced=val;
			malloc_tracker[i].length = length;
			break;
		}
	}

	if (i>=MAX_MALLOC_TRACKER)
	{
		debug_printf("\r\n\r\n!!!!! Unable to track malloc!!!\r\n");
	}

	num_mallocs_out++;
	if (num_mallocs_out>max_mallocs_out) max_mallocs_out=num_mallocs_out;

}

void free_malloc(void *val)
{

	int i;

	//debug_printf("free: %X\r\n", val);

	for(i=0; i<MAX_MALLOC_TRACKER; i++)
	{
		if (malloc_tracker[i].malloced==val)
		{
			malloc_tracker[i].malloced=0;
			break;
		}
	}

	if (i>=MAX_MALLOC_TRACKER)
	{
		debug_printf("!!!!! Unable to free malloc tracker, not found (%X)!!\r\n", val);
	}

	num_mallocs_out--;

}



EXP_FUNC void * STDCALL ax_malloc(size_t s)
{
    void *x;

    if ((x = malloc(s)) == NULL)
    {
    	debug_printf("malloc faild\r\n");
    }

    track_malloc(x, s);


    return x;
}

EXP_FUNC void STDCALL ax_free(void *y)
{
	free(y);
	free_malloc(y);
}


EXP_FUNC void * STDCALL ax_realloc(void *y, size_t s)
{
    void *x;

    free_malloc(y);

    if ((x = realloc(y, s)) == NULL)
        return NULL;

    track_malloc(x, s);

    num_realloc++;

    return x;
}

EXP_FUNC void * STDCALL ax_calloc(size_t n, size_t s)
{
    void *x;

    //printf("calloc %d\r\n", s);

    if ((x = calloc(n, s)) == NULL)
        return NULL;

    track_malloc(x, n*s);

    return x;
}

#else

void print_tracker_stats(void)
{
	debug_printf("SSL Tracking not enabled\r\n");
}

#endif

/***   End Of File   ***/
