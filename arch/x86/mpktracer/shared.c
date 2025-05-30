
#include "shared.h"
#include "config.h"
#ifndef TRACER_USERSPACE
#include <linux/module.h>
MODULE_LICENSE("GPL");
#include <linux/printk.h>
#endif
void set_length(Trace *trace, long length) {
    // this cuts of the highest bit but lets be honest we have values that big
    // we have other issues
    trace->value_size_and_location = length << 1;
}

void set_intern(Trace *trace) {
	// TODO: implement this correctly
    //trace->value_size_and_location &= ((unsigned long)-1) << 1;
}
void set_extern(Trace *trace) { trace->value_size_and_location |= 0x01; }


long get_size(Trace *trace) {
	return trace->value_size_and_location >> 1;
}
int is_intern(Trace* trace) {
	return !(trace->value_size_and_location & 0x01);
}
