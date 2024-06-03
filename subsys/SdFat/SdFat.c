/**
 * @file SdFat.c
 * @author Surya Poudel (poudel.surya2011@gmail.com)
 * @brief FAT32 driver for SD/MMC Card
 * @version 0.1
 * @date 2023-07-05
 *
 * @copyright Copyright(c) 2023, Surya Poudel
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
// #include <zephyr/sd/sdmmc.h>
// #include <zephyr/drivers/sdhc.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SD_drv.h>
#include <SdFat.h>

static bootSecParams_t params;

__aligned(4) static uint8_t SD_buff[512];

static uint32_t FatStartSector;
static uint32_t FatSectorsCnt;

static uint32_t RootDirStartSector;
static uint32_t RootDirSectors;

static uint32_t DataStartSector;
static uint32_t DataSectorsCnt;

static char fileName[128] = "";
static uint8_t fileNameIndex;
/*

#define SD_READ_SUCCESS 0
#define SD_WRITE_SUCCESS 0
#define SD_WRITE_ERROR -1


static const struct device *sd_dev = DEVICE_DT_GET(DT_NODELABEL(microsd));

static struct sd_card sdcard;


static inline int SD_readSector(uint32_t address, uint8_t *rxbuf)
{
    return sdmmc_read_blocks(&sdcard, rxbuf, address, 1);
}

static inline int SD_writeSector(uint32_t address, uint8_t *txbuf)
{
    return sdmmc_write_blocks(&sdcard, txbuf, address, 1);
}
*/

static inline char *getFileName()
{
    return &fileName[0];
}

static bool getBootSecParams()
{
    if (SD_readSector(BOOT_SEC_START, SD_buff) == SD_READ_SUCCESS)
    {

        params.BPB_BytesPerSec = (uint16_t)SD_buff[11];
        params.BPB_BytesPerSec |= (uint16_t)(SD_buff[12] << 8);

        params.BPB_SecPerClus = SD_buff[13];

        params.BPB_RsvdSecCnt = (uint16_t)SD_buff[14];
        params.BPB_RsvdSecCnt |= ((uint16_t)SD_buff[15]) << 8;

        params.BPB_TotSec32 = ((uint32_t)SD_buff[32]);
        params.BPB_TotSec32 |= ((uint32_t)SD_buff[33]) << 8;
        params.BPB_TotSec32 |= ((uint32_t)SD_buff[34]) << 16;
        params.BPB_TotSec32 |= ((uint32_t)SD_buff[35]) << 24;

        params.BPB_FATSz32 = ((uint32_t)SD_buff[36]);
        params.BPB_FATSz32 |= ((uint32_t)SD_buff[37]) << 8;
        params.BPB_FATSz32 |= ((uint32_t)SD_buff[38]) << 16;
        params.BPB_FATSz32 |= ((uint32_t)SD_buff[39]) << 24;

        params.BPB_RootEntCnt = (uint16_t)SD_buff[17];
        params.BPB_RootEntCnt |= ((uint16_t)SD_buff[18]) << 8;

        params.BPB_NumFATs = SD_buff[16];

        params.BPB_RootClus = ((uint32_t)SD_buff[44]);
        params.BPB_RootClus |= ((uint32_t)SD_buff[45]) << 8;
        params.BPB_RootClus |= ((uint32_t)SD_buff[46]) << 16;
        params.BPB_RootClus |= ((uint32_t)SD_buff[47]) << 24;

        params.BPB_FSInfo = (uint16_t)(SD_buff[48]);
        params.BPB_FSInfo |= ((uint16_t)(SD_buff[49])) << 8;

        memcpy(params.BS_VolLab, &SD_buff[71], 11);
        params.BS_VolLab[8] = '\0';

        return true;
    }
    return false;
}

static FATtype getFatType()
{
    uint32_t clusterCnt = DataSectorsCnt / params.BPB_SecPerClus;

    if (clusterCnt <= 4085)
        return FAT12;

    else if (clusterCnt >= 4086 && clusterCnt <= 65525)
        return FAT16;

    else
        return FAT32;
}

static fatEntLoc_t fatEntLocation(uint32_t fat_entry_index)
{
    fatEntLoc_t fatEntLoc;
    fatEntLoc.fatSecNum = FatStartSector + (fat_entry_index * 4 / params.BPB_BytesPerSec);
    fatEntLoc.fatEntOffset = (fat_entry_index * 4) % params.BPB_BytesPerSec;
    return fatEntLoc;
}

static uint32_t fatNextClus(uint32_t fatThisClus)
{
    uint32_t temp;
    fatEntLoc_t fatEntLoc = fatEntLocation(fatThisClus);
    SD_readSector(fatEntLoc.fatSecNum, SD_buff);
    temp = *((uint32_t *)&SD_buff[fatEntLoc.fatEntOffset]);

    return temp;
}

static void fatSetNextClus(uint32_t fatThisClus, uint32_t fatNextClus)
{
    uint32_t *p_temp;
    fatEntLoc_t fatEntLoc = fatEntLocation(fatThisClus);
    SD_readSector(fatEntLoc.fatSecNum, SD_buff);
    p_temp = ((uint32_t *)&SD_buff[fatEntLoc.fatEntOffset]);
    memcpy(p_temp, &fatNextClus, 4);
    SD_writeSector(fatEntLoc.fatSecNum, SD_buff);
}

static inline uint32_t startSecOfClus(uint32_t cluster_index)
{
    return (DataStartSector + (cluster_index - 2) * params.BPB_SecPerClus);
}

static void displayTime(uint16_t time)
{
    uint8_t hours = (time & 0xF800) >> 11;
    uint8_t minutes = (time & 0x07E0) >> 5;
    uint8_t seconds = (time & 0x001F) * 2;

    printk(hours > 10 ? "%d" : "0%d", hours);
    printk(":");
    printk(minutes > 10 ? "%d" : "0%d", minutes);
    printk(":");
    printk(seconds > 10 ? "%d" : "0%d", seconds);
}

static void displayDate(uint16_t date)
{
    uint16_t year = 1980;
    year += (date & 0xF700) >> 9;
    uint8_t month = (date & 0x01E0) >> 5;
    uint8_t day = (date & 0x001F);

    printk("%d", year);
    printk("-");
    printk(month < 10 ? "0%d" : "%d", month);
    printk("-");
    printk(day < 10 ? "0%d" : "%d", month);
}

static void getShortFileName(myFile *pFile)
{
    uint8_t nameIndx = 0;
    for (uint8_t index = 0; index < 8; index++)
    {
        if (pFile->DIR_Name[index] == ' ')
            break;

        if (pFile->DIR_NTRes & 0x08)
        {
            if ((pFile->DIR_Name[index] > 64) && (pFile->DIR_Name[index] < 91))
            {
                fileName[nameIndx++] = pFile->DIR_Name[index] + 32;
            }
            else
                fileName[nameIndx++] = pFile->DIR_Name[index];
        }

        else
            fileName[nameIndx++] = pFile->DIR_Name[index];
    }
    if ((pFile->DIR_attr & ATTR_DIRECTORY) == 0)
    {
        if (pFile->DIR_ext[0] != ' ')
        {
            fileName[nameIndx++] = '.';
            for (uint8_t index = 0; index < 3; index++)
                fileName[nameIndx++] = (pFile->DIR_ext[index] == ' ') ? '\0' : pFile->DIR_ext[index] + 32;
        }
    }
}

static inline bool isFreeEntry(myFile *pFile)
{
    return ((uint8_t)(pFile->DIR_Name[0]) == 0xE5);
}

static bool LFN_Entry(myFile *pFile)
{
    return (((pFile->DIR_attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_FILE_NAME) && (((uint8_t)pFile->DIR_Name[0] & 0xF0) == 0x40));
}

myFile rootDir()
{
    SD_readSector(startSecOfClus(params.BPB_RootClus), SD_buff);
    myFile rootDirectory = *((myFile *)&SD_buff[0]);
    rootDirectory.DIR_FstClusLO = 2;
    rootDirectory.DIR_FstClusHI = 0;
    rootDirectory.entryIndex = 1;

    return rootDirectory;
}

/**
 * @brief function to get next file in the folder
 *
 * @param[in] pFolder  pointer to the folder
 * @return next file in the folder
 */

myFile nextFile(myFile *pFolder)
{
    uint8_t sectorIndex = (pFolder->entryIndex / 16) % params.BPB_SecPerClus;
    uint32_t currentClus = startCluster(pFolder);
    myFile temp;

    uint32_t currentClusterIndex = (pFolder->entryIndex / (16 * params.BPB_SecPerClus));

    for (uint8_t i = 0; i < currentClusterIndex; i++)
    {
        currentClus = fatNextClus(currentClus);
        if (currentClus >= FAT_EOC)
        {
            pFolder->entryIndex = 2;
            memset(&temp, 0, sizeof(myFile));
            return temp;
        }
    }

    if (!isDirectory(pFolder))
    {
        printk("Not a Dir\n");
        memset(&temp, 0, sizeof(myFile));
        return temp;
    }

    SD_readSector(startSecOfClus(currentClus) + sectorIndex, SD_buff);

    while (1)
    {
        temp = *((myFile *)(SD_buff + (pFolder->entryIndex % 16) * 32));

        if (!isFreeEntry(&temp))
        {
            if (isEndOfDir(&temp))
            {
                memset(&temp, 0, sizeof(myFile));
                return temp;
            }

            if (LFN_Entry(&temp))
            {
                memset(fileName, 0, sizeof(fileName));
                uint8_t LFN_entryCnt = ((((LFN_entry_t *)&temp)->LDIR_Ord) & 0x0F);
                char tempName[8][13] = {0};

                while (LFN_entryCnt)
                {

                    uint8_t tempNameIndex = 0;
                    LFN_entry_t *entry = (LFN_entry_t *)(SD_buff + (pFolder->entryIndex % 16) * 32);

                    for (uint8_t i = 0; i < 10; i += 2)
                        tempName[LFN_entryCnt - 1][tempNameIndex++] = entry->LDIR_Name1[i];

                    for (uint8_t i = 0; i < 12; i += 2)
                        tempName[LFN_entryCnt - 1][tempNameIndex++] = entry->LDIR_Name2[i];

                    for (uint8_t i = 0; i < 4; i += 2)
                        tempName[LFN_entryCnt - 1][tempNameIndex++] = entry->LDIR_Name3[i];

                    LFN_entryCnt--;
                    pFolder->entryIndex++;

                    if ((pFolder->entryIndex % 16) == 0)
                    {
                        sectorIndex++;

                        if (sectorIndex == params.BPB_SecPerClus)
                        {
                            sectorIndex = 0;
                            currentClus = fatNextClus(currentClus);
                            if (currentClus > FAT_EOC)
                            {
                                pFolder->entryIndex = 2;
                                memset(&temp, 0, sizeof(myFile));
                                return temp;
                            }
                        }

                        SD_readSector(startSecOfClus(currentClus) + sectorIndex, SD_buff);
                    }
                }

                char *p_name = (char *)tempName;
                while ((*p_name) != '\0')
                    fileName[fileNameIndex++] = *(p_name++);

                fileNameIndex = 0;

                temp = *((myFile *)(SD_buff + (pFolder->entryIndex % 16) * 32));
                temp.fileEntInf.Cluster = currentClus;
                temp.fileEntInf.sectorIndex = sectorIndex;
                temp.fileEntInf.entryIndex = pFolder->entryIndex % 16;
                pFolder->entryIndex++;
                break;
            }
            else
            {
                memset(fileName, 0, sizeof(fileName));
                getShortFileName(&temp);
                temp.fileEntInf.Cluster = currentClus;
                temp.fileEntInf.sectorIndex = sectorIndex;
                temp.fileEntInf.entryIndex = pFolder->entryIndex % 16;
                pFolder->entryIndex++;
                break;
            }
        }
        else
        {
            pFolder->entryIndex++;
            if (pFolder->entryIndex % 16 == 0)
            {
                sectorIndex++;
                if (sectorIndex == params.BPB_SecPerClus)
                {
                    sectorIndex = 0;
                    currentClus = fatNextClus(currentClus);
                    if (currentClus > FAT_EOC)
                    {
                        pFolder->entryIndex = 2;
                        memset(&temp, 0, sizeof(myFile));
                        return temp;
                    }
                }
                SD_readSector(startSecOfClus(currentClus) + sectorIndex, SD_buff);
            }
        }
    }
    temp.entryIndex = isDirectory(&temp) ? 2 : 0;

    return temp;
}

static void dispFile(myFile *pFile, char *fileName, uint8_t tab)
{
    for (uint8_t i = 0; i < tab; i++)
        printk("    ");

    printk("%s", fileName);

    if (isDirectory(pFile))
        printk("/");
    printk("     ");

    displayDate(pFile->DIR_WrtDate);

    printk(" || ");
    displayTime(pFile->DIR_WrtTime);

    printk(" || ");

    printk("%d Bytes", pFile->DIR_FileSize);

    /*
    printk(" || ");
    printk("startClus:");
    printk(startCluster(pFile));
    */
    printk("\n");
}

static myFile fileExists(const char *file, myFile *pFolder)
{
    myFile tempFile = {0};

    do
    {
        tempFile = nextFile(pFolder);
        if (isValidFile(&tempFile))
        {
            uint8_t nameIndx;
            for (nameIndx = 0; nameIndx < strlen(file); nameIndx++)
            {
                if (file[nameIndx] != fileName[nameIndx])
                    break;
            }
            if (nameIndx == strlen(file) && nameIndx == strlen(fileName))
                return tempFile;
        }
    } while (!isEndOfDir(&tempFile));
    memset(&tempFile, 0, sizeof(myFile));
    return tempFile;
}

static myFile pathExists(const char *path)
{
    myFile tempFile = rootDir();

    if (strlen(path) == 1 && path[0] == '/')
        return tempFile;

    uint8_t index = 0;
    uint8_t charCnt = 0;
    while (path[charCnt] != '\0')
    {
        char dirName[36] = "";
        for (uint8_t i = index + 1; i < index + 36; i++)
        {
            charCnt++;

            if (path[i] == '/' || path[i] == '\0')
                break;
            dirName[charCnt - index - 1] = path[i];
        }
        index = charCnt;
        tempFile = fileExists(dirName, &tempFile);

        if (startCluster(&tempFile) == 0)
            return tempFile;
    }
    return tempFile;
}

static bool printContent(uint32_t startClus, uint32_t size)
{
    uint32_t charCnt = 0;

    if (startClus == 0 || size == 0)
        return 0;
    printk("\n");
    do
    {
        for (uint8_t i = 0; i < params.BPB_SecPerClus; i++)
        {
            if (SD_readSector(startSecOfClus(startClus) + i, SD_buff) == SD_READ_SUCCESS)
            {
                for (uint16_t c = 0; c < 512; c++)
                {
                    printk("%c", SD_buff[c]);

                    charCnt++;
                    if (charCnt == size)
                    {

                        return true;
                    }
                }
            }
            else
                return false;
            k_sleep(K_USEC(10));
        }

    } while ((startClus = fatNextClus(startClus)) < FAT_EOC);

    return true;
}

static inline bool isClosed(myFile *pFile)
{
    if ((startCluster(pFile) == 0) && (pFile->DIR_FileSize == 0))
        return true;
    return false;
}

uint8_t readByte(myFile *pFile)
{
    static uint32_t Cluster = 0;
    static uint8_t sectorIndex = 0;

    if (isClosed(pFile))
    {
        return 0;
    }

    if (pFile->entryIndex == 0)
    {
        Cluster = startCluster(pFile);
        sectorIndex = 0;
    }

    if ((pFile->entryIndex > 0) && (pFile->entryIndex % (params.BPB_SecPerClus * params.BPB_BytesPerSec) == 0))
    {
        sectorIndex = 0;
        Cluster = fatNextClus(Cluster);
        if (Cluster >= FAT_EOC)
        {
            pFile->entryIndex = 0;
            return 0;
        }
    }

    if (pFile->entryIndex % params.BPB_BytesPerSec == 0)
        SD_readSector(startSecOfClus(Cluster) + (sectorIndex++), SD_buff);

    return SD_buff[(pFile->entryIndex++) % params.BPB_BytesPerSec];
}

bool listDir(const char *path)
{
    myFile tempFile = pathExists(path);
    if (startCluster(&tempFile) == 0)
    {
        printk("Invalid Path!\n");
        return false;
    }
    if (!isDirectory(&tempFile))
    {
        printContent(startCluster(&tempFile), tempFile.DIR_FileSize);
        printk("\n");
        return true;
    }

    myFile folder = tempFile;

    do
    {
        tempFile = nextFile(&folder);
        if (isValidFile(&tempFile))
            dispFile(&tempFile, fileName, 0);
    } while (!isEndOfDir(&tempFile));
    printk("\n");
    return true;
}

void listDir_recursive(myFile *pFolder, uint8_t tab)
{
    myFile tempFile;

    do
    {
        tempFile = nextFile(pFolder);
        if (isValidFile(&tempFile))
        {
            if (isDirectory(&tempFile))
            {
                printk("\n");
                dispFile(&tempFile, fileName, tab);
                listDir_recursive(&tempFile, tab + 2);
                printk("\n");
            }
            else
                dispFile(&tempFile, fileName, tab);
        }
    } while (!isEndOfDir(&tempFile));
}

static void fileSetStartClus(myFile *pFile, uint32_t cluster)
{

    pFile->DIR_FstClusLO = (uint16_t)(cluster & 0x0000FFFF);
    pFile->DIR_FstClusHI = (uint16_t)((cluster & 0xFFFF0000) >> 16);
}

static void fileSetDate(myFile *pFile, uint16_t year, uint8_t month, uint8_t day)
{
    year -= 1980;
    pFile->DIR_WrtDate = day;
    pFile->DIR_WrtDate |= (uint16_t)month << 5;
    pFile->DIR_WrtDate |= year << 9;
}

static void fileSetTime(myFile *pFile, uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    pFile->DIR_WrtTime = (uint16_t)(seconds / 2);
    pFile->DIR_WrtTime |= (uint16_t)minutes << 5;
    pFile->DIR_WrtTime |= (uint16_t)hours << 11;
}

static freeEntInf_t getFreeEntry(myFile *Dir, uint8_t freeEntryCnt)
{
    freeEntInf_t frEntInf;
    frEntInf.Cluster = startCluster(Dir);

    do
    {
        for (frEntInf.sectorIndex = 0; frEntInf.sectorIndex < params.BPB_SecPerClus; frEntInf.sectorIndex++)
        {
            SD_readSector(startSecOfClus(frEntInf.Cluster) + frEntInf.sectorIndex, SD_buff);
            for (frEntInf.entryIndex = 0; frEntInf.entryIndex < 16; frEntInf.entryIndex++)
            {
                myFile temp = *((myFile *)(SD_buff + frEntInf.entryIndex * 32));
                if (isFreeEntry(&temp) || isEndOfDir(&temp))
                {
                    if (isEndOfDir(&temp))
                    {

                        if ((frEntInf.entryIndex + freeEntryCnt) > 15)
                        {
                            SD_readSector(startSecOfClus(frEntInf.Cluster) + frEntInf.sectorIndex, SD_buff);
                            for (uint8_t i = frEntInf.entryIndex; i < 16; i++)
                            {
                                myFile *pFile = (myFile *)(SD_buff + (i * 32));
                                pFile->DIR_Name[0] = 0xE5;
                            }
                            SD_writeSector(startSecOfClus(frEntInf.Cluster) + frEntInf.sectorIndex, SD_buff);

                            frEntInf.sectorIndex++;
                            frEntInf.entryIndex = 0;
                        }
                        return frEntInf;
                    }

                    if (freeEntryCnt == 1)
                    {

                        return frEntInf;
                    }
                    uint8_t i;
                    for (i = 0; i < freeEntryCnt; i++)
                    {
                        frEntInf.entryIndex += i;
                        if (frEntInf.entryIndex == 16)
                            break;

                        temp = *((myFile *)(SD_buff + frEntInf.entryIndex * 32));
                        if (!isFreeEntry(&temp))
                            break;
                    }
                    if (i != freeEntryCnt)
                        continue;

                    frEntInf.entryIndex -= (freeEntryCnt - 1);
                    return frEntInf;
                }
            }
        }

    } while ((frEntInf.Cluster = fatNextClus(frEntInf.Cluster)) < FAT_EOC);

    memset(&frEntInf, 0, sizeof(freeEntInf_t));
    return frEntInf;
}

static void get_datetime_numerical(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    const char *date_str = __DATE__; // e.g. "Apr 12 2023"
    const char *time_str = __TIME__; // e.g. "23:59:59"
    char *endptr;

    // Parse date string
    char monthStr[4] = {'\0'};
    memcpy(monthStr, date_str, 3);
    if (strcmp(monthStr, "Jan") == 0)
        *month = 1;

    else if (strcmp(monthStr, "Feb") == 0)
        *month = 2;

    else if (strcmp(monthStr, "Mar") == 0)
        *month = 3;

    else if (strcmp(monthStr, "Apr") == 0)
        *month = 4;

    else if (strcmp(monthStr, "May") == 0)
        *month = 5;

    else if (strcmp(monthStr, "Jun") == 0)
        *month = 6;

    else if (strcmp(monthStr, "Jul") == 0)
        *month = 7;

    else if (strcmp(monthStr, "Aug") == 0)
        *month = 8;

    else if (strcmp(monthStr, "Sep") == 0)
        *month = 9;

    else if (strcmp(monthStr, "Oct") == 0)
        *month = 10;

    else if (strcmp(monthStr, "Nov") == 0)
        *month = 11;

    else if (strcmp(monthStr, "Dec") == 0)
        *month = 12;

    *day = strtol(date_str + 4, &endptr, 10);
    *year = strtol(endptr + 1, NULL, 10);

    // Parse time string
    *hour = strtol(time_str, &endptr, 10);
    *minute = strtol(endptr + 1, &endptr, 10);
    *second = strtol(endptr + 1, NULL, 10);
}

static uint8_t fileNameLength(const char *filename)
{

    uint8_t i;
    for (i = 0; i < strlen(filename); i++)
    {
        if (filename[i] == '.')
            break;
    }
    return i;
}

static bool allLowerCase(const char *filename)
{
    for (uint8_t i = 0; i < fileNameLength(filename); i++)
    {
        if ((((uint8_t)filename[i]) < 91) && (((uint8_t)filename[i]) > 64))
        {
            return false;
        }
    }
    return true;
}

static bool mixedLetters(const char *filename)
{
    if (!allLowerCase(filename))
    {
        for (uint8_t i = 0; i < fileNameLength(filename); i++)
        {
            if (((uint8_t)filename[i] > 96))
            {
                return true;
            }
        }
    }
    return false;
}

static uint8_t create_sum(myFile *entry)
{
    uint8_t i;
    uint8_t Sum = 0;

    for (i = 0; i < 8; i++)
    { // Calculate sum of DIR_Name[] field
        Sum = (Sum >> 1) + (Sum << 7) + (uint8_t)entry->DIR_Name[i];
    }
    for (i = 0; i < 3; i++)
    { /// Calculate sum of DIR_ext[]] field
        Sum = (Sum >> 1) + (Sum << 7) + (uint8_t)entry->DIR_ext[i];
    }
    return Sum;
}

static bool updateFSInfo(uint32_t nxtFreeClus)
{
    if (SD_readSector(FSInfo_SEC, SD_buff) == SD_READ_SUCCESS)
    {
        FSInfo_t *p_fsinfo = (FSInfo_t *)SD_buff;
        p_fsinfo->FSI_Nxt_Free = nxtFreeClus;
        p_fsinfo->FSI_Free_Count--;

        if (SD_writeSector(FSInfo_SEC, SD_buff) == SD_WRITE_SUCCESS)
            return true;
        else
            return false;
    }
    return false;
}

static uint32_t getNxtFreeClus()
{
    if (SD_readSector(FSInfo_SEC, SD_buff) == SD_READ_SUCCESS)
    {

        FSInfo_t *p_fsinfo = (FSInfo_t *)SD_buff;
        uint32_t nxtFreeClus = p_fsinfo->FSI_Nxt_Free;

        while (fatNextClus(nxtFreeClus) != 0x00000000)
            nxtFreeClus++;

        if (updateFSInfo(nxtFreeClus))
        {
            return nxtFreeClus;
        }
        else
            return 0xFFFFFFFF;
    }
    else
        return 0xFFFFFFFF;
}

static myFile createFile(myFile *pathDir, const char *filename, bool isDir)
{
    myFile newFile = {0};

    uint32_t fileStartClus = getNxtFreeClus();
    fileSetStartClus(&newFile, fileStartClus);
    fatSetNextClus(fileStartClus, FAT_EOC);

    uint8_t tempIndx = 0;
    memset(newFile.DIR_Name, ' ', 8);
    memset(newFile.DIR_ext, ' ', 3);

    freeEntInf_t frEnt;

    if (mixedLetters(filename) || (fileNameLength(filename) > 8))
    {
        for (uint8_t i = 0; i < strlen(filename); i++)
        {
            if (filename[i] == '.')
            {
                tempIndx = i + 1;
                break;
            }
            if (i < 8)
            {
                if ((filename[i] > 96) && (filename[i] < 123))
                    newFile.DIR_Name[i] = filename[i] - 32;
                else
                    newFile.DIR_Name[i] = filename[i];
            }
            if (fileNameLength(filename) > 8)
            {
                newFile.DIR_Name[6] = '~';
                newFile.DIR_Name[7] = '1';
            }
        }
        for (uint8_t i = 0; i < 3; i++)
        {
            if (filename[tempIndx + i] == '\0')
                break;
            newFile.DIR_ext[i] = filename[tempIndx + i] - 32;
        }
        uint8_t lfnEntCnt = strlen(filename) / 13;
        newFile.fileEntInf.LFN_EntCnt = lfnEntCnt;

        if ((strlen(filename) % 13) != 0)
            lfnEntCnt += 1;

        uint8_t nameIndex = 0;
        uint8_t temp = lfnEntCnt;

        frEnt = getFreeEntry(pathDir, lfnEntCnt + 1);
        SD_readSector(startSecOfClus(frEnt.Cluster) + frEnt.sectorIndex,
                      SD_buff);

        while (lfnEntCnt)
        {
            LFN_entry_t *entry = (LFN_entry_t *)(SD_buff + (frEnt.entryIndex + lfnEntCnt - 1) * 32);
            entry->LDIR_Attr = ATTR_LONG_FILE_NAME;
            entry->LDIR_FstClusLO = 0;
            entry->LDIR_Type = 0;
            entry->LDIR_Ord = temp - lfnEntCnt + 1;
            entry->LDIR_Chksum = create_sum(&newFile);
            memset(entry->LDIR_Name1, 0, 10);
            memset(entry->LDIR_Name2, 0, 12);
            memset(entry->LDIR_Name3, 0, 4);

            if (lfnEntCnt == 1)
                entry->LDIR_Ord |= 0x40;

            for (uint8_t i = 0; i < 10; i += 2)
            {
                if (filename[nameIndex] != 0)
                    entry->LDIR_Name1[i] = filename[nameIndex++];
            }
            for (uint8_t i = 0; i < 12; i += 2)
            {
                if (filename[nameIndex] != 0)
                    entry->LDIR_Name2[i] = filename[nameIndex++];
            }
            for (uint8_t i = 0; i < 4; i += 2)
            {
                if (filename[nameIndex] != 0)
                    entry->LDIR_Name3[i] = filename[nameIndex++];
            }

            lfnEntCnt--;
        }
        frEnt.entryIndex += temp;
    }
    else

    {
        frEnt = getFreeEntry(pathDir, 1);
        SD_readSector(startSecOfClus(frEnt.Cluster) + frEnt.sectorIndex,
                      SD_buff);

        for (uint8_t i = 0; i < 9; i++)
        {
            if (filename[i] == '.')
            {
                tempIndx = i + 1;
                break;
            }

            if ((filename[i] > 96) && (filename[i] < 123))
                newFile.DIR_Name[i] = filename[i] - 32;
            else
                newFile.DIR_Name[i] = filename[i];
        }
        for (uint8_t i = 0; i < 3; i++)
        {
            if (filename[i] == '\0')
                break;
            newFile.DIR_ext[i] = filename[tempIndx + i] - 32;
        }
        newFile.fileEntInf.LFN_EntCnt = 0;
    }
    if (isDir)
        newFile.DIR_attr = ATTR_DIRECTORY;
    else
        newFile.DIR_attr = 0;

    if (allLowerCase(filename))
        newFile.DIR_NTRes = 0x18;
    else
        newFile.DIR_NTRes = 0x10;

    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    get_datetime_numerical(&year, &month, &day, &hour, &minute, &second);

    fileSetDate(&newFile, year, month, day);
    fileSetTime(&newFile, hour, minute, second);

    newFile.fileEntInf.Cluster = frEnt.Cluster;
    newFile.fileEntInf.sectorIndex = frEnt.sectorIndex;
    newFile.fileEntInf.entryIndex = frEnt.entryIndex;

    myFile *pFile = (myFile *)(SD_buff + frEnt.entryIndex * 32);
    memcpy(pFile, &newFile, 32);

    if (SD_writeSector(startSecOfClus(frEnt.Cluster) + frEnt.sectorIndex,
                       SD_buff) == SD_WRITE_SUCCESS)
    {
        printk("File Created!\n");
        return newFile;
    }
    else
    {
        memset(&newFile, 0, sizeof(myFile));
        return newFile;
    }
}

myFile fileOpen(const char *path, const char *filename)
{

    myFile pathDir = pathExists(path);

    if (startCluster(&pathDir) == 0)
    {
        printk("Invalid path!\n");
        return pathDir;
    }
    else if (filename == NULL)
    {
        return pathDir;
    }
    else
    {

        myFile tempFile = fileExists(filename, &pathDir);

        if (startCluster(&tempFile) != 0)
        {
            printk("File exists!\n");
            return tempFile;
        }

        return createFile(&pathDir, filename, false);
    }
}

myFile createDirectory(const char *path, const char *dirName)
{
    myFile parentDir = pathExists(path);

    if (startCluster(&parentDir) == 0)
    {
        printk("Invalid path!\n");
        return parentDir;
    }

    myFile thisDir = fileExists(dirName, &parentDir);

    if (startCluster(&thisDir) != 0)
    {
        // //printk("Folder exists!");
        return thisDir;
    }

    thisDir = createFile(&parentDir, dirName, true);

    uint32_t dirStartClus = startCluster(&thisDir);

    memset(thisDir.DIR_Name, ' ', 8);
    memset(thisDir.DIR_ext, ' ', 3);
    memset(parentDir.DIR_Name, ' ', 8);
    memset(parentDir.DIR_ext, ' ', 3);

    thisDir.DIR_Name[0] = '.';

    parentDir.DIR_Name[0] = '.';
    parentDir.DIR_Name[1] = '.';

    memset(SD_buff, 0, 512);

    for (uint8_t sectorIndex = 0; sectorIndex < params.BPB_SecPerClus;
         sectorIndex++)
        SD_writeSector(startSecOfClus(dirStartClus) + sectorIndex, SD_buff);

    memcpy(SD_buff, &thisDir, 32);
    memcpy(SD_buff + 32, &parentDir, 32);

    SD_writeSector(startSecOfClus(dirStartClus), SD_buff);

    return thisDir;
}

bool fileWrite(myFile *pFile, const char *data)
{
    uint32_t startClus = startCluster(pFile);
    uint16_t byteIndex = pFile->DIR_FileSize % 512;
    uint32_t sectorIndex = pFile->DIR_FileSize / 512;
    uint32_t clusterCnt = (sectorIndex / params.BPB_SecPerClus) + 1;
    uint32_t byteCnt = 0;
    bool moreData = true;

    sectorIndex = sectorIndex % params.BPB_SecPerClus;
    uint32_t tempClus;

    if (clusterCnt > 1)
    {
        while (clusterCnt)
        {
            tempClus = startClus;
            startClus = fatNextClus(startClus);
            clusterCnt--;
        }
        uint32_t nextClus = getNxtFreeClus();
        fatSetNextClus(tempClus, nextClus);
        fatSetNextClus(nextClus, FAT_EOC);
    }

    if (byteIndex != 0)
        SD_readSector(startSecOfClus(startClus) + sectorIndex, SD_buff);
    for (uint32_t sector = sectorIndex; sector < params.BPB_SecPerClus; sector++)
    {
        for (uint32_t i = byteIndex; i < 512; i++)
        {
            SD_buff[i] = data[byteCnt++];
            if (byteCnt == strlen(data))
            {
                moreData = false;
                break;
            }
        }
        byteIndex = 0;

        if (SD_writeSector(startSecOfClus(startClus) + sector, SD_buff) == SD_WRITE_ERROR)
            return false;

        if (!moreData)
        {
            pFile->DIR_FileSize += strlen(data);

            if (SD_readSector(startSecOfClus(pFile->fileEntInf.Cluster) + pFile->fileEntInf.sectorIndex, SD_buff) == SD_READ_SUCCESS)
            {
                myFile *p_temp = (myFile *)(SD_buff + pFile->fileEntInf.entryIndex * 32);
                memcpy(p_temp, pFile, 32);
                if (SD_writeSector(startSecOfClus(pFile->fileEntInf.Cluster) + pFile->fileEntInf.sectorIndex, SD_buff) == SD_WRITE_SUCCESS)
                    return true;
            }
            return false;
        }
    }
    return true;
}

bool fileDelete(const char *path, const char *filename)
{
    myFile pathDir;

    myFile tempFile = pathExists(path);
    if (startCluster(&tempFile) == 0)
    {
        // printk("Invalid path!\n");
        return false;
    }

    pathDir = tempFile;

    tempFile = fileExists(filename, &pathDir);

    if (startCluster(&tempFile) == 0)
    {
        printk("File doesnt exists!\n");
        return false;
    }

    uint8_t lfnEntCnt = 0;
    if (mixedLetters(filename) || (fileNameLength(filename) > 8))
    {
        lfnEntCnt = strlen(filename) / 13;
        if ((strlen(filename) % 13) != 0)
            lfnEntCnt += 1;
    }

    if (SD_readSector(startSecOfClus(tempFile.fileEntInf.Cluster) + tempFile.fileEntInf.sectorIndex, SD_buff) == SD_READ_SUCCESS)
    {
        for (uint8_t i = 0; i < (lfnEntCnt + 1); i++)
        {
            myFile *p_temp = (myFile *)(SD_buff + (tempFile.fileEntInf.entryIndex - i) * 32);
            p_temp->DIR_Name[0] = 0xE5;
        }

        if (SD_writeSector(startSecOfClus(tempFile.fileEntInf.Cluster) + tempFile.fileEntInf.sectorIndex, SD_buff) == SD_WRITE_SUCCESS)
        {
            uint32_t fileClus = startCluster(&tempFile);
            uint32_t tempClus;
            while (fileClus < FAT_EOC)
            {
                tempClus = fileClus;
                fileClus = fatNextClus(fileClus);
                fatSetNextClus(tempClus, 0x00000000);
            }
            // updateFSInfo(startCluster(tempFile));
            return true;
        }
        return false;
    }
    return false;
}

bool SdFat_init()
{
    printk("Using Custom SDHC driver by Surya Poudel\n");
    /* if (sd_init(sd_dev, &sdcard) != 0)
         return false;
 */
    if (SD_drv_init() == SD_INIT_ERROR)
        return false;

    if (getBootSecParams())
    {

        FatStartSector = BOOT_SEC_START + params.BPB_RsvdSecCnt; // 0X2020

        FatSectorsCnt = params.BPB_FATSz32 * params.BPB_NumFATs;

        RootDirStartSector = FatStartSector + FatSectorsCnt;

        RootDirSectors = (32 * params.BPB_RootEntCnt + params.BPB_BytesPerSec - 1) / params.BPB_BytesPerSec; // 0 for FAT32

        DataStartSector = RootDirStartSector + RootDirSectors; // 0X96AE

        DataSectorsCnt = params.BPB_TotSec32 - DataStartSector;

        float size = (params.BPB_TotSec32 * 512.0) / (1024.0 * 1024.0 * 1024.0);
        uint16_t sizeInt = size;
        float tmpFrac = size - sizeInt;
        uint16_t tmpInt = tmpFrac * 100;

        printk("Card Size=%d.%d GB\n", sizeInt, tmpInt);

        printk("FAT type is: ");
        switch (getFatType())
        {
        case FAT12:
            printk("FAT12\n");
            break;
        case FAT16:
            printk("FAT16\n");
            break;

        case FAT32:
            printk("FAT32\n");
            break;

        default:
            break;
        }
        printk("Volume Label: %s\n", params.BS_VolLab);
        return true;
    }
    return false;
}
