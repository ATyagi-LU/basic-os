#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct __attribute__((__packed__))
{
    uint8_t BS_jmpBoot[3];
    // x86 jump instr. to boot code
    uint8_t BS_OEMName[8];
    // What created the filesystem
    uint16_t BPB_BytsPerSec;
    // Bytes per Sector
    uint8_t BPB_SecPerClus;
    // Sectors per Cluster
    uint16_t BPB_RsvdSecCnt;
    // Reserved Sector Count
    uint8_t BPB_NumFATs;
    // Number of copies of FAT
    uint16_t BPB_RootEntCnt;
    // FAT12/FAT16: size of root DIR
    uint16_t BPB_TotSec16;
    // Sectors, may be 0, see below
    uint8_t BPB_Media;
    // Media type, e.g. fixed
    uint16_t BPB_FATSz16;
    // Sectors in FAT (FAT12 or FAT16)
    uint16_t BPB_SecPerTrk;
    // Sectors per Track
    uint16_t BPB_NumHeads;
    // Number of heads in disk
    uint32_t BPB_HiddSec;
    // Hidden Sector count
    uint32_t
        BPB_TotSec32;
    // Sectors if BPB_TotSec16 == 0
    uint8_t BS_DrvNum;
    // 0 = floppy, 0x80 = hard disk
    uint8_t BS_Reserved1;
    //
    uint8_t BS_BootSig;
    // Should = 0x29
    uint32_t BS_VolID;
    // 'Unique' ID for volume
    uint8_t BS_VolLab[11];
    // Non zero terminated string
    uint8_t BS_FilSysType[8]; // e.g. 'FAT16' (Not 0 terms.)
} BootSector;

int reader(char *path, void *bootSec, off_t offset)
{
    int fd = open(path, 0);
    if (fd == -1)
    {
        printf("Invalid Filename or Path.");
        return -1;
    }
    lseek(fd, offset, SEEK_CUR);
    int size = read(fd, bootSec, 29);
    if (size == -1)
    {
        printf("Invalid file.");
        close(fd);
        return -1;
    }
    close(fd);
    return size;
}

int main()
{   

    BootSector * bootSector = (BootSector *) malloc(sizeof(BootSector));
    int size = reader("fat16.img", bootSector, 0);
    printf(
        "%-6d Bytes Per Sector\n%-6d Sectors per Cluster\n%-6d Reserved Sector Count\n%-6d Number of copies of FAT\n%-6d Size of root DIR\n%-6d Sectors\n%-6d Sectors in FAT\n%-6d Sectors if BPB_TotSec16==0\n%.11sNon zero terminated string\n",
        bootSector->BPB_BytsPerSec, 
        bootSector->BPB_SecPerClus,
        bootSector->BPB_RsvdSecCnt,
        bootSector->BPB_NumFATs,
        bootSector->BPB_RootEntCnt,
        bootSector->BPB_TotSec16,
        bootSector->BPB_FATSz16,
        bootSector->BPB_TotSec32,
        bootSector->BS_VolLab);
}
