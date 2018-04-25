#ifndef OMG_H
#define OMG_H

#include "string.h"
#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qapi/error.h"
#include "qemu/option.h"
#include "exec/log.h"


struct Attribute {
    int attributeOffset;
    unsigned char attributeType;
    int nextAttributeOffset;
    unsigned char attributeContent[50];    //TODO: 50 - only for parent location and name, change later!!!
    bool redident;

};

struct outerAttribute {
    int startingSectorNumber;
    int size;
    unsigned char attributeType; //0xa0 - index allocation, 0x80 - data
};

struct File {

    unsigned char contents[1024];
    int sizeInBytes;
    int numberInMFT;
    struct Attribute attributes[10];
    int parentNumberInMFT;
    char fileName[50];
    int nameLength;
    char fullPath[100];
    int childrenNumbersInMFT[512];
    int numOfChildren;
    int startingSector;
    int endingSector;
    struct outerAttribute outerAttributes[10];
    int numOfOuterAttributes;

};

void log_request(BdrvChild *child,
                 int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
                 BdrvRequestFlags *flags);

int read_disk_image(unsigned char *buffer, BdrvChild *file, uint64_t offset, size_t len);

bool file_system_is_correct(BdrvChild *child);

int initial_read_of_disk(BdrvChild *file);

int determine_file(int offset);

int MBRParser(unsigned char *MBR);

struct Attribute AttributeParser(unsigned char *file, int attributeOffset);

struct File getChildrenForDirectory(struct File file, struct Attribute attribute);

struct File indexAllocationParser(struct File file, struct Attribute attribute);

struct File dataAttributeParser(struct File file, struct Attribute attribute);

struct File FileParser(struct File file);

struct File printParentFile(int index, struct File arrayOfFiles[100], struct File originalFile);

#endif