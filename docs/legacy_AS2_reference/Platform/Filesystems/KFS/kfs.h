#ifndef KFS_H_
#define KFS_H_




#define KFS_CONFIG_SIZE_BYTES		(100*1024*1024)
#define KFS_FIRMWARE_SIZE_BYTES		(10*1024*1024)
#define KFS_EVENT_SIZE_BYTES		(200*1024*1024)
	
#define KFS_FIRMWARE_FD_INDEX 	((int)0)
#define KFS_CONFIG_FD_INDEX 	((int)1)
#define KFS_EVENT_FD_INDEX		((int)2)
#define KFS_LOG_FD_INDEX		((int)3)

typedef enum
{
	KFS_SUCCESS				= -200,
	KFS_BADDISK,
	KFS_WRITE_ERROR,
	KFS_READ_ERROR,
	KFS_SEEK_ERROR,
	
	KFS_BAD_VERSION,
	KFS_UNFORMATTED,
	KFS_MISMATCH_SECTOR_COUNT,
	
	KFS_UNKNOWN_FILE,
	KFS_NOT_INSTALLED,
	
}KFS_RET;

#define KFS_TRUNCATE 	(1<<0)

#define KFS_SEEK_RELATIVE 	1
#define KFS_SEEK_ABSOLUTE 	2

KFS_RET kfs_disk_state(void);

KFS_RET kfs_init(void);
KFS_RET kfs_sync(void);
KFS_RET kfs_format(void);
KFS_RET kfs_open(int fd_index, unsigned int flags);
KFS_RET kfs_free(int fd_index, unsigned int length);
KFS_RET kfs_seek(int fd_index, long long offset, unsigned int type);
int kfs_eof(int fd_index);
unsigned long long kfs_file_size(int fd_index);
unsigned long long kfs_file_allocated_size(int fd_index);
int kfs_read(int fd_index, void *buffer, unsigned int length);
int kfs_write(int fd_index, void *buffer, unsigned int length);
char *kfs_gets(int fd_index, char *buffer, unsigned int max_length);
void kfs_print_stats(void);
char *kfs_strerror(KFS_RET error);
void kfs_periodic(void);

#endif /*KFS_H_*/
