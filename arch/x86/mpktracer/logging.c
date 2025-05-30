
#include "config.h"
#ifdef TRACER_USERSPACE
#define _GNU_SOURCE
#include <stdio.h>
#include "pthread.h"
#include <unistd.h>
#include <pthread.h>
#endif

#include "print.h"
#ifdef TRACER_USERSPACE
extern ThreadMappings* thread_mappings;
#endif
extern  int id_offset;

#ifdef TRACER_USERSPACE
int get_fd() {
#ifdef TRACER_LOG_TO_CONSOLE
    return 0;
#else
return 0;
	//void* pthread;
	//(asm("mov %%fs:16,%0": "=r"(pthread));
	//return  thread_mappings->mappings[get_tid(pthread) - id_offset].fd;
#endif
}
#endif


