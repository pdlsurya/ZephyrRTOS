/**
 * @file SdFat.h
 * @author Surya Poudel
 * @brief
 * @version 0.1
 * @date 2023-07-05
 * @copyright Copyright (c) 2023, Surya Poudel
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __MYSDFAT_H
#define __MYSDFAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <SD_drv.h>

#define BOOT_SEC_START 0x00002000
#define FSInfo_SEC 0x00002001

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_FILE_NAME 0x0F

#define ATTR_LONG_NAME_MASK (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)

#define FAT_EOC 0x0FFFFFF8

typedef enum
{
    FAT12,
    FAT16,
    FAT32
} FATtype;

typedef struct
{
    uint32_t Cluster;
    uint8_t sectorIndex;
    uint8_t entryIndex;
    uint8_t LFN_EntCnt;
} fileEntInf_t;

typedef struct
{
    uint32_t FSI_LeadSig;
    uint8_t FSI_Reserved1[480];
    uint32_t FSI_StrucSig;
    uint32_t FSI_Free_Count;
    uint32_t FSI_Nxt_Free;
    uint8_t FSI_Reserved2[12];
    uint32_t FSI_TrailSig;

} FSInfo_t;

typedef struct
{
    char DIR_Name[8];
    char DIR_ext[3];
    uint8_t DIR_attr;
    uint8_t DIR_NTRes;
    uint8_t DIR_CrtTimeTenth;
    uint16_t DIR_CrtTime;
    uint16_t DIR_CrtDate;
    uint16_t DIR_LstAccDate;
    uint16_t DIR_FstClusHI;
    uint16_t DIR_WrtTime;
    uint16_t DIR_WrtDate;
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
    uint32_t entryIndex;
    fileEntInf_t fileEntInf;

} myFile;

extern char fileName[128];

static inline uint32_t startCluster(myFile *pFile)
{
    uint32_t startClus = (uint32_t)pFile->DIR_FstClusLO;
    startClus |= ((uint32_t)(pFile->DIR_FstClusHI)) << 16;
    return startClus;
}

static inline bool isDirectory(myFile *pFile)
{
    return !(((pFile->DIR_attr & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == 0));
}

static inline bool isEndOfDir(myFile *pFile)
{
    return ((uint8_t)(pFile->DIR_Name[0]) == 0);
}

static inline bool isValidFile(myFile *pFile)
{
    return (startCluster(pFile) != 0) && !(isEndOfDir(pFile) || (fileName[0] == '.' && fileName[1] == '_'));
}

static inline uint32_t fileSize(myFile *pFile)
{
    return pFile->DIR_FileSize;
}

static inline uint8_t fileLfnEntCnt(myFile *pFile)
{
    return pFile->fileEntInf.LFN_EntCnt;
}

static inline void fileReset(myFile *pFile)
{
    // stop any on going multiple secotrs read
    SD_readMultipleSecStop();

    // reset index;
    pFile->entryIndex = 0;
}

static inline void fileClose(myFile *pFile)
{
    memset(pFile, 0, sizeof(myFile));
}

bool mySdFat_init(SPI_HandleTypeDef *hspi);

bool listDir(const char *path);

myFile fileOpen(const char *path, const char *filename);

void fileClose(myFile *pFile);

uint8_t readByte(myFile *pFile);

myFile createDirectory(const char *path, const char *dirName);

bool fileWrite(myFile *pFile, const char *data);

bool fileDelete(const char *path, const char *filename);

myFile nextFile(myFile *pFile);

extern char fileName[128];

typedef struct
{
    uint32_t fatSecNum;
    uint16_t fatEntOffset;
} fatEntLoc_t;

typedef fileEntInf_t freeEntInf_t;

typedef struct
{
    uint8_t LDIR_Ord;
    char LDIR_Name1[10];
    uint8_t LDIR_Attr;
    uint8_t LDIR_Type;
    uint8_t LDIR_Chksum;
    char LDIR_Name2[12];
    uint16_t LDIR_FstClusLO;
    char LDIR_Name3[4];
} LFN_entry_t;

typedef struct
{
    uint16_t BPB_BytesPerSec;
    uint8_t BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint32_t BPB_TotSec32;
    uint32_t BPB_FATSz32;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    char BS_VolLab[11];
} bootSecParams_t;

#endif
