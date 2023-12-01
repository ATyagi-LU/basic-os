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
#define DATA_OFFSET(bootSector) (DIR_OFFSET(bootSector)+ DIR_SIZE(bootSector))
#define FLAGS_SET(entry, mask) (((entry) & (mask)) == (mask))
#define FLAGS_NOT_SET(entry, mask) (((entry) & (mask)) == 0)
#define ARCHIVE 0b00100000
#define DIRECTORY 0b00010000
#define VOL_NAME 0b00001000
#define SYSTEM 0b00000100
#define HIDDEN 0b00000010
#define READ_ONLY 0b00000001
#define FSEEK_SET 0
#define FSEEK_CUR 1
#define FSEEK_END 2

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
    uint8_t LDIR_Ord; // Order/ position in sequence/ set
    uint8_t LDIR_Name1[ 10 ]; // First 5 UNICODE characters
    uint8_t LDIR_Attr; // = ATTR_LONG_NAME (xx001111)
    uint8_t LDIR_Type; // Should = 0
    uint8_t LDIR_Chksum; // Checksum of short name
    uint8_t LDIR_Name2[ 12 ]; // Middle 6 UNICODE characters
    uint16_t LDIR_FstClusLO; // MUST be zero
    uint8_t LDIR_Name3[ 4 ]; // Last 2 UNICODE characters
} LongDirEntry;

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



typedef struct Volume
{
    uint16_t *FAT;
    BootSector *bootSector;
    char * path;

} Volume;

typedef struct File
{
    off_t offset;
    int size;
    ListHead *clusterList;
    Volume * fileVolume;

} File;

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

int clusterCounter(ListHead *list)
{
    int size = 0;
    Node *currentNode = list->nextNode;
    while (currentNode->nextNode != NULL)
    {
        size++;
        currentNode = currentNode->nextNode;
    }
    return size;
}

extern File *openFile(Volume *vol, ShortDirEntry *ent)
{

    File *file = (File *)malloc(sizeof(File));
    if (FLAGS_NOT_SET(ent->DIR_Attr, VOL_NAME | DIRECTORY))
    {
        file->clusterList = clusterCompiler(vol->FAT, (ent->DIR_FstClusHI) << 16 | (ent->DIR_FstClusLO));
        file->offset = 0;
        file->size = ent->DIR_FileSize;
        file->fileVolume = vol;
        return file;
    }
    else
    {
        return NULL;
    }
};

extern off_t seekFile(File *file, off_t offset, int whence)
{

    switch (whence)
    {
    case FSEEK_SET:
        if (offset < 0)
        {
            printf("Offset size not valid.");
            return -1;
        }
        file->offset = offset;
        return file->offset;
    case FSEEK_CUR:
        if (file->offset + offset < 0)
        {
            printf("Offset size not valid.");
            return -1;
        }
        file->offset += offset;
        return file->offset;
    case FSEEK_END:
        if (file->size + offset < 0)
        {
            printf("Offset size not valid.");
            return -1;
        }
        file->offset = file->size;
        return file->offset;

    default:
        return -1;
    }
};

extern void closeFile(File *file)
{
    freeList(file->clusterList);
    free(file);
};

extern ssize_t readFile(File *file, void *buffer, size_t length)
{

    int clusterIndex = file->offset / CLUSTER_SIZE(file->fileVolume->bootSector);
    int startRead = file->offset % CLUSTER_SIZE(file->fileVolume->bootSector);
    ssize_t totalBytesRead = 0;
    if (length > (file->size) - (file->offset))
    {
        length = file->size - file->offset;
    }
    Node *currentNode = file->clusterList->nextNode;
    for (int i = 0; i < clusterIndex && currentNode != NULL; i++)
    {
        currentNode = currentNode->nextNode;
    }
    for (; currentNode != NULL && length > 0; currentNode = currentNode->nextNode){
        off_t offset = DATA_OFFSET(file->fileVolume->bootSector) + ((currentNode->data - 2)*CLUSTER_SIZE(file->fileVolume->bootSector)) + startRead;
        int cap = CLUSTER_SIZE(file->fileVolume->bootSector) - startRead;
        int bytesRead;
        if (cap<length){
            bytesRead = reader(file->fileVolume->path,buffer,cap,offset);
        }
        else{
            bytesRead = reader(file->fileVolume->path,buffer,length,offset);
        }
        
        if (bytesRead == -1)
        {
            printf("Invalid file.");
            return -1;
        }

        length -= bytesRead;
        buffer += bytesRead;
        totalBytesRead += bytesRead;
        seekFile(file,bytesRead,FSEEK_CUR);
    }

    return totalBytesRead;

};

Volume * loadVol (char * path){
    Volume * vol = (Volume *) malloc(sizeof(Volume));
    vol->path = path;

    BootSector *bootSector = (BootSector *)malloc(sizeof(BootSector));

    if ((reader(path, bootSector, sizeof(BootSector), 0)) == -1)
    {
        printf("Invalid file.");
        return NULL;
    }

    uint16_t *FAT = (uint16_t *)malloc(FAT_SIZE(bootSector));

    if ((reader(path, FAT, FAT_SIZE(bootSector), FAT_OFFSET(bootSector))) == -1)
    {
        printf("Invalid file.");
        return NULL;
    }

    vol->FAT = FAT;
    vol->bootSector = bootSector;

    return vol;
}

void UTF16ToASCII(uint8_t* utf16, char* ASCIIOut, int length){
    for(int i = 0;i < length;i++){
        ASCIIOut[i] = utf16[i*2];
    }
}

int main()
{

    Volume * volume = loadVol("fat16.img");

    ShortDirEntry **rootDir = (ShortDirEntry **)malloc(DIR_SIZE(volume->bootSector));

    for (int i = 0; i < volume->bootSector->BPB_RootEntCnt; i++)
    {
        ShortDirEntry *entry = (ShortDirEntry *)malloc(sizeof(ShortDirEntry));
        reader(volume->path, entry, sizeof(ShortDirEntry), (DIR_OFFSET(volume->bootSector) + (i * sizeof(ShortDirEntry))));
        rootDir[i] = entry;
    }
    printf("%-30s%s%20s%28s%21s%20s  %s\n", "Filename", "File Attributes", "Date Modified", "Time Modified", "File Size", "Starting Cluster", "Long Filename");

    for (int i = 0; i < volume->bootSector->BPB_RootEntCnt; i++)
    {

        int longNameLength = ((((LongDirEntry*) rootDir[i])->LDIR_Ord & 0b1111)*13);
        char longName[longNameLength+1];
        longName[longNameLength] = '\0';
        for(;i<volume->bootSector->BPB_RootEntCnt&&(FLAGS_SET(rootDir[i]->DIR_Attr, VOL_NAME | SYSTEM | HIDDEN | READ_ONLY) && FLAGS_NOT_SET(rootDir[i]->DIR_Attr, ARCHIVE | DIRECTORY));i++){
            LongDirEntry * longEnt = (LongDirEntry *) rootDir[i];
            longNameLength -= 13;
            UTF16ToASCII(longEnt->LDIR_Name1,longName+longNameLength,5);
            UTF16ToASCII(longEnt->LDIR_Name2,longName+longNameLength+5,6);
            UTF16ToASCII(longEnt->LDIR_Name3,longName+longNameLength+11,2);
            
        }

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
            printf("%20d/%d/%d", ((rootDir[i]->DIR_WrtDate) >> 9) + 1980, ((rootDir[i]->DIR_WrtDate) >> 5) & 0b1111, (rootDir[i]->DIR_WrtDate) & 0b11111);
            printf("%20d:%d:%d", ((rootDir[i]->DIR_WrtTime) >> 11), ((rootDir[i]->DIR_WrtTime) >> 5) & 0b111111, ((rootDir[i]->DIR_WrtTime) & 0b11111) * 2);
            printf("%20d bytes", rootDir[i]->DIR_FileSize);
            printf("%20d  ", ((rootDir[i]->DIR_FstClusHI) << 16 | (rootDir[i]->DIR_FstClusLO)));
            printf("%s\n",longName);
        }
    }


    File *file = openFile(volume, rootDir[18]);
    char buffer[file->size];
    readFile(file,buffer,file->size);
    printf("%s\n",buffer);
    closeFile(file);
    ListHead *cluster = clusterCompiler(volume->FAT, 4);
    freeList(cluster);

    printf(
        "%-6d Bytes Per Sector\n%-6d Sectors per Cluster\n%-6d Reserved Sector Count\n%-6d Number of copies of FAT\n%-6d Size of root DIR\n%-6d Sectors\n%-6d Sectors in FAT\n%-6d Sectors if BPB_TotSec16==0\n%.11sNon zero terminated string\n",
        volume->bootSector->BPB_BytsPerSec,
        volume->bootSector->BPB_SecPerClus,
        volume->bootSector->BPB_RsvdSecCnt,
        volume->bootSector->BPB_NumFATs,
        volume->bootSector->BPB_RootEntCnt,
        volume->bootSector->BPB_TotSec16,
        volume->bootSector->BPB_FATSz16,
        volume->bootSector->BPB_TotSec32,
        volume->bootSector->BS_VolLab);

    for (int i = 0; i < volume->bootSector->BPB_RootEntCnt; i++)
    {
        free(rootDir[i]);
    }
    free(rootDir);
    free(volume->bootSector);
    free(volume->FAT);
    free(volume);
}
