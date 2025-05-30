// taken and adapted from https://wiki.osdev.org/User:A22347/Printf
#include "config.h"
#ifdef TRACER_USERSPACE
#include "logging.h"
#include <complex.h>
#include <ctype.h> //isdigit
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>  //putchar
#include <string.h> //strcpy, strcat, memcpy, memset
#include <unistd.h>
#include <wctype.h>

// we provide our own fucntions so we dont call out into libc where we might
// hight an overwritten trmapoline and run into a loop/segfault
// taken from https://stackoverflow.com/questions/2488563/strcat-implementation 
char *local_strcat(char *dest, const char *src) {
    size_t i, j;
    for (i = 0; dest[i] != '\0'; i++)
        ;
    for (j = 0; src[j] != '\0'; j++)
        dest[i + j] = src[j];
    dest[i + j] = '\0';
    return dest;
}
// from https://www.w3resource.com/c-programming-exercises/c-snippets/implementing-custom-strlen-function-in-c.php
size_t local_strlen(const char* str) {
    size_t len = 0;
    while (*str != '\0') {
        len++;
        str++;
    }
    return len;
}
char *__int_str(intmax_t i, char b[], int base, bool plusSignIfNeeded,
                bool spaceSignIfNeeded, int paddingNo, bool justify,
                bool zeroPad) {

    char digit[32] = {0};
    for (int i = 0; i < 32; i++) {
        digit[i] = 0;
    }
    strcpy(digit, "0123456789");

    if (base == 16) {
        local_strcat(digit, "ABCDEF");
    } else if (base == 17) {
        local_strcat(digit, "abcdef");
        base = 16;
    }

    char *p = b;
    if (i < 0) {
        *p++ = '-';
        i *= -1;
    } else if (plusSignIfNeeded) {
        *p++ = '+';
    } else if (!plusSignIfNeeded && spaceSignIfNeeded) {
        *p++ = ' ';
    }

    intmax_t shifter = i;
    do {
        ++p;
        shifter = shifter / base;
    } while (shifter);

    *p = '\0';
    do {
        *--p = digit[i % base];
        i = i / base;
    } while (i);

    int padding = paddingNo - (int)local_strlen(b);
    if (padding < 0)
        padding = 0;

    if (justify) {
        while (padding--) {
            if (zeroPad) {
                b[local_strlen(b)] = '0';
            } else {
                b[local_strlen(b)] = ' ';
            }
        }

    } else {
        char a[256] = {0};
        while (padding--) {
            if (zeroPad) {
                a[local_strlen(a)] = '0';
            } else {
                a[local_strlen(a)] = ' ';
            }
        }
        local_strcat(a, b);
        strcpy(b, a);
    }

    return b;
}

int displayCharacter(char c, int *a, int fd, char buffer[], int index) {
    // int res = write(fd, buf, 1);
    buffer[index] = c;
    /*if (res == -1) {
        safe_printf("fd for write: %i\n", fd);
        err(EXIT_FAILURE, "write failed\n");
    }
        */
    *a += 1;
    return 1;
}

int displayString(char *c, int *a, int fd, char buffer[], int current_index) {
    int res = 0;
    for (int i = 0; c[i]; ++i) {
        res += displayCharacter(c[i], a, fd, buffer, current_index + res);
    }
    return res;
}

int vprintf_local(const char *format, va_list list, int fd) {
    char buffer[1000] = {0};
    int current_index = 0;
    int chars = 0;
    char intStrBuffer[256] = {0};

    for (int i = 0; format[i]; ++i) {

        char specifier = '\0';
        char length = '\0';

        int lengthSpec = 0;
        int precSpec = 0;
        bool leftJustify = false;
        bool zeroPad = false;
        bool spaceNoSign = false;
        bool altForm = false;
        bool plusSign = false;
        bool emode = false;
        int expo = 0;

        if (format[i] == '%') {
            ++i;

            bool extBreak = false;
            while (1) {

                switch (format[i]) {
                case '-':
                    leftJustify = true;
                    ++i;
                    break;

                case '+':
                    plusSign = true;
                    ++i;
                    break;

                case '#':
                    altForm = true;
                    ++i;
                    break;

                case ' ':
                    spaceNoSign = true;
                    ++i;
                    break;

                case '0':
                    zeroPad = true;
                    ++i;
                    break;

                default:
                    extBreak = true;
                    break;
                }

                if (extBreak)
                    break;
            }

            while (isdigit(format[i])) {
                lengthSpec *= 10;
                lengthSpec += format[i] - 48;
                ++i;
            }

            if (format[i] == '*') {
                lengthSpec = va_arg(list, int);
                ++i;
            }

            if (format[i] == '.') {
                ++i;
                while (isdigit(format[i])) {
                    precSpec *= 10;
                    precSpec += format[i] - 48;
                    ++i;
                }

                if (format[i] == '*') {
                    precSpec = va_arg(list, int);
                    ++i;
                }
            } else {
                precSpec = 6;
            }

            if (format[i] == 'h' || format[i] == 'l' || format[i] == 'j' ||
                format[i] == 'z' || format[i] == 't' || format[i] == 'L') {
                length = format[i];
                ++i;
                if (format[i] == 'h') {
                    length = 'H';
                    ++i;
                } else if (format[i] == 'l') {
                    length = 'q';
                    ++i;
                }
            }
            specifier = format[i];

            for (int i = 0; i < 256; i++) {
                intStrBuffer[i] = 0;
            }

            int base = 10;
            if (specifier == 'o') {
                base = 8;
                specifier = 'u';
                if (altForm) {
                    current_index +=
                        displayString("0", &chars, fd, buffer, current_index);
                }
            }
            if (specifier == 'p') {
                base = 16;
                length = 'z';
                specifier = 'u';
            }
            switch (specifier) {
            case 'X':
                base = 16;
            case 'x':
                base = base == 10 ? 17 : base;
                if (altForm) {
                    current_index +=
                        displayString("0x", &chars, fd, buffer, current_index);
                }

            case 'u': {
                switch (length) {
                case 0: {
                    unsigned int integer = va_arg(list, unsigned int);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'H': {
                    unsigned char integer =
                        (unsigned char)va_arg(list, unsigned int);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'h': {
                    unsigned short int integer = va_arg(list, unsigned int);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'l': {
                    unsigned long integer = va_arg(list, unsigned long);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'q': {
                    unsigned long long integer =
                        va_arg(list, unsigned long long);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'j': {
                    uintmax_t integer = va_arg(list, uintmax_t);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'z': {
                    size_t integer = va_arg(list, size_t);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                default:
                    break;
                }
                break;
            }

            case 'd':
            case 'i': {
                switch (length) {
                case 0: {
                    int integer = va_arg(list, int);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'H': {
                    signed char integer = (signed char)va_arg(list, int);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'h': {
                    short int integer = va_arg(list, int);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'l': {
                    long integer = va_arg(list, long);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'q': {
                    long long integer = va_arg(list, long long);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'j': {
                    intmax_t integer = va_arg(list, intmax_t);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                case 'z': {
                    size_t integer = va_arg(list, size_t);
                    __int_str(integer, intStrBuffer, base, plusSign,
                              spaceNoSign, lengthSpec, leftJustify, zeroPad);
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                    break;
                }
                default:
                    break;
                }
                break;
            }

            case 'c': {
                if (length == 'l') {
                    current_index +=
                        displayCharacter(va_arg(list, wint_t), &chars, fd,
                                         buffer, current_index);
                } else {
                    current_index += displayCharacter(
                        va_arg(list, int), &chars, fd, buffer, current_index);
                }

                break;
            }

            case 's': {
                current_index += displayString(va_arg(list, char *), &chars, fd,
                                               buffer, current_index);
                break;
            }

            case 'n': {
                switch (length) {
                case 'H':
                    *(va_arg(list, signed char *)) = chars;
                    break;
                case 'h':
                    *(va_arg(list, short int *)) = chars;
                    break;

                case 0: {
                    int *a = va_arg(list, int *);
                    *a = chars;
                    break;
                }

                case 'l':
                    *(va_arg(list, long *)) = chars;
                    break;
                case 'q':
                    *(va_arg(list, long long *)) = chars;
                    break;
                case 'j':
                    *(va_arg(list, intmax_t *)) = chars;
                    break;
                case 'z':
                    *(va_arg(list, size_t *)) = chars;
                    break;
                default:
                    break;
                }
                break;
            }

            case 'e':
            case 'E':
                emode = true;

            case 'f':
            case 'F':
            case 'g':
            case 'G': {
                double floating = va_arg(list, double);

                while (emode && floating >= 10) {
                    floating /= 10;
                    ++expo;
                }

                int form = lengthSpec - precSpec - expo -
                           (precSpec || altForm ? 1 : 0);
                if (emode) {
                    form -= 4; // 'e+00'
                }
                if (form < 0) {
                    form = 0;
                }

                __int_str(floating, intStrBuffer, base, plusSign, spaceNoSign,
                          form, leftJustify, zeroPad);

                current_index += displayString(intStrBuffer, &chars, fd, buffer,
                                               current_index);

                floating -= (int)floating;

                for (int i = 0; i < precSpec; ++i) {
                    floating *= 10;
                }
                intmax_t decPlaces = (intmax_t)(floating + 0.5);

                if (precSpec) {
                    current_index += displayCharacter('.', &chars, fd, buffer,
                                                      current_index);
                    __int_str(decPlaces, intStrBuffer, 10, false, false, 0,
                              false, false);
                    intStrBuffer[precSpec] = 0;
                    current_index += displayString(intStrBuffer, &chars, fd,
                                                   buffer, current_index);
                } else if (altForm) {
                    current_index += displayCharacter('.', &chars, fd, buffer,
                                                      current_index);
                }

                break;
            }

            case 'a':
            case 'A':
                // ACK! Hexadecimal floating points...
                break;

            default:
                break;
            }

            if (specifier == 'e') {
                current_index +=
                    displayString("e+", &chars, fd, buffer, current_index);
            } else if (specifier == 'E') {
                current_index +=
                    displayString("E+", &chars, fd, buffer, current_index);
            }
            if (specifier == 'e' || specifier == 'E') {
                __int_str(expo, intStrBuffer, 10, false, false, 2, false, true);
                current_index += displayString(intStrBuffer, &chars, fd, buffer,
                                               current_index);
            }

        } else {
            current_index +=
                displayCharacter(format[i], &chars, fd, buffer, current_index);
        }
    }


    int res = write(fd, buffer, current_index);
    return chars;
}

int safe_printf(const char *format, ...) {
    va_list list;
    va_start(list, format);
    int i = vprintf_local(format, list, 0);
    va_end(list);
    return i;
}
int safe_print_to_file(int fd, const char *format, ...) {

    va_list list;
    va_start(list, format);
    int i = vprintf_local(format, list, fd);
    va_end(list);
    return i;
}
#endif
