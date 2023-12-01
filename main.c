#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define FAT_SIZE(bootSector) (((bootSector)->BPB_BytsPerSec) * ((bootSector)->BPB_FATSz16)) //FAT SIZE = bytes per sector * number of sectors in fat
#define FAT_OFFSET(bootSector) ((((bootSector)->BPB_RsvdSecCnt) * ((bootSector)->BPB_BytsPerSec))) //Where the fat begins (After the bootsector aka reserved sectors)
#define DIR_OFFSET(bootSector) (FAT_OFFSET(bootSector)+(FAT_SIZE(bootSector))*bootSector->BPB_NumFATs) //dir offset is the BPB_RsvdSecCnt + BPB_NumFATs * BPB_FATSz16.
#define DIR_SIZE(bootSector) ((bootSector)->BPB_RootEntCnt * sizeof(ShortDirEntry)) //size of directory = num of entries * size of 1 entry
#define CLUSTER_SIZE(bootSector) ((bootSector)->BPB_SecPerClus * (bootSector)->BPB_BytsPerSec) //size of one cluster = bytes per sector * number of sectors in 1 cluster
#define DATA_OFFSET(bootSector) (DIR_OFFSET(bootSector) + DIR_SIZE(bootSector)) //DATA OFFSET = dir offset + dir size
#define FLAGS_SET(entry, mask) (((entry) & (mask)) == (mask)) //checks if the mask is set
#define FLAGS_NOT_SET(entry, mask) (((entry) & (mask)) == 0) //checks if the mask is not set
#define ARCHIVE 0b00100000 //Archive bit
#define DIRECTORY 0b00010000 //Directory bit
#define VOL_NAME 0b00001000 //Volume name bit
#define SYSTEM 0b00000100 //System bit
#define HIDDEN 0b00000010 //Hidden bit
#define READ_ONLY 0b00000001 //Read only bit
#define FSEEK_SET 0 //seek set flag
#define FSEEK_CUR 1 //seek current flag
#define FSEEK_END 2 // seek end flag

// linked list structures
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
    uint8_t LDIR_Ord;        // Order/ position in sequence/ set
    uint8_t LDIR_Name1[10];  // First 5 UNICODE characters
    uint8_t LDIR_Attr;       // = ATTR_LONG_NAME (xx001111)
    uint8_t LDIR_Type;       // Should = 0
    uint8_t LDIR_Chksum;     // Checksum of short name
    uint8_t LDIR_Name2[12];  // Middle 6 UNICODE characters
    uint16_t LDIR_FstClusLO; // MUST be zero
    uint8_t LDIR_Name3[4];   // Last 2 UNICODE characters
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

// volume structure
typedef struct Volume
{
    uint16_t *FAT;
    BootSector *bootSector;
    char *path;

} Volume;

// File structure
typedef struct File
{
    off_t offset;
    int size;
    ListHead *clusterList;
    Volume *fileVolume;

} File;

// name converter
void convertToNameString(uint8_t *name, char *output)
{
    int i, j = 0;
    for (i = 0; i < 8; i++)
    {
        if (name[i] != ' ') // clear trailing whitespace for the 8 part of the 8.3
        {
            output[j] = name[i];
            j++;
        }
    }
    if (!((name[8] == ' ') && (name[9] == ' ') && (name[10] == ' '))) // check if file extension empty
    {
        output[j++] = '.'; // add the dot in the 9th slot (name[8])
        for (i = 8; i < 11; i++)
        {
            if (name[i] != ' ')
            {
                output[j] = name[i]; // clear trailing whitespace for the 3 part of the 8.3
                j++;
            }
        }
    }
    output[j] = '\0'; // add null terminator
}

// create list function
ListHead *createList()
{
    ListHead *list = (ListHead *)malloc(sizeof(ListHead)); // malloc list
    list->nextNode = NULL;                                 // init nextnode
    list->lastNode = NULL;                                 // init lastnode
    return list;
}

// add node function
void addNode(ListHead *list, uint16_t data)
{
    Node *addedNode = (Node *)malloc(sizeof(Node)); // malloc node
    addedNode->data = data;                         // allocate data
    addedNode->nextNode = NULL;                     // add nextnode to be null
    if (list->nextNode == NULL)                     // check if start of the list
        list->nextNode = addedNode;                 // set the next node to this node

    else
        list->lastNode->nextNode = addedNode; // set the last node to point to the added node

    list->lastNode = addedNode; // make the last node the added node
}

// free the elements in the list, then free the listhead itself
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

// reader function
int reader(char *path, void *out, size_t bytes, off_t offset)
{
    int fd = open(path, 0); // open the file at path and get the file desc
    if (fd == -1)           // error handle the file desc
    {
        printf("Invalid Filename or Path.");
        return -1;
    }
    lseek(fd, offset, SEEK_SET);     // seek to the desire offset
    int size = read(fd, out, bytes); // read the desired bytes into the output
    if (size == -1)                  // error handle the read
    {
        printf("Invalid file.");
        close(fd); // close in this case
        return -1;
    }
    close(fd); // close
    return size;
}

// cluster compiler (the list of clusters for a file)
ListHead *clusterCompiler(uint16_t *FAT, uint32_t index)
{
    ListHead *clusterList = createList();                       // create instance of list
    uint32_t clusterCursor = index;                             // where the index
    while ((clusterCursor < 0x0000FFF8) && (clusterCursor > 1)) // check for index <1 and not index less than the end of the cluster
    {
        addNode(clusterList, clusterCursor); // addnode
        clusterCursor = FAT[clusterCursor];  // go to next cluster
    }

    return clusterList; // return cluster list
}

// open file
extern File *openFile(Volume *vol, ShortDirEntry *ent)
{

    File *file = (File *)malloc(sizeof(File));              // malloc new file
    if (FLAGS_NOT_SET(ent->DIR_Attr, VOL_NAME | DIRECTORY)) // check that it's file
    {
        file->clusterList = clusterCompiler(vol->FAT, (ent->DIR_FstClusHI) << 16 | (ent->DIR_FstClusLO)); // get cluster list
        file->offset = 0;                                                                                 // offset initalized to 0
        file->size = ent->DIR_FileSize;                                                                   // file size is the entry size
        file->fileVolume = vol;                                                                           // the associated volume of the file
        return file;                                                                                      // return volume
    }
    else
    {
        return NULL; // if invalid return a NULL pointer
    }
};

// seek function
extern off_t seekFile(File *file, off_t offset, int whence)
{

    switch (whence) // switch statement for whence
    {
    case FSEEK_SET:     // case FSEEK_SET
        if (offset < 0) // if offset is negative handling
        {
            printf("Offset size not valid.");
            return -1; // return error number
        }
        file->offset = offset; // set offset
        return file->offset;   // return the offset val
    case FSEEK_CUR:
        if (file->offset + offset < 0)
        {
            printf("Offset size not valid.");
            return -1; // return error number
        }
        file->offset += offset; // add toffset
        return file->offset;    // return the offset val
    case FSEEK_END:
        if (file->size + offset < 0)
        {
            printf("Offset size not valid.");
            return -1; // return error number
        }
        file->offset = file->size; // go to end of file and set that as the offset
        return file->offset;       // return the offset val

    default:
        return -1; // if whence is invalid
    }
};

// close file function
extern void closeFile(File *file)
{
    freeList(file->clusterList); // free the list nodes and list head
    free(file);                  // free the file itself
};

// read file function
extern ssize_t readFile(File *file, void *buffer, size_t length)
{

    int clusterIndex = file->offset / CLUSTER_SIZE(file->fileVolume->bootSector); // which cluster does the offset start
    int startRead = file->offset % CLUSTER_SIZE(file->fileVolume->bootSector);    // which byte in the cluster does the offset start
    ssize_t totalBytesRead = 0;                                                   // variable to return
    if (length > (file->size) - (file->offset))                                   // capping the length so it doesn't go beyond the end of the size
    {
        length = file->size - file->offset;
    }
    Node *currentNode = file->clusterList->nextNode;              // get a node pointer reference
    for (int i = 0; i < clusterIndex && currentNode != NULL; i++) // iterate through to the cluster the offset defines and see if the cluster is not null
    {
        currentNode = currentNode->nextNode;
    }
    for (; currentNode != NULL && length > 0; currentNode = currentNode->nextNode) //iterate through the remaining clusters to read through
    {
        off_t offset = DATA_OFFSET(file->fileVolume->bootSector) + ((currentNode->data - 2) * CLUSTER_SIZE(file->fileVolume->bootSector)) + startRead; //navigate to cluster in data and add local offset, which uses startRead
        int cap = CLUSTER_SIZE(file->fileVolume->bootSector) - startRead; //the cap of the data to read
        int bytesRead; //current bytes read
        if (cap < length) //check of the cap is less than length so that to read up to cap
        {
            bytesRead = reader(file->fileVolume->path, buffer, cap, offset);
        }
        else
        {
            bytesRead = reader(file->fileVolume->path, buffer, length, offset); //read to length
        }

        if (bytesRead == -1) //invalid file check
        {
            printf("Invalid file.");
            return -1;
        }

        length -= bytesRead; //remove how many just read
        buffer += bytesRead; //push buffer pointer forward
        totalBytesRead += bytesRead; //add return value
        seekFile(file, bytesRead, FSEEK_CUR); //seek forward the number of bytes just read
    }

    return totalBytesRead; //return the size of what was just read
};

//load volume function
Volume *loadVol(char *path) 
{
    Volume *vol = (Volume *)malloc(sizeof(Volume)); //malloc a volume
    vol->path = path;

    BootSector *bootSector = (BootSector *)malloc(sizeof(BootSector)); //malloc a volume

    if ((reader(path, bootSector, sizeof(BootSector), 0)) == -1) // read a boot sector
    {
        printf("Invalid file."); //error handling
        return NULL; //return null pointer
    }

    uint16_t *FAT = (uint16_t *)malloc(FAT_SIZE(bootSector));

    if ((reader(path, FAT, FAT_SIZE(bootSector), FAT_OFFSET(bootSector))) == -1)
    {
        printf("Invalid file."); //error handling
        return NULL; //return null pointer
    }

    vol->FAT = FAT; //assign to the volume
    vol->bootSector = bootSector; //assign to the volume

    return vol; //return volume
}

void UTF16ToASCII(uint8_t *utf16, char *ASCIIOut, int length)
{
    for (int i = 0; i < length; i++)
    {
        ASCIIOut[i] = utf16[i * 2]; //reads into asciiout every other byte in the utf16 string of data since ascii only is used and ascii values in utf is set as 0 ascii 0 ascii etc.
    }
}

int main()
{

    Volume *volume = loadVol("fat16.img"); //initialize volume

    ShortDirEntry **rootDir = (ShortDirEntry **)malloc(DIR_SIZE(volume->bootSector)); //create a list of short dir entries for the root dir

    for (int i = 0; i < volume->bootSector->BPB_RootEntCnt; i++) //initialize root dir
    {
        ShortDirEntry *entry = (ShortDirEntry *)malloc(sizeof(ShortDirEntry)); //malloc a short dir entry
        reader(volume->path, entry, sizeof(ShortDirEntry), (DIR_OFFSET(volume->bootSector) + (i * sizeof(ShortDirEntry)))); //read into the malloced reader
        rootDir[i] = entry; //add to root dir at i
    }
    printf("%-30s%s%20s%28s%21s%20s  %s\n", "Filename", "File Attributes", "Date Modified", "Time Modified", "File Size", "Starting Cluster", "Long Filename"); //header for list of items in root dir

    for (int i = 0; i < volume->bootSector->BPB_RootEntCnt; i++) //iterate through the root dir
    {

        int longNameLength = ((((LongDirEntry *)rootDir[i])->LDIR_Ord & 0b1111) * 13);//get the number of long ents and therefore the length by *13
        char longName[longNameLength + 1]; //set the long name size for the long name char and + 1 for null terminator
        longName[longNameLength] = '\0'; //set the last character to null terminator
        for (; i < volume->bootSector->BPB_RootEntCnt && (FLAGS_SET(rootDir[i]->DIR_Attr, VOL_NAME | SYSTEM | HIDDEN | READ_ONLY) && FLAGS_NOT_SET(rootDir[i]->DIR_Attr, ARCHIVE | DIRECTORY)); i++) //iterate through from the outer loop for every long dir entry and till the end
        {
            LongDirEntry *longEnt = (LongDirEntry *)rootDir[i]; //read the long entry
            longNameLength -= 13; //subtract the length to write to
            UTF16ToASCII(longEnt->LDIR_Name1, longName + longNameLength, 5); //convert and write
            UTF16ToASCII(longEnt->LDIR_Name2, longName + longNameLength + 5, 6); //convert and write
            UTF16ToASCII(longEnt->LDIR_Name3, longName + longNameLength + 11, 2); //convert and write
        }

        if (rootDir[i]->DIR_Name[0] == 0x0) //end of dir case
            break; 

        if (rootDir[i]->DIR_Name[0] != 0xE5) //if not dead entry
        {
            char name[13]; //name 
            convertToNameString(rootDir[i]->DIR_Name, name); //convert name for printing
            printf("%-30s", name); //name print
            printf("%c%c%c%c%c%c",
                   (rootDir[i]->DIR_Attr & 0x20) ? 'A' : '-',
                   (rootDir[i]->DIR_Attr & 0x10) ? 'D' : '-',
                   (rootDir[i]->DIR_Attr & 0x08) ? 'V' : '-',
                   (rootDir[i]->DIR_Attr & 0x04) ? 'S' : '-',
                   (rootDir[i]->DIR_Attr & 0x02) ? 'H' : '-',
                   (rootDir[i]->DIR_Attr & 0x01) ? 'R' : '-'); //attribute printer using bit masking
            printf("%20d/%d/%d", ((rootDir[i]->DIR_WrtDate) >> 9) + 1980, ((rootDir[i]->DIR_WrtDate) >> 5) & 0b1111, (rootDir[i]->DIR_WrtDate) & 0b11111); //write date masking printer
            printf("%20d:%d:%d", ((rootDir[i]->DIR_WrtTime) >> 11), ((rootDir[i]->DIR_WrtTime) >> 5) & 0b111111, ((rootDir[i]->DIR_WrtTime) & 0b11111) * 2); //write time masking printer
            printf("%20d bytes", rootDir[i]->DIR_FileSize); //size printer
            printf("%20d  ", ((rootDir[i]->DIR_FstClusHI) << 16 | (rootDir[i]->DIR_FstClusLO))); //cluster printer
            printf("%s\n", longName); //print long name
        }
    }

    File *file = openFile(volume, rootDir[18]); //test for given file (sessions.txt)
    char buffer[file->size]; //give buffer for read
    readFile(file, buffer, file->size); //read into buffer
    printf("%s\n", buffer); //print the buffer
    closeFile(file); //close file
    ListHead *cluster = clusterCompiler(volume->FAT, 6); // test cluster compiler
    freeList(cluster); //free cluster

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
        volume->bootSector->BS_VolLab); //printer for boot sector info

    //free root dir
    for (int i = 0; i < volume->bootSector->BPB_RootEntCnt; i++)
    {
        free(rootDir[i]);
    }
    free(rootDir);
    //free boot sector
    free(volume->bootSector);
    //free FAT
    free(volume->FAT);
    //free volume
    free(volume);
}
