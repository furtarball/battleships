#ifndef TYPES_H
#define TYPES_H

#include <netinet/in.h>
#include <sys/types.h>

/* This is a workaround for a bug in Clang,
 * apparently compatible with GCC as well */
using in6_addr = struct in6_addr;

#endif
