
#include "lwip/def.h"
#include "http_fs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "system.h"
#include "tools.h"


#define FILE_IN_FLASH 	0
#define FILE_IN_RAM   	1
#define FILE_IN_QUEUE 	2

#define CGI_TASK_STACK_SIZE  500
#define PRIORITY_CGI_TASK     3

static void cgi_task(void *pvParameters);


#define FS_HEADER_OFFSET 	FS_FLASH_OFFSET
#define FS_INDEX_OFFSET  	(FS_HEADER_OFFSET + sizeof(_fs_file_header))

// Define the file system memory allocation structure
typedef struct
{
	unsigned char in_use;
    _fs_file file;
}_fs_table;

_fs_table fs_memory[HTTPFS_NUM_OPEN_FILES];



_fs_dynamic_handler fs_dynamic_handlers[HTTPFS_NUM_DYNAMIC_HANDLERS];

static unsigned char dynamic_handler_buffers[HTTPFS_NUM_DYNAMIC_HANDLERS][HTTPFS_DYNAMIC_HANDLERS_SIZE];



typedef struct
{
    unsigned int romfs_magic;
    unsigned int num_entries;
}_fs_file_header;

_fs_file_header file_header;

typedef struct
{
    char name[HTTPFS_MAX_ROM_FILE_NAME_LENGTH];
    unsigned int offset;
    unsigned int length;
}_fs_file_entry;

_fs_file_entry *romfs_files=NULL;
_fs_file_entry actual_romfs_files[HTTPFS_MAX_ROM_FILES];


const unsigned char err_404[]={"HTTP/1.0 404 File not found\r\nContent-type: text/html\r\nConnection: close\r\n\r\nError 404: File not Found"};
const unsigned char err_401[]={"HTTP/1.1 401 Unauthorized\r\nSet-Cookie: data=; path=/; expires='-1d'\r\nLocation: /login.html\r\n\r\n" };

void dynamic_handler_stats(void)
{
    int i;

    for(i=0; i<HTTPFS_NUM_DYNAMIC_HANDLERS; i++)
    {
        debug_printf("%d: in_use: %d still_processing: %d force_quit: %d\r\n", fs_dynamic_handlers[i].in_use, fs_dynamic_handlers[i].still_processing, fs_dynamic_handlers[i].force_quit);
    }
}




_fs_dynamic_handler *cgi_malloc(void)
{
    int i;
    for(i=0; i<HTTPFS_NUM_DYNAMIC_HANDLERS; i++)
    {
    	if ((fs_dynamic_handlers[i].in_use==0)&&(fs_dynamic_handlers[i].still_processing==0))
        {
    		fs_dynamic_handlers[i].in_use=1;
            return &fs_dynamic_handlers[i];
        }
    }
    return 0;
}

void cgi_free(_fs_dynamic_handler *in)
{
    int i;
    if (in==0) return;
    for(i=0; i<HTTPFS_NUM_DYNAMIC_HANDLERS; i++)
    {
    	if (&fs_dynamic_handlers[i]==in)
        {
    	    if (fs_dynamic_handlers[i].still_processing)
    	    {
    	        debug_printf("CGI is still processing, force it to quiet\r\n");
    	        fs_dynamic_handlers[i].force_quit=1;
    	        xSemaphoreGive(fs_dynamic_handlers[i].data_sem);
    	    }
    		fs_dynamic_handlers[i].in_use=0;
            return;
        }
    }
}





/*-----------------------------------------------------------------------------------*/
void fs_init(void)
{
	static unsigned int threads_started=0;
	int i;

	if (threads_started==0) // this function (fs_init) can be called multiple times, at boot and after updating webpages, we only need to start threads the first time.
	{
		threads_started=1;
		memset(fs_dynamic_handlers, 0, sizeof(fs_dynamic_handlers));

		// Create Threads
		for(i=0; i<HTTPFS_NUM_DYNAMIC_HANDLERS; i++)
		{
			fs_dynamic_handlers[i].work_sem = xSemaphoreCreateBinary();
			fs_dynamic_handlers[i].data_sem = xSemaphoreCreateBinary();
			queue_init(&fs_dynamic_handlers[i].data_queue, dynamic_handler_buffers[i], HTTPFS_DYNAMIC_HANDLERS_SIZE);

			if ((fs_dynamic_handlers[i].work_sem) && (fs_dynamic_handlers[i].data_sem))
			{
				if(xTaskCreate(cgi_task, (signed portCHAR *)"CGI TASK", CGI_TASK_STACK_SIZE, &fs_dynamic_handlers[i], tskIDLE_PRIORITY + PRIORITY_CGI_TASK, NULL) != pdTRUE)
				{
					debug_printf("!!!!\r\n\r\nFAILED CREATING THREAD: %d!\r\n", i);
				}
			}
			else
			{
				debug_printf("FAILED CREATING SEM: %d\r\n", i);
			}
		}
	}


    // read in _fs_file_entry
    FlashRead((unsigned char*)&file_header, FS_HEADER_OFFSET, sizeof(_fs_file_header));
    if (file_header.romfs_magic != FS_MAGIC)
    {
        debug_printf("fs_init: romfs_magic is wrong: read %X, expected %X\r\n", file_header.romfs_magic, FS_MAGIC);
        file_header.num_entries=0;
        romfs_files=NULL;
        return;
    }
    else
    {
        romfs_files=actual_romfs_files;
        if (romfs_files==NULL)
        {
            debug_printf("fs_init: error allocating memory for romfs_files\r\n");
            return;
        }

        FlashRead((unsigned char*)romfs_files, FS_INDEX_OFFSET, file_header.num_entries*sizeof(_fs_file_entry));
    }
}

/*-----------------------------------------------------------------------------------*/
static _fs_file * fs_malloc(void)
{
    int i;
    for(i = 0; i < HTTPFS_NUM_OPEN_FILES; i++)
    {
        if(fs_memory[i].in_use == 0)
        {
            fs_memory[i].in_use = 1;
            memset(&fs_memory[i].file, 0, sizeof(_fs_file));
            return(&fs_memory[i].file);
        }
    }
    return(NULL);
}

/*-----------------------------------------------------------------------------------*/
static void fs_free(_fs_file *file, char *val)
{
    int i;
    
    for(i = 0; i < HTTPFS_NUM_OPEN_FILES; i++)
    {
        if(&fs_memory[i].file == file) 
        {
            if (file->cgi_str!=NULL) // cgi file
            {
                if (file->write)
                {
                	file->len=0;
                    if (file->cgi_str->write_fn) file->cgi_str->write_fn(file, file->get_val, file->cgi_str->process_fn, 0);
                }
            }

            if (file->dynamic_handler!=NULL) cgi_free(file->dynamic_handler);
            fs_memory[i].in_use = 0;
            break;
        }
    }
    return;
}

/*-----------------------------------------------------------------------------------*/




/*-----------------------------------------------------------------------------------*/



int fs_printf(_fs_file *file, const char *fmt, ...)
{
    int length;
    int queued_length;
    va_list ap;
 
    if (file->dynamic_handler->force_quit) return 0;

    va_start(ap,fmt);
    length = vsprintf(file->fs_printf_t, fmt, ap);
    va_end(ap);
    
    while(((file->dynamic_handler->data_queue.data_size-sizeof_queue(&file->dynamic_handler->data_queue)-1)<length)&&(file->dynamic_handler->force_quit==0))
    {
        xSemaphoreTake(file->dynamic_handler->data_sem, portMAX_DELAY);
    }

    queued_length = enqueue(&file->dynamic_handler->data_queue, (unsigned char*)file->fs_printf_t, length, 0);
    if (queued_length!=length)
    {
    	debug_printf("Failed to queue!\r\n");
    }
    //debug_printf("Enqueued %d of %d bytes\r\n", queued_length, length);

    file->len+=length;
    return length;
}

_fs_cgi_str *fs_cgi=NULL;

void fs_register_cgi(_fs_cgi_str *cgi)
{
    _fs_cgi_str *temp;
    cgi->next=NULL;
    
    if (fs_cgi==NULL)
    {
        fs_cgi=cgi;
    }
    else
    {
        for(temp=fs_cgi; temp->next!=NULL; temp=temp->next);
        
        temp->next=cgi;
    }
}


/*-----------------------------------------------------------------------------------*/
_fs_file *fs_open(char *name, int write, char *val, int content_length)
{
    _fs_file *file;
    _fs_cgi_str *cgi_ptr;
    int i;
    char rename[80];
    
    file = fs_malloc();
    if(file == NULL) 
    {
        return NULL;
    }

    memset(file, 0, sizeof(_fs_file));

    to_lower(name);
    
    //debug_printf("fs_open '%s'\r\n", name);
    
    if (strcmp((char*)name,"404.html")==0)
    {
        file->data=(unsigned char*)err_404;
        file->len=sizeof(err_404);
        file->index=0;
        file->loc=FILE_IN_RAM;
        file->cgi_str=NULL;
        return file;
    }

    else if (strcmp((char*)name, "401.html")==0)
    {
    	file->data=(unsigned char*)err_401;
		file->len=sizeof(err_401);
		file->index=0;
		file->loc=FILE_IN_RAM;
		file->cgi_str=NULL;
		return file;
    }

    else if (fs_cgi!=NULL)
    {
        // check for cgi files
        for(cgi_ptr=fs_cgi; cgi_ptr!=NULL; cgi_ptr=cgi_ptr->next)
        {
            if (strncmp((char*)cgi_ptr->name, (const char*)name, strlen((const char*)cgi_ptr->name))==0)
            {
                file->len     = 0;
                file->index   = 0;
                file->loc     = FILE_IN_QUEUE;
                file->cgi_str = cgi_ptr;
                file->write   = write;
                file->dynamic_handler=NULL;

                if (val!=NULL) strncpy((char*)file->get_val, val, HTTPFS_DYNAMIC_GET_VAL_LENGTH);
				else file->get_val[0]='\0';
				file->get_val[HTTPFS_DYNAMIC_GET_VAL_LENGTH-1]='\0';

            	if(write) // write
                {
                	if(cgi_ptr->write_fn==NULL) // test write_fn exists
                	{
	                	debug_printf("Error, POST to CGI w/o write function!\n");
	                	fs_free(file, val);
	                	file->cgi_str=NULL;
	                	return NULL;
                	}
                	else
                	{
                		cgi_ptr->write_fn(file, file->get_val, file->cgi_str->process_fn, content_length);
                	}
                }
                else // read only
                {
						file->dynamic_handler = cgi_malloc();

						if (file->dynamic_handler==NULL)
						{
							debug_printf("Error, cannot allocate CGI file '%s'!\n", name);
							file->cgi_str=NULL; // null out so we don't call the write function
							fs_free(file, val);
							return NULL;
						}
						else
						{
							queue_drain(&file->dynamic_handler->data_queue);
							//file->data = file->cgi_str->dynamic_handler->buffer;
							//file->dynamic_handler->cgi = file->cgi_str;
							file->dynamic_handler->file = file;
						}

						// we call the read function now to populate the data
						if(cgi_ptr->read_fn != NULL)
						{
							file->dynamic_handler->still_processing=1;
							file->dynamic_handler->force_quit=0;
							xSemaphoreGive(file->dynamic_handler->work_sem);
						}
	                }

                return file;
            }
        }
    }

    
    for(i=0; i<file_header.num_entries; i++)
    {
        if (strcmp((const char*)name, (const char*)romfs_files[i].name)==0)
        {
            file->data  = FS_FLASH_OFFSET+(unsigned char*)romfs_files[i].offset;
            file->len   = romfs_files[i].length;
            file->index = 0;
            file->loc   = FILE_IN_FLASH;
            file->write = 0;
            return file;
        }
    }


    // check for a *.gz file
    sprintf(rename,"%s.gz", (char*)name);
    
    for(i=0; i<file_header.num_entries; i++)
    {
        if (strcmp(rename, romfs_files[i].name)==0)
        {
            file->data  = FS_FLASH_OFFSET+(unsigned char*)romfs_files[i].offset;
            file->len   = romfs_files[i].length;
            file->index = 0;
            file->loc   = FILE_IN_FLASH;
            file->write = 0;
            return file;
        }
    }

    fs_free(file, NULL);
    return NULL;
}

/*-----------------------------------------------------------------------------------*/
void fs_close(_fs_file *file, char *val)
{
   fs_free(file, val);
}
/*-----------------------------------------------------------------------------------*/

int fs_write(_fs_file *file, unsigned char *buffer, int count, int content_length)
{
    if (file->write==0) return 0;
    if ((file->loc!=FILE_IN_RAM)&&(file->loc!=FILE_IN_QUEUE))   return 0; // can only write RAM files
    if (count==0)		return 0;
    
    file->data=buffer;
    file->len=count;
    
    if((file->cgi_str) && (file->cgi_str->write_fn))
    {
		file->cgi_str->write_fn(file, file->get_val, file->cgi_str->process_fn, content_length);
    }

    return count;
}

int fs_unread(_fs_file *file, int count)
{
	debug_printf("Unreading!\r\n");
	if (file->index>count) file->index-=count;
	else                   file->index=0;
	return 0;
}

int fs_read(_fs_file *file, unsigned char *buffer, int count)
{
    int read;
    
    if (file->loc!=FILE_IN_QUEUE)
    {
        if(file->index == file->len) return -1;

        read = file->len - file->index;
        if(read > count) read = count;
    }

    switch(file->loc)
    {
    	case FILE_IN_FLASH:
    		FlashRead(buffer, (unsigned int)(file->data+file->index), read);
    		break;

    	case FILE_IN_RAM:
    		memcpy(buffer, file->data+file->index, read);
    		break;

    	case FILE_IN_QUEUE:
            read = dequeue(&file->dynamic_handler->data_queue, buffer, count);
            //debug_printf("Dequeued %d bytes: len=%d, index=%d\r\n", read, file->len, file->index);
            if ((read==0)&&(file->dynamic_handler->still_processing==0))
            {
                //debug_printf("File is done: handler:%08X file:%08X\r\n", file->dynamic_handler, file);
                return -1;
            }
    		xSemaphoreGive(file->dynamic_handler->data_sem);
    		break;

    	default:
    		return -1;
    }

    file->index += read;
    return read;
}



static void cgi_task(void *pvParameters)
{
    _fs_dynamic_handler *handler = (_fs_dynamic_handler*)pvParameters;

    //debug_printf("cgi_task with handler: %08X\r\n", handler);

    while(1)
    {
    	// Block until we have a file to process
        if (xSemaphoreTake(handler->work_sem, portMAX_DELAY) == pdTRUE )
    	{
        	//debug_printf("CGI working on %s : handler:%08X file:%08X\r\n", handler->file->cgi_str->name, handler, handler->file);

        	// call cgi when it's done
        	handler->file->cgi_str->read_fn(handler->file, handler->file->get_val);

        	handler->still_processing=0;
        	//debug_printf("CGI done: handler:%08X\r\n", handler);
    	}
    }

}

