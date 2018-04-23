#ifndef OMG_H
#define OMG_H

#include "string.h"
#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qapi/error.h"
#include "qemu/option.h"
#include "exec/log.h"
#include <glib.h>



void log_request(BdrvChild *child,
                 int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
                 BdrvRequestFlags* flags);

int read_disk_image(unsigned char* buffer, BdrvChild *file, uint64_t offset, size_t len, BdrvRequestFlags* flags);
bool file_system_is_correct(BdrvChild *child, BdrvRequestFlags *flags);
int initial_read_of_disk(BdrvChild *file, BdrvRequestFlags *flags);
int MBRParser(unsigned char *MBR);
#endif