#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define FAT_SIZE(bootSector) (((bootSector)->BPB_BytsPerSec)*((bootSector)->BPB_FATSz16)) 
#define FAT_OFFSET(bootSector) ((((bootSector)->BPB_RsvdSecCnt)*((bootSector)->BPB_BytsPerSec)))

typedef struct{
    uint16_t data;
    Node * nextNode;
} Node;

typedef struct{
    Node * nextNode;
    Node * lastNode;
} ListHead;


typedef struct __attribute__((__packed__))
{
    uint8_t BS_jmpBoot[3];
    // x86 jump instr. to boot code
    uint8_t BS_OEMName[8];
    // What created the filesystem
    uint16_t BPB_BytsPerSec;
    // Bytes per Sector
    uint8_t BPB_SecPerClus;
    // Sectors per ClusterHeadNode
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

ListHead * createList(){
    ListHead * list = (ListHead *) malloc(sizeof(ListHead));
    list->nextNode = NULL;
    list->lastNode = NULL;
    return list;
}

void addNode(ListHead * list, uint16_t data){
    Node * addedNode = (Node *) malloc(sizeof(Node));
    addedNode->data = data;
    addedNode->nextNode = NULL;
    if (list->nextNode == NULL)
        list->nextNode = addedNode;
    
    else
        list->lastNode->nextNode = addedNode;
    
    list->lastNode = addedNode;
}

void freeList(ListHead * list){
    Node * node = list->nextNode;
    while(node !=NULL){
        Node * nextNode = node->nextNode;
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

int main()
{   
    

    BootSector * bootSector = (BootSector *) malloc(sizeof(BootSector));
    
    if ((reader("fat16.img", bootSector, sizeof(BootSector), 0)) == -1){
        printf("Invalid file.");
        return -1;
    }
    

    uint16_t * FAT = (uint16_t *) malloc(FAT_SIZE(bootSector));

    if ((reader("fat16.img", FAT,FAT_SIZE(bootSector), FAT_OFFSET(bootSector))) == -1){
        printf("Invalid file.");
        return -1;
    }

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
