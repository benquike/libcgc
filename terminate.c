# include <libcgc.h>

extern void _run_detors();
void __terminate(unsigned int status) __attribute__((__noreturn__));
void _terminate(unsigned int status) {
  _run_detors();
  __terminate(status);
}
