
#ifndef HTTP_FS_H
#define HTTP_FS_H

#include "flash.h"
#include "queues/circular_queue.h"

// FreeRTOS Includes
//#include "priorities.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// Maximum number of open files, both static and dynamic
#define HTTPFS_NUM_OPEN_FILES 	10
#define HTTPFS_MAX_ROM_FILES  	100 // Maximum number of ROM files to support
#define HTTPFS_MAX_ROM_FILE_NAME_LENGTH	48	// Maximum file name length

// Maximum number of dynamic files and their buffer size
#define HTTPFS_DYNAMIC_HANDLERS_SIZE    1000
#define HTTPFS_NUM_DYNAMIC_HANDLERS    	3
#define HTTPFS_DYNAMIC_GET_VAL_LENGTH   200


#define FS_MAGIC        	0xbfa399bd
#define FS_FLASH_OFFSET 	FLASH_WEB



typedef  void (*fs_cgi_read_fn   )(void *, char*);
typedef  void (*fs_cgi_process_fn)(void*, char *, unsigned int);
typedef  void (*fs_cgi_write_fn )(void*, unsigned char*, fs_cgi_process_fn, unsigned int);

typedef struct _fs_dynamic_handler _fs_dynamic_handler;

typedef struct
{
    fs_cgi_read_fn 		read_fn;
    fs_cgi_write_fn 	write_fn;
    fs_cgi_process_fn	process_fn;
    char *name;

    void                *next;
}_fs_cgi_str;

typedef struct
{
	unsigned char *data;
	int len;
	int index;
	int loc;
	int write;

	_fs_cgi_str *cgi_str;
	_fs_dynamic_handler *dynamic_handler;
	char escape_buffer[100];
	char fs_printf_t[500];
	char get_val[HTTPFS_DYNAMIC_GET_VAL_LENGTH];
} _fs_file;

// Define for the dynamic file handlers
struct _fs_dynamic_handler
{
	unsigned char in_use;
	unsigned char still_processing;
	unsigned char force_quit;
	_queue data_queue;

	xSemaphoreHandle work_sem;
	xSemaphoreHandle data_sem;
	_fs_file *file; // pointer BACK to the fs_file that holds this dynamic handler
};


void fs_init(void);
int fs_printf(_fs_file *file, const char *fmt, ...);
int fs_printf_old(_fs_file *file, char *fmt, ...);
_fs_file *fs_open(char *name, int write, char *val, int content_length);
void fs_close(_fs_file *file, char *val);
int fs_read(_fs_file *file, unsigned char *buffer, int count);
int fs_unread(_fs_file *file, int count);
int fs_write(_fs_file *file, unsigned char *buffer, int count, int content_length);

void dynamic_handler_stats(void);


void fs_register_cgi(_fs_cgi_str *cgi);

#endif
