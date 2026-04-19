/* driverlib/debug.h — TivaWare debug assert shim for AM2434 */
#ifndef DRIVERLIB_DEBUG_H_SHIM
#define DRIVERLIB_DEBUG_H_SHIM

/* TivaWare ASSERT macro — used by some utility modules */
#ifdef DEBUG
#define ASSERT(expr) do { if (!(expr)) { while(1); } } while(0)
#else
#define ASSERT(expr) ((void)0)
#endif

#endif
