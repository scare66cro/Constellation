#ifndef KFS_PORT_H_
#define KFS_PORT_H_

#include "kfs.h"

void kfs_timer(void);
unsigned int kfs_get_sector_count(void);

KFS_RET kfs_disk_initialize(void);
KFS_RET kfs_write_sector(const unsigned char *buff, unsigned int sector, unsigned int count);
KFS_RET kfs_read_sector(unsigned char *buff, unsigned int sector, unsigned int count);


#endif /*KFS_PORT_H_*/
