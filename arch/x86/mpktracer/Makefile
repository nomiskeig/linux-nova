# see https://docs.kernel.org/kbuild/makefiles.html
ccflags-y := -mxsave  -fno-stack-protector -O1 -std=gnu11
obj-y += tracer.o Zydis.o allocator.o collector.o context.o instruction.o logging.o patcher.o pre.o print.o register.o shared.o trampoline.o signal_handler.o post.o vmalloc_for_tramp.o
