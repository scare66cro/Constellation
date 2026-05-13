/*
 * extra_errno.h
 *
 *  Created on: Mar 26, 2015
 *      Author: kfarr
 */

#ifndef EXTRA_ERRNO_H_
#define EXTRA_ERRNO_H_

#include <errno.h>

#define EWOULDBLOCK     EAGAIN  /* Operation would block */
#define ENOPROTOOPT     92      /* Protocol not available */
#define EOPNOTSUPP      95      /* Operation not supported on transport endpoint */
#define EAFNOSUPPORT    97      /* Address family not supported by protocol */
#define EADDRINUSE      98      /* Address already in use */
#define ECONNABORTED    103     /* Software caused connection abort */
#define ECONNRESET      104     /* Connection reset by peer */
#define ENOBUFS         105     /* No buffer space available */
#define ENOTCONN        107     /* Transport endpoint is not connected */
#define EHOSTUNREACH    113     /* No route to host */
#define EALREADY        114     /* Operation already in progress */



#endif /* EXTRA_ERRNO_H_ */
