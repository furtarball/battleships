#ifndef TYPES_H
#define TYPES_H

#include "platform.h"
#include <netinet/in.h>
#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#endif
#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#endif
#include <sys/types.h>

/* This is a workaround for a bug in Clang,
 * apparently compatible with GCC as well */
using in6_addr = struct in6_addr;
#ifdef HAVE_KEVENT
using kevent_t = struct kevent;
#endif
#ifdef HAVE_SYS_EPOLL_H
using epoll_event = struct epoll_event;
#endif

#endif
