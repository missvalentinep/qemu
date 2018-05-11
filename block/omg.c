#include "block/omg.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

bool firstRequest = false;
int numOfFilesToRead = 100; //corresponding value arrayOfFilesSize;
struct File arrayOfFiles[100];
int startOfPartition = 0;
int sectorSize = 512;
int clusterSize = 0;
int startOfMft = 0;
unsigned char SUPPORTED_FILE_SYSTEM = 0x83;
int SIZE_OF_CHILDREN_ARRAY = 512; // corresponding value in header
BdrvChild *disk_image;
int fileCount;

void log_request(BdrvChild *child,
                 int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
                 BdrvRequestFlags *flags) {


    if (flags) {                                            //If flags exist
        if (*flags != BDRV_REQ_FROM_MODULE) {               //and the bdrv_co_preadv wasn't called from this module

            if (file_system_is_correct(child)) {           //check if this file system is supported, continue if true

                if (firstRequest == false) {               //if the tree of files was not already built, do it
                    firstRequest = true;
                    disk_image = child;
                    initial_read_of_disk(child);

                }

                time_t rawtime;
                struct tm *timeinfo;
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                qemu_log("%s Reading from offset %lli\n", asctime(timeinfo), offset); //log request
                determine_file(offset);
            }
        }
    }
}

int determine_file(int offset) {

    int sector_num = offset / 512;

    for (int i = 0; i < fileCount; i++) {
        if (arrayOfFiles[i].startingSector == sector_num ||
            arrayOfFiles[i].endingSector == sector_num) {
            qemu_log("!! Reading file: %s \n",arrayOfFiles[i].fullPath );
            return 0;
        }
    }
    for (int i = 0; i < fileCount; i++) {
        if (arrayOfFiles[i].numOfOuterAttributes > 0){
            for (int k = 0; k < arrayOfFiles[i].numOfOuterAttributes; k++ ){
                if (arrayOfFiles[i].outerAttributes[k].startingSectorNumber == sector_num){
                    qemu_log("!!? Reading file: %s \n",arrayOfFiles[i].fullPath );
                    return 0;
                }
            }
        }
    }
        qemu_log("Error determining file\n ");
    return -1;
}

int read_disk_image(unsigned char *buffer, BdrvChild *file, uint64_t offset, size_t len) {

    unsigned char temp_buffer[len];

    QEMUIOVector qiov;
    qemu_iovec_init(&qiov, len);
    qemu_iovec_add(&qiov, &temp_buffer, len);

    bdrv_co_preadv(file, offset, len, &qiov, BDRV_REQ_FROM_MODULE);

    size_t recv_len = 0;

    while (recv_len < len) {
        recv_len += qemu_iovec_to_buf(&qiov, recv_len, buffer, len - recv_len);

    }

    return 0;
}

bool file_system_is_correct(BdrvChild *child) {

    unsigned char file_system[2];
    read_disk_image(file_system, child, 450, 2);

    if (file_system[0] != SUPPORTED_FILE_SYSTEM) {
        qemu_log("Unsupported\n");
        return false;
    }
    qemu_log("Supported!\n");
    return true;
}

int initial_read_of_disk(BdrvChild *file) {

    unsigned char buffer[512];

    fileCount = 0;
    read_disk_image(buffer, file, 0, 512);
    startOfPartition = MBRParser(buffer);

    read_disk_image(buffer, file, startOfPartition, 50);

    clusterSize = buffer[13] * sectorSize;
    startOfMft = buffer[48] * clusterSize + startOfPartition;

    qemu_log("Start of MFT: %i, cluster size: %i\n", startOfMft, clusterSize);

    for (fileCount = 0; fileCount < numOfFilesToRead; fileCount++) {

        struct File newFile;

        read_disk_image(newFile.contents, file, startOfMft + 1024 * fileCount, 1024); //reading file entry contents
        newFile.startingSector = (startOfMft + 1024 * fileCount) / 512;
        newFile.endingSector = newFile.startingSector + 1;
        newFile.numberInMFT = fileCount; //Record number in MFT

        if ((newFile.contents[25] * 256 + newFile.contents[24]) == 0) {
            printf("End of files\n");
            break;
        }
        newFile = FileParser(newFile);
        arrayOfFiles[fileCount] = newFile;

    }

    for (int i = 0; i < fileCount; i++) {
        printf("\n %i File Path:\n", i);
        arrayOfFiles[i] = printParentFile(i, arrayOfFiles, arrayOfFiles[i]);
        printf("%s \n", arrayOfFiles[i].fullPath);
    }

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

struct File FileParser(struct File file) {
    int i;
    int nextAttributeOffset;
    i = 0;
    int parentNumber = -1;


    file.parentNumberInMFT = parentNumber; //default is root directory
    file.nameLength = 0; //default name length is 0
    file.sizeInBytes = file.contents[25] * 256 + file.contents[24];

    for (int i = 0; i < SIZE_OF_CHILDREN_ARRAY; i++) {    //default
        file.childrenNumbersInMFT[i] = 0;
    }

    file.numOfChildren = 0;  //default
    file.numOfOuterAttributes = 0;  //default

    //--------------------------------------------- update sequence array & fix up

    int updateSequenceOffset = file.contents[4] + file.contents[5] * 256;

    file.contents[510] = file.contents[updateSequenceOffset + 2];
    file.contents[511] = file.contents[updateSequenceOffset + 3];
    file.contents[1022] = file.contents[updateSequenceOffset + 2];
    file.contents[1023] = file.contents[updateSequenceOffset + 3];

    //-------------------------------------------------


    int numOfAttributes = 0;
    struct Attribute newAttribute = AttributeParser(file.contents, (int) file.contents[20]);
    file.attributes[0] = newAttribute;
    numOfAttributes++;
    nextAttributeOffset = newAttribute.nextAttributeOffset;

    while (nextAttributeOffset != -1) {
        newAttribute = AttributeParser(file.contents, nextAttributeOffset);
        nextAttributeOffset = newAttribute.nextAttributeOffset;
        file.attributes[numOfAttributes] = newAttribute;
        numOfAttributes++;

        if (newAttribute.attributeType == 0x30) {     //Adding info about parent & file name
            parentNumber = newAttribute.attributeContent[0] + newAttribute.attributeContent[1] * 256;
            file.parentNumberInMFT = parentNumber;
            file.nameLength = ((int) newAttribute.attributeContent[4]) * 2;

            //Через символ идет пустой символ, искажает вид файла, поэтому убираем
            for (int j = 0; j < file.nameLength; j += 2) {
                file.fileName[j / 2] = newAttribute.attributeContent[5 + j];

            }
            //заполняем остаток имени пустыми символами
            for (int j = file.nameLength / 2; j < file.nameLength; j++) {
                file.fileName[j] = '\0';
            }

        } else if (newAttribute.attributeType == 0x90) {
            file = getChildrenForDirectory(file, newAttribute);
        } else if (newAttribute.attributeType == 0xa0) {
            file = indexAllocationParser(file, newAttribute);
        } else if (newAttribute.attributeType == 0x80) {
            file = dataAttributeParser(file, newAttribute);
        }
    }

    return file;
}

struct File getChildrenForDirectory(struct File file, struct Attribute attribute) {

    int nameLengthOffset = 9;
    int nameLength = file.contents[attribute.attributeOffset + nameLengthOffset];
    int indexEntryOffset = 32;
    int indexEntrySize;

    int attributeBodyOffset = 24 + nameLength * 2;
    int attributeBody = attribute.attributeOffset + attributeBodyOffset;
    int fileReference = attributeBody + indexEntryOffset;

    while (file.contents[fileReference] != 0) {
        file.childrenNumbersInMFT[file.numOfChildren] =
                file.contents[fileReference] + file.contents[fileReference + 1] * 256;
        indexEntrySize = file.contents[fileReference + 8];
        fileReference += indexEntrySize;
        file.numOfChildren++;
    }

    return file;

}

struct File indexAllocationParser(struct File file,
                                  struct Attribute attribute) {

    int nameLength = file.contents[attribute.attributeOffset + 9];
    int dataRunsOffset = attribute.attributeOffset + nameLength * 2 + 64;
    unsigned char dataRunHeader = 0x90;
    int lengthOfOffset;
    int offsetAsInteger;
    int previousOffset = 0;

    while (1) {

        dataRunHeader = file.contents[dataRunsOffset];
        if ((int) dataRunHeader / 16 == 0) {
            break;
        }
        offsetAsInteger = 0;
        lengthOfOffset = (int) dataRunHeader / 16;
        int nextOffset = lengthOfOffset + dataRunHeader % 8;
        unsigned char offset[lengthOfOffset];

        dataRunsOffset = dataRunsOffset + 1 + dataRunHeader % 6;

        for (int i = 0; i < lengthOfOffset; i++) {
            offset[i] = file.contents[dataRunsOffset];
            dataRunsOffset++;
        }

        for (int i = lengthOfOffset - 1; i >= 0; i--) {
            offsetAsInteger += offset[i] * (int) pow(256, i);
        }

        offsetAsInteger += previousOffset; // data runs взаимосвязаны и смещение указано относительно предыдущего


        unsigned char sizeInWordsOfUpdateSequence[2];
        read_disk_image(sizeInWordsOfUpdateSequence, disk_image, offsetAsInteger * clusterSize + startOfPartition + 6,
                        2);

        unsigned char updateSequence[2];
        read_disk_image(updateSequence, disk_image, offsetAsInteger * clusterSize + startOfPartition + 40,
                        2);  // update sequence number

        unsigned char updateSequenceArray[8];
        read_disk_image(updateSequence, disk_image, offsetAsInteger * clusterSize + startOfPartition + 42, 8);

        unsigned char indxEntrySize[4];
        int bufferSize = 0;
        read_disk_image(indxEntrySize, disk_image, offsetAsInteger * clusterSize + startOfPartition + 28,
                        4);  // Размер INDX Entry

        for (int i = 3; i >= 0; i--) {
            bufferSize += indxEntrySize[i] * (int) pow(256, i);
        }

        file.outerAttributes[file.numOfOuterAttributes].startingSectorNumber =
                (offsetAsInteger * clusterSize + startOfPartition) / 512;
        file.outerAttributes[file.numOfOuterAttributes].size = bufferSize;
        file.outerAttributes[file.numOfOuterAttributes].attributeType = 0x90;
        file.numOfOuterAttributes++;


        unsigned char buffer[bufferSize];
        read_disk_image(buffer, disk_image, offsetAsInteger * clusterSize + startOfPartition, bufferSize);

        if (bufferSize == 0) {
            break;                          //TODO: CHECK IF THIS IS CORRECT -- WHY IT HAPPENS
        }
        if (false) {
            LABEL:                          //TODO: THIS TOO
            break;
        }
        int j = 511;
        int indexOfUSA = 0;
        unsigned char valuesOfUSA[2];


        //--------------------------------------------- fix up
        bool badRecord = false;
        while (j < bufferSize) {                    //DOING FIX UP!!!!!!!!

            if (buffer[j - 1] != updateSequence[0] || buffer[j] != updateSequence[1]) {
                badRecord = true;
                goto LABEL;

            }
            read_disk_image(valuesOfUSA, disk_image, offsetAsInteger * clusterSize + startOfPartition + 42 + indexOfUSA,
                            2);  //переходим к конкретному элементу USA и берем два нужных значения
            buffer[j - 1] = valuesOfUSA[0];
            buffer[j] = valuesOfUSA[1];
            indexOfUSA += 2;
            j += 512;
        }
        //---------------------------------------------

        int i = 0;
        int firstIndexEntryOffset = 0;

        for (i = 3; i >= 0; i--) {
            firstIndexEntryOffset += buffer[24 + i] * (int) pow(256, i);
        }

        firstIndexEntryOffset += 24;
        int fileReference = firstIndexEntryOffset;
        int indexEntrySize = 0;

        while (fileReference < bufferSize) {
            file.childrenNumbersInMFT[file.numOfChildren] = buffer[fileReference] + buffer[fileReference + 1] * 256;
            indexEntrySize = buffer[fileReference + 8];
            indexEntrySize += buffer[fileReference + 9] * 256;
            fileReference += indexEntrySize;
            file.numOfChildren++;
        }
        previousOffset = offsetAsInteger;
    }
    return file;
}

struct File dataAttributeParser(struct File file, struct Attribute attribute) {

    int nameLength = file.contents[attribute.attributeOffset + 9];
    int dataRunsOffset = attribute.attributeOffset + nameLength * 2 + 64;

    unsigned char dataRunHeader;
    int lengthOfOffset;
    int offsetAsInteger;

    while (1) {
        dataRunHeader = file.contents[dataRunsOffset];
        if (dataRunHeader == 0x00) {
            break;
        }
        offsetAsInteger = 0;
        lengthOfOffset = (int) dataRunHeader / 16;
        unsigned char offset[lengthOfOffset];

        dataRunsOffset += 2;
        for (int i = 0; i < lengthOfOffset; i++) {
            offset[i] = file.contents[dataRunsOffset];
            dataRunsOffset++;
        }

        for (int i = lengthOfOffset - 1; i >= 0; i--) {
            offsetAsInteger += offset[i] * (int) pow(256, i);
        }
    }

    return file;
}

struct Attribute AttributeParser(unsigned char *file, int attributeOffset) {

    struct Attribute newAttribute;
    newAttribute.redident = true;
    if ((int) file[attributeOffset + 4] == 0x00 || file[attributeOffset] == 0xFF) {    //no more attributes
        newAttribute.nextAttributeOffset = -1;
        return newAttribute;
    }
    int headerSize = 24;
    newAttribute.attributeOffset = attributeOffset;

    int attributeSize = file[attributeOffset + 4] + file[attributeOffset + 5] * 256;
    newAttribute.attributeType = file[attributeOffset];

    switch (file[attributeOffset]) {
        case 0x10:
//            printf("$STANDARD_INFORMATION \n");
            break;
        case 0x20:
//            printf("$ATTRIBUTE_LIST\n");
            break;
        case 0x30:
//            printf("$FILE_NAME\n");
            for (int i = 0; i < 3; i++) {
                newAttribute.attributeContent[i] = file[attributeOffset + headerSize + i];   //parent directory
            }
            newAttribute.attributeContent[4] = file[attributeOffset + headerSize + 64];  //filename length

            for (int i = 0; i < (int) file[attributeOffset + headerSize + 64] * 2; i++) {
                newAttribute.attributeContent[5 + i] = file[attributeOffset + headerSize + 66 + i];
            }
            break;
        case 0x50:
//            printf("$SECURITY_DESCRIPTOR\n");
            break;

        case 0x80:
//            printf("$DATA\n");
            break;
        case 0x90:
//            printf("$INDEX_ROOT\n");
            break;
        case 0xa0:
//            printf("$INDEX_ALLOCATION\n");
            break;
        case 0xb0:
//            printf("$BITMAP\n");
            break;
        default:
            break;
    }
    newAttribute.nextAttributeOffset = attributeOffset + attributeSize;
    return newAttribute;

}

struct File printParentFile(int index, struct File arrayOfFiles[100], struct File originalFile) {

    struct File currentFile = arrayOfFiles[index];

    if (currentFile.parentNumberInMFT == -1) {
        strcat(originalFile.fullPath, "No directory");
        return originalFile;
    }

    char *str1 = originalFile.fullPath;
    char *str2 = currentFile.fileName;

    if (strcmp(originalFile.fullPath, "") != 0) {        // После самого файла не печатает слеш
        strcat(str2, "/");
    }

    strcat(str2, str1);
    strcpy(originalFile.fullPath, str2);

    if (currentFile.fileName[0] != '.') {
        return printParentFile(currentFile.parentNumberInMFT, arrayOfFiles, originalFile);
    }
    return originalFile;

}