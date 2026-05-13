/*
 * Copyright (c) 2007-2014, Cameron Rich
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
 * @file os_port.h
 *
 * Some stuff to minimise the differences between windows and linux/unix
 */

#ifndef HEADER_OS_PORT_H
#define HEADER_OS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

//#define AX_MEM_TESTING

#include "os_int.h"
#include "axTLS_config.h"
#include <stdio.h>

#define STDCALL
#define EXP_FUNC


#define SSL_CTX_MUTEX_INIT(A)
#define SSL_CTX_MUTEX_DESTROY(A)
#define SSL_CTX_LOCK(A)
#define SSL_CTX_UNLOCK(A)


#include <time.h>
#include "extra_errno.h"
#include "lwip/sockets.h"


#define SOCKET_READ(A,B,C)      lwip_read(A,B,C)
#define SOCKET_WRITE(A,B,C)     lwip_write(A,B,C)
#define TTY_FLUSH();

#define select(A,B,C,D,E) 		lwip_select(A,B,C,D,E)

EXP_FUNC int STDCALL os_port_ConnectionLost(int fd);
EXP_FUNC void * STDCALL alloca(size_t n);
EXP_FUNC void STDCALL gettimeofday(struct timeval* t, void* timezone);



#ifdef AX_MEM_TESTING
#define malloc(A)       ax_malloc(A)
#define realloc(A,B)    ax_realloc(A,B)
#define calloc(A,B)     ax_calloc(A,B)
#define free(A)         ax_free(A)

EXP_FUNC void * STDCALL ax_malloc(size_t s);
EXP_FUNC void * STDCALL ax_realloc(void *y, size_t s);
EXP_FUNC void * STDCALL ax_calloc(size_t n, size_t s);
EXP_FUNC void STDCALL ax_free(void *y);


#else



#endif


#endif 
