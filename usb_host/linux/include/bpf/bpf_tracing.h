#ifndef USB_HOST_BPF_TRACING_H
#define USB_HOST_BPF_TRACING_H

/* Native static programs retain the upstream BPF_PROG spelling. */
#define BPF_PROG(name, ...) name(__VA_ARGS__)

/* Function sections become private native functions. License declarations in
 * imported programs retain their upstream line commented beside a native
 * static marker because SEC() follows the declarator there. */
#define SEC(name) static

#endif
