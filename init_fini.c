
extern char *__init_array_start;
extern char *__init_array_end;
extern char *__fini_array_start;
extern char *__fini_array_end; 

typedef void (*func)();

void _run_ctors() {
  char *start = (char *)&__init_array_start;
  char *end = (char *)&__init_array_end;
  char *fptr = start;
  func p;
  for (fptr = start; fptr < end; fptr = fptr + sizeof(char *)) {
    p = *(func*)fptr;
    p();
  }
}

void _run_detors() {
  char *start = (char *)&__fini_array_start;
  char *end = (char *)&__fini_array_end;
  char *fptr = start;
  func p;
  for (fptr = start; fptr < end; fptr = fptr + sizeof(char *)) {
    p = *(func*)fptr;
    p();
  }  
}
