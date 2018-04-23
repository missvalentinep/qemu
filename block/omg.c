#include "block/omg.h"
#include <stdio.h>
#include <time.h>

bool firstRequest = false;
int startOfPartition = 0;
int sectorSize = 512;
int clusterSize = 0;
int startOfMft = 0;
unsigned char SUPPORTED_FILE_SYSTEM = 0x83;

void log_request(BdrvChild *child,
                 int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
                 BdrvRequestFlags *flags) {

    time_t rawtime;
    struct tm * timeinfo;
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );

    qemu_log("%s Reading from offset %i\n", asctime (timeinfo), offset);

    if (file_system_is_correct(child, flags)){

        if (firstRequest == false) {
            firstRequest = true;
            initial_read_of_disk(child, flags);
        }
    }
}


int read_disk_image(unsigned char *buffer, BdrvChild *file, uint64_t offset, size_t len, BdrvRequestFlags *flags) {

    unsigned char temp_buffer[len];

    QEMUIOVector qiov;
    qemu_iovec_init(&qiov, len);
    qemu_iovec_add(&qiov, &temp_buffer, len);

    bdrv_co_preadv(file, offset, len, &qiov, flags);

    size_t recv_len = 0;

    while (recv_len < len) {
        recv_len += qemu_iovec_to_buf(&qiov, recv_len, buffer, len - recv_len);

    }

    return 0;
}

bool file_system_is_correct(BdrvChild *child, BdrvRequestFlags *flags){

    unsigned char file_system[1];
    read_disk_image(file_system, child, 450, 1, flags);
    if (file_system[0] != SUPPORTED_FILE_SYSTEM){
        qemu_log("Unsupported\n");
        return false;
    }
    qemu_log("Supported");
    return true;
}

int initial_read_of_disk(BdrvChild *file, BdrvRequestFlags *flags) {

    unsigned char buffer[512];
    read_disk_image(buffer, file, 0, 512, flags);
    startOfPartition = MBRParser(buffer);

    read_disk_image(buffer, file, startOfPartition, 50, flags);

    clusterSize = buffer[13] * sectorSize;
    startOfMft = buffer[48] * clusterSize + startOfPartition;

    qemu_log("Start of MFT: %i, cluster size: %i\n", startOfMft, clusterSize);
    return 0;
}


int MBRParser(unsigned char *MBR) {
    
    int startOfSector;

    qemu_log("File system: %.2X -- ", MBR[450]);
    switch (MBR[450]) {
        case 0x83:
            qemu_log("Linux native partition\n");
            break;
        case 0x7:
            qemu_log("Windows NT NTFS\n");
            break;
        case 0x0e:
            qemu_log("WIN95: DOS 16-bit FAT, LBA-mapped\n");
            break;
        default:
            qemu_log("Unknown file system\n");
    }



    startOfSector = MBR[456] * 4096 + MBR[455] * 256 + MBR[454];
    startOfSector *= 512;
    qemu_log("Starting sector: %.2X%.2X%.2X%.2X -- position:%i \n", MBR[457], MBR[456], MBR[455], MBR[454],
           startOfSector);


    return startOfSector;

}

