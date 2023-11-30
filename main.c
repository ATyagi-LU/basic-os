#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define FAT_SIZE(bootSector) (((bootSector)->BPB_BytsPerSec) * ((bootSector)->BPB_FATSz16))
#define FAT_OFFSET(bootSector) ((((bootSector)->BPB_RsvdSecCnt) * ((bootSector)->BPB_BytsPerSec)))
#define DIR_OFFSET(bootSector) (((bootSector)->BPB_RsvdSecCnt + ((bootSector)->BPB_NumFATs * (bootSector)->BPB_FATSz16)) * (bootSector)->BPB_BytsPerSec)
#define DIR_SIZE(bootSector) ((bootSector)->BPB_RootEntCnt * sizeof(ShortDirEntry))
#define CLUSTER_SIZE(bootSector) ((bootSector)->BPB_SecPerClus * (bootSector)->BPB_BytsPerSec)
#define DATA_OFFSET(bootSector) (FAT_OFFSET(bootSector)+FAT_SIZE(bootSector)+DIR_SIZE(bootSector))
#define FLAGS_SET(entry, mask) (((entry) & (mask)) == (mask))
#define FLAGS_NOT_SET(entry, mask) (((entry) & (mask)) == 0)
#define ARCHIVE 0b00100000
#define DIRECTORY 0b00010000
#define VOL_NAME 0b00001000
#define SYSTEM 0b00000100
#define HIDDEN 0b00000010
#define READ_ONLY 0b00000001

typedef struct Volume
{
    uint16_t * FAT;
    BootSector * bootSector;
    int fd;
    int startCluster;

} Volume;

typedef struct File
{   
    off_t offset;
    int size;
    uint8_t * data;

} File;

typedef struct Node
{
    uint16_t data;
    struct Node *nextNode;
} Node;

typedef struct
{
    Node *nextNode;
    Node *lastNode;
} ListHead;

typedef struct __attribute__((__packed__))
{
    uint8_t DIR_Name[11];     // Non zero terminated string
    uint8_t DIR_Attr;         // File attributes
    uint8_t DIR_NTRes;        // Used by Windows NT, ignore
    uint8_t DIR_CrtTimeTenth; // Tenths of sec. 0...199
    uint16_t DIR_CrtTime;     // Creation Time in 2s intervals
    uint16_t DIR_CrtDate;     // Date file created
    uint16_t DIR_LstAccDate;  // Date of last read or write
    uint16_t DIR_FstClusHI;   // Top 16 bits file's 1st cluster
    uint16_t DIR_WrtTime;     // Time of last write
    uint16_t DIR_WrtDate;     // Date of last write
    uint16_t DIR_FstClusLO;   // Lower 16 bits file's 1st cluster
    uint32_t DIR_FileSize;    // File size in bytes

} ShortDirEntry;

typedef struct __attribute__((__packed__))
{
    uint8_t BS_jmpBoot[3];    // x86 jump instr. to boot code
    uint8_t BS_OEMName[8];    // What created the filesystem
    uint16_t BPB_BytsPerSec;  // Bytes per Sector
    uint8_t BPB_SecPerClus;   // Sectors per ClusterHeadNode
    uint16_t BPB_RsvdSecCnt;  // Reserved Sector Count
    uint8_t BPB_NumFATs;      // Number of copies of FAT
    uint16_t BPB_RootEntCnt;  // FAT12/FAT16: size of root DIR
    uint16_t BPB_TotSec16;    // Sectors, may be 0, see below
    uint8_t BPB_Media;        // Media type, e.g. fixed
    uint16_t BPB_FATSz16;     // Sectors in FAT (FAT12 or FAT16)
    uint16_t BPB_SecPerTrk;   // Sectors per Track
    uint16_t BPB_NumHeads;    // Number of heads in disk
    uint32_t BPB_HiddSec;     // Hidden Sector count
    uint32_t BPB_TotSec32;    // Sectors if BPB_TotSec16 == 0
    uint8_t BS_DrvNum;        // 0 = floppy, 0x80 = hard disk
    uint8_t BS_Reserved1;     //
    uint8_t BS_BootSig;       // Should = 0x29
    uint32_t BS_VolID;        // 'Unique' ID for volume
    uint8_t BS_VolLab[11];    // Non zero terminated string
    uint8_t BS_FilSysType[8]; // e.g. 'FAT16' (Not 0 terms.)
} BootSector;

void convertToNameString(uint8_t *name, char *output)
{
    int i, j = 0;
    for (i = 0; i < 8; i++)
    {
        if (name[i] != ' ')
        {
            output[j] = name[i];
            j++;
        }
    }
    if (!((name[8] == ' ') && (name[9] == ' ') && (name[10] == ' ')))
    {
        output[j++] = '.';
        for (i = 8; i < 11; i++)
        {
            if (name[i] != ' ')
            {
                output[j] = name[i];
                j++;
            }
        }
    }
    output[j] = '\0';
}

ListHead *createList()
{
    ListHead *list = (ListHead *)malloc(sizeof(ListHead));
    list->nextNode = NULL;
    list->lastNode = NULL;
    return list;
}

void addNode(ListHead *list, uint16_t data)
{
    Node *addedNode = (Node *)malloc(sizeof(Node));
    addedNode->data = data;
    addedNode->nextNode = NULL;
    if (list->nextNode == NULL)
        list->nextNode = addedNode;

    else
        list->lastNode->nextNode = addedNode;

    list->lastNode = addedNode;
}

void freeList(ListHead *list)
{
    Node *node = list->nextNode;
    while (node != NULL)
    {
        Node *nextNode = node->nextNode;
        free(node);
        node = nextNode;
    }
    free(list);
}

int reader(char *path, void *out, size_t bytes, off_t offset)
{
    int fd = open(path, 0);
    if (fd == -1)
    {
        printf("Invalid Filename or Path.");
        return -1;
    }
    lseek(fd, offset, SEEK_SET);
    int size = read(fd, out, bytes);
    if (size == -1)
    {
        printf("Invalid file.");
        close(fd);
        return -1;
    }
    close(fd);
    return size;
}

ListHead *clusterCompiler(uint16_t *FAT, uint32_t index)
{
    ListHead *clusterList = createList();
    uint32_t clusterCursor = index;
    while ((clusterCursor < 0x0000FFF8) && (clusterCursor > 1))
    {
        addNode(clusterList, clusterCursor);
        clusterCursor = FAT[clusterCursor];
    }

    return clusterList;
}

int clusterCounter(ListHead * list){
    int size = 0;
    Node * currentNode = list->nextNode;
    while (currentNode->nextNode != NULL){
        size++;
        currentNode = currentNode->nextNode;
    }
    return size;
}

extern File *openFile( Volume * vol, ShortDirEntry * ent){
    File * file = (File *) malloc(sizeof(File));
    file->size = ent->DIR_FileSize;
    file->offset = 0;
    ListHead * clusterList = clusterCompiler(vol->FAT,(ent->DIR_FstClusHI)<<16|(ent->DIR_FstClusLO));
    int clusterSize =clusterCounter(clusterList);
    file->data = (uint8_t*) malloc(sizeof(uint8_t)*clusterSize);
    off_t dataOffset = DATA_OFFSET((vol->bootSector));
    Node * currentNode = clusterList->nextNode;
    int count = 0;
    while (currentNode->nextNode != NULL){
        reader("fat16.img",file->data[count],sizeof(uint8_t),(dataOffset + currentNode->data - 2));
        count++;
    }

};

int main()
{

    BootSector *bootSector = (BootSector *)malloc(sizeof(BootSector));

    if ((reader("fat16.img", bootSector, sizeof(BootSector), 0)) == -1)
    {
        printf("Invalid file.");
        return -1;
    }

    uint16_t *FAT = (uint16_t *)malloc(FAT_SIZE(bootSector));

    if ((reader("fat16.img", FAT, FAT_SIZE(bootSector), FAT_OFFSET(bootSector))) == -1)
    {
        printf("Invalid file.");
        return -1;
    }

    ShortDirEntry **rootDir = (ShortDirEntry**) malloc(DIR_SIZE(bootSector));

    for (int i = 0; i < bootSector->BPB_RootEntCnt; i++)
    {
        ShortDirEntry *entry = (ShortDirEntry *)malloc(sizeof(ShortDirEntry));
        reader("fat16.img", entry, sizeof(ShortDirEntry), (DIR_OFFSET(bootSector) + (i * sizeof(ShortDirEntry))));
        rootDir[i] = entry;
    }
    printf("%-30s%s%20s%28s%21s%20s\n","Filename", "File Attributes", "Date Modified", "Time Modified", "File Size", "Starting Cluster");
    
    for (int i = 0; i < bootSector->BPB_RootEntCnt; i++)
    {   

        if (!(FLAGS_SET(rootDir[i]->DIR_Attr, VOL_NAME | SYSTEM | HIDDEN | READ_ONLY) && FLAGS_NOT_SET(rootDir[i]->DIR_Attr, ARCHIVE | DIRECTORY)))
        {
            if (rootDir[i]->DIR_Name[0] == 0x0)
                break;

            if (rootDir[i]->DIR_Name[0] != 0xE5)
            {
                char name[13];
                convertToNameString(rootDir[i]->DIR_Name, name);
                printf("%-30s", name);
                printf("%c%c%c%c%c%c",
                       (rootDir[i]->DIR_Attr & 0x20) ? 'A' : '-',
                       (rootDir[i]->DIR_Attr & 0x10) ? 'D' : '-',
                       (rootDir[i]->DIR_Attr & 0x08) ? 'V' : '-',
                       (rootDir[i]->DIR_Attr & 0x04) ? 'S' : '-',
                       (rootDir[i]->DIR_Attr & 0x02) ? 'H' : '-',
                       (rootDir[i]->DIR_Attr & 0x01) ? 'R' : '-');
                printf("%20d/%d/%d",((rootDir[i]->DIR_WrtDate)>>9)+1980,((rootDir[i]->DIR_WrtDate)>>5)&0b1111,(rootDir[i]->DIR_WrtDate)&0b11111);
                printf("%20d:%d:%d",((rootDir[i]->DIR_WrtTime)>>11),((rootDir[i]->DIR_WrtTime)>>5)&0b111111,((rootDir[i]->DIR_WrtTime)&0b11111)*2);
                printf("%20d bytes", rootDir[i]->DIR_FileSize);
                printf("%20d\n", ((rootDir[i]->DIR_FstClusHI)<<16|(rootDir[i]->DIR_FstClusLO)));
            }
        }
    }  

    ListHead *cluster = clusterCompiler(FAT, 4);
    freeList(cluster);

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

    for (int i = 0; i < bootSector->BPB_RootEntCnt; i++)
    {
        free(rootDir[i]);
    }
    free(rootDir);
    free(bootSector);
    free(FAT);
}
