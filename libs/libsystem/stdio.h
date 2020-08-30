#ifndef __oneOS__LibC__STDIO_H
#define __oneOS__LibC__STDIO_H
#include "types.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define MAX_OPENED_FILES 16
#define STDIO_BUFFER_SIZE 16

struct __file {
    int _fileno; // -1 if the file is closed
    int _r;
    int _read_write_position;
    char* _IO_read_pointer;
    char* _IO_read_end;
    char* _IO_write_pointer;
    char* _IO_write_end;
};
typedef struct __file FILE;

FILE opened_files[MAX_OPENED_FILES];

int fclose(FILE* stream);
FILE* fopen(const char* filename, const char* mode);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fseek(FILE* stream, uint32_t offset, int whence);

#endif // __oneOS__LibC__STDIO_H