#ifndef ACCMUT_IO_H
#define ACCMUT_IO_H

#include <stddef.h>

typedef struct _ACCMUT_FILE
{
	int flags;
	int fd;
	char *bufbase;
	char *bufend;
	char *read_cur;
	char *write_cur;
	int fsize;	//only for input file
}ACCMUT_FILE;

extern ACCMUT_FILE* accmut_stdin;
extern ACCMUT_FILE* accmut_stdout;
extern ACCMUT_FILE* accmut_stderr;

ACCMUT_FILE* __accmut__fopen(const char *path, const char *mode);

int __accmut__fclose(ACCMUT_FILE *fp);

int __accmut__feof(ACCMUT_FILE *fp);

int __accmut__fileno(ACCMUT_FILE *fp);

ACCMUT_FILE * __accmut__freopen(const char *path, const char *mode, ACCMUT_FILE *fp);

int __accmut__unlink(const char *pathname);


char* __accmut__fgets(char *buf, int size, ACCMUT_FILE *fp);

int __accmut__getc(ACCMUT_FILE *fp);

size_t __accmut__fread(void *buf, size_t size, size_t count, ACCMUT_FILE *fp);

int __accmut__ungetc(int c, ACCMUT_FILE *fp);

int __accmut__fscanf(ACCMUT_FILE *fp, const char *format, ...);


int __accmut__fputc(int c, ACCMUT_FILE *fp);
//putc implemented as a macro
#define __accmut__putc(c, f) ( __accmut__fputc(c, f) )

int __accmut__fputs(const char* s, ACCMUT_FILE *fp);

int __accmut__puts(const char* s);

int __accmut__fprintf(ACCMUT_FILE *fp, const char *format, ...);

int __accmut__printf(const char *format, ...);

size_t __accmut__fwrite(const void *buf, size_t size, size_t count, ACCMUT_FILE *fp);

#endif