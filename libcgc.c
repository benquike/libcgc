/*******************************************************
 * Copyright (C) 2017 Hui Peng <peng124@purdue.edu>
 *
 * This file is part of project.
 *
 * The source files from project can not be copied and/or
 * distributed without the express permission of Hui Peng
 *******************************************************/

#include "libcgc.h"

#if defined(_CGC_EMU)
// _terminate
void _terminate(unsigned int status) {
  exit(status);
}

// transmit
int transmit(int fd, const void *buf, size_t count, size_t *tx_bytes)
{
  size_t w = write(fd, buf, count);

  if (w == -1)
    return EBADF;

  if (tx_bytes != NULL)
    *tx_bytes = w;

  return 0;
}

// receive
int receive(int fd, void *buf, size_t count, size_t *rx_bytes) {
  size_t r = read(fd, buf, count);

  if (r == -1)
    return EBADF;

  if (rx_bytes != NULL)
    *rx_bytes = r;

  return 0;
}

// fdwait
int fdwait(int nfds, fd_set *readfds, fd_set *writefds,
	   struct timeval *timeout, int *readyfds) {
  if (nfds < 0)
    return EINVAL;

  if (readyfds == NULL)
    return EFAULT;

  int r = select(nfds, readfds, writefds, NULL, timeout);

  if (r == -1) {
    return errno;
  }

  *readyfds = r;

  return 0;
}

// allocate
int allocate(size_t length, int is_X, void **addr) {

  if (length == 0)
    return EINVAL;

  if (addr == NULL)
    return EFAULT;

  length = 4096 * ((length + 4096)/4096);

  char *ret = valloc(length);

  if (ret == NULL)
    return ENOMEM;

  if (is_X) {
    mprotect(ret, length, PROT_EXEC);
  }

  *addr = ret;
  return 0;
}

// deallocate
int deallocate(void *addr, size_t length) {
  free(addr);

  return 0;
}

// random
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int random(void *buf, size_t count, size_t *rnd_bytes) {

  if (count <= 0) {
    return EINVAL;
  }

  if (buf == NULL || rnd_bytes == NULL) {
    return EFAULT;
  }

  int r_fd = open("/dev/urandom", O_RDONLY);

  if (r_fd == -1) {
    return EFAULT ;
  }

  size_t r;

  while (count > 0) {
    r = read(r_fd, buf, count);
    buf = buf + r;
    count = count - r;
  }

  *rnd_bytes = r;

  return 0;
}

#endif /* !defined(_CGC_EMU) */
