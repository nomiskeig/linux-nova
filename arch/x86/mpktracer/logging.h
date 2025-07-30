#pragma once
#include "config.h"
#include "print.h"
#ifdef TRACER_USERSPACE
#include "pthread.h"
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include <linux/printk.h>
#endif
#define RED "\033[31m"
#define ORANGE "\033[33m"
#define RESET "\033[0m"
#ifdef TRACER_USERSPACE

int get_fd();
#endif
#ifdef TRACER_LOG_INFO
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_INFO(...)                                                 \
    do {                                                                       \
        safe_printf("[INFO] ");                                                \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf("\n");                                                     \
    } while (0)
#define TRACER_EXIT(...)                                                       \
    do {                                                                       \
        safe_printf(RED "[ERROR] ");                                           \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf(RESET "\n");                                               \
        err(EXIT_FAILURE, "Coredump");                                         \
    } while (0)
#else
#define TRACER_PRINT_INFO(...)                                                 \
    do {                                                                       \
        pr_info("[INFO] ");                                                    \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#define TRACER_EXIT(Y)                                                         \
    do {                                                                       \
        pr_info(RED "[ERROR] ");                                               \
        pr_info(Y);                                                            \
        pr_info(RESET "\n");                                                   \
    } while (0)
#endif
#else
#define TRACER_PRINT_INFO(...)

#ifdef TRACER_USERSPACE
#define TRACER_EXIT(...)                                                       \
    do {                                                                       \
        safe_printf(RED "[ERROR] ");                                           \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf(RESET "\n");                                               \
        err(EXIT_FAILURE, "Exited");                                           \
    } while (0)
#else
#define TRACER_EXIT(...)                                                       \
    do {                                                                       \
        pr_err(RED "[ERROR] ");                                                \
        pr_err(__VA_ARGS__);                                                   \
        pr_err(RESET "\n");                                                    \
    } while (0)
#endif
#endif

#ifdef TRACER_LOG_DEBUG_PATCHER
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_PATCHER(...)                                        \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[PATCHER] ");                                  \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_PATCHER(...)                                        \
    do {                                                                       \
        printk(KERN_INFO "[PATCHER] ");                                        \
        printk(KERN_INFO __VA_ARGS__);                                         \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG_PATCHER(...)
#endif

#ifdef TRACER_LOG_DEBUG_STARTER
#define TRACER_PRINT_DEBUG_STARTER(...)                                        \
    do {                                                                       \
        safe_printf("[STARTER] ");                                             \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf("\n");                                                     \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_STARTER(...)
#endif

#ifdef TRACER_LOG_DEBUG_INSTRUCTION
#define TRACER_PRINT_DEBUG_INSTRUCTION(...)                                    \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[INSTRUCTION] ");                              \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_INSTRUCTION(...)
#endif

#ifdef TRACER_LOG_DEBUG_SIGNAL_HANDLER
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_SIGNAL_HANDLER(...)                                 \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[HANDLER] ");                                  \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_SIGNAL_HANDLER(...)                                 \
    do {                                                                       \
        pr_info("[HANDLER] ");                                                 \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG_SIGNAL_HANDLER(...)
#endif

#ifdef TRACER_LOG_DEBUG_NOT_IMPLEMENTED
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_NOT_IMPLEMENTED(...)                                \
    do {                                                                       \
        safe_printf(ORANGE "[NOT IMPL] ");                                     \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf(RESET "\n");                                               \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_NOT_IMPLEMENTED(...)                                \
    do {                                                                       \
        pr_info(ORANGE "[NOT IMPL] ");                                         \
        pr_info(__VA_ARGS__);                                                  \
        pr_info(RESET "\n");                                                   \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG_NOT_IMPLEMENTED(...)
#endif

#ifdef TRACER_LOG_DEBUG_INJECT
#define TRACER_PRINT_DEBUG_INJECT(...)                                         \
    do {                                                                       \
        safe_printf("[INJECT] ");                                              \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf("\n");                                                     \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_INJECT(...)
#endif

#ifdef TRACER_LOG_DEBUG_PTRACE
#define TRACER_PRINT_DEBUG_PTRACE(...)                                         \
    do {                                                                       \
        safe_printf("[PTRACE] ");                                              \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf("\n");                                                     \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_PTRACE(...)
#endif
#ifdef TRACER_LOG_DEBUG_PTRACE_ALL
#define TRACER_PRINT_DEBUG_PTRACE_ALL(...)                                     \
    do {                                                                       \
        safe_printf("[PTRACE] ");                                              \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf("\n");                                                     \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_PTRACE_ALL(...)
#endif
#ifdef TRACER_LOG_DEBUG_PTRACE_SYSCALLS
#define TRACER_PRINT_DEBUG_PTRACE_SYSCALL(...)                                 \
    do {                                                                       \
        safe_printf("[PTRACE] ");                                              \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf("\n");                                                     \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_PTRACE_SYSCALL(...)
#endif
#ifdef TRACER_LOG_DEBUG_CONTEXT
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_CONTEXT(...)                                        \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[CONTEXT] ");                                  \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_CONTEXT(...)                                        \
    do {                                                                       \
        pr_info("[CONTEXT] ");                                                 \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG_CONTEXT(...)
#endif

#ifdef TRACER_LOG_DEBUG_KERNEL_TRACER
#ifndef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_KERNEL_TRACER(...)                                  \
    do {                                                                       \
        pr_info("[PTRACE] ");                                                  \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG_KERNEL_TRACER(...)
#endif
#ifdef TRACER_LOG_DEBUG_SYSCALL_HANDLER
#define TRACER_PRINT_DEBUG_SYSCALL_HANDLER(...)                                \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[SYSCALL] ");                                  \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_SYSCALL_HANDLER(...)
#endif
#ifdef TRACER_LOG_DEBUG_COLLECTOR
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_COLLECTOR(...)                                      \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[COLLECTOR] ");                                \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_COLLECTOR(...)                                      \
    do {                                                                       \
        printk(KERN_INFO "[COLLECTOR] ");                                      \
        printk(KERN_INFO __VA_ARGS__);                                         \
        printk("\n");                                                          \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG_COLLECTOR(...)
#endif

#ifdef TRACER_LOG_DEBUG_WRITER
#define TRACER_PRINT_DEBUG_WRITER(...)                                         \
    do {                                                                       \
        safe_printf("[STARTER] ");                                             \
        safe_printf(__VA_ARGS__);                                              \
        safe_printf("\n");                                                     \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_WRITER(...)
#endif
#ifdef TRACER_LOG_DEBUG_TRAMPOLINES
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_TRAMPOLINES(...)                                    \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[TRAMPOLINES] ");                              \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_TRAMPOLINES(...)                                    \
    do {                                                                       \
        pr_info("[TRAMPOLINES] ");                                             \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)

#endif
#else
#define TRACER_PRINT_DEBUG_TRAMPOLINES(...)
#endif

#ifdef TRACER_LOG_ERROR
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_ERROR(...)                                                \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, RED "[ERROR] ");                                \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, RESET "\n");                                    \
        err(EXIT_FAILURE, "An error occured\n");                               \
    } while (0)
#else
#define TRACER_PRINT_ERROR(...)                                                \
    do {                                                                       \
        pr_info("[ERROR] ");                                                   \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else
#define TRACER_PRINT_ERROR(...)
#endif
#ifdef TRACER_LOG_DEBUG_REGISTERS
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_REGISTERS(...)                                      \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[REGISTERS] ");                                \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else

#define TRACER_PRINT_DEBUG_REGISTERS(...)                                      \
    do {                                                                       \
        pr_info("[Registers] ");                                               \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)

#endif
#else
#define TRACER_PRINT_DEBUG_REGISTERS(...)

#endif

#ifdef TRACER_LOG_DEBUG_PRE
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_PRE(...)                                            \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[PRE] ");                                      \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_PRE(...)                                            \
    do {                                                                       \
        pr_info("[PRE] ");                                                     \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else

#define TRACER_PRINT_DEBUG_PRE(...)
#endif
#ifdef TRACER_LOG_DEBUG_NOVA

#ifndef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_NOVA(...)                                           \
    do {                                                                       \
        pr_info("[NOVA] ");                                                    \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#else

#define TRACER_PRINT_DEBUG_NOVA(...)
#endif

#else

#define TRACER_PRINT_DEBUG_NOVA(...)
#endif
#ifdef TRACER_LOG_DEBUG_POST
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_POST(...)                                           \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[POST] ");                                     \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)

#else
#define TRACER_PRINT_DEBUG_POST(...)                                           \
    do {                                                                       \
        pr_info("[POST] ");                                                    \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG_POST(...)
#endif
#ifdef TRACER_LOG_DEBUG_TRACE_ADDRESS
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG_TRACE_ADDRESS(...)                                  \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, "[ADDRESS] ");                                  \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
        safe_print_to_file(fd, "\n");                                          \
    } while (0)
#else
#define TRACER_PRINT_DEBUG_TRACE_ADDRESS(...)                                  \
    do {                                                                       \
        pr_info("[ADDRESS] ");                                                 \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else

#define TRACER_PRINT_DEBUG_TRACE_ADDRESS(...)
#endif
#ifdef TRACER_LOG_DEBUG_NORMAL_PRINT
#ifdef TRACER_USERSPACE
#define TRACER_PRINT_DEBUG(...)                                                \
    do {                                                                       \
        int fd = get_fd();                                                     \
        safe_print_to_file(fd, __VA_ARGS__);                                   \
    } while (0)
#else
#define TRACER_PRINT_DEBUG(...)                                                \
    do {                                                                       \
        pr_info(__VA_ARGS__);                                                  \
    } while (0)
#endif
#else
#define TRACER_PRINT_DEBUG(...)
#endif
