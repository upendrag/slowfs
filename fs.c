#include "fs.h"
#include "disk.h"
#include <unistd.h>
#include <string.h>

#define NUM_OF_BLOCKS 30
#define SUPER_BLOCK_SECTOR 0
#define INODE_BMP_SECTOR 1
#define DATABLK_BMP_SECTOR 2
#define NUM_OF_DATABLK_BITMAP 3

#define INODE_BLOCK_START 5
#define NUM_OF_INODE_BLOCKS ((SECTOR_SIZE * 8)/32)

#define INODE_SECTOR(i_num) INODE_BLOCK_START + ((i_num * sizeof(inode_t)) / SECTOR_SIZE)
#define INODE_SECTOR_INDEX(i_num) (i_num * sizeof(inode_t)) % SECTOR_SIZE;    

#define DATA_BLOCK_START (INODE_BLOCK_START + NUM_OF_INODE_BLOCKS)

#define MAX_ENTRIES 25
#define MAX_OPEN_FILES 256
#define MAX_FILE_LEN 16

// global errno value here
int osErrno;

static char *disk_path;

typedef enum {
    REGULAR,
    DIRECTORY
} file_t;

typedef struct {
    int size;
    file_t type;
    int blocks[NUM_OF_BLOCKS];
} inode_t;

typedef struct {
    char name[MAX_FILE_LEN];
    int inode;
} dir_entry;

struct open_file {
    int fp;
    int block;
    int isect_num;
    int isect_idx;
};

static struct open_file open_files[MAX_OPEN_FILES];

int set_inode_bmp(int);
int create_dir(int, int, const char *);
int get_free_blk(void);
int get_inode_num(char *, int);
int list_dirs(dir_entry *, int);
int dentry_exists(const char *, int, int *);
int create_file(const char *, int);
int add_dir_entry(const char *, int, int);

int 
FS_Boot(char *path)
{
    printf("FS_Boot %s\n", path);
    //set default values to open_file entries;
    memset(open_files, -1, MAX_OPEN_FILES);

    if (Disk_Init() == -1) {
	    printf("Disk_Init() failed: %d\n", diskErrno);
	    osErrno = E_GENERAL;
	    return -1;
    }
    
    if (access(path, F_OK) == 0) {
        if (Disk_Load(path) == -1) {
            printf("Disk_Load() failed: %d\n", diskErrno);
            osErrno = E_GENERAL;
            return -1;
        }
        //Read default sectors
        char buf[SECTOR_SIZE];
        Disk_Read(0, buf);
        if (buf[0] != '#' || buf[1] != '#' || buf[2] != 'S'
            || buf[3] != 'B' || buf[4] != '#' || buf[5] != '#'
            || buf[6] != '\n' || buf[7] != 0) {
            printf("Disk_Load() failed: Corrupted Image\n");
            osErrno = E_GENERAL;
            return -1;
        }
    }
    else {
        //Write default data
        char buf[SECTOR_SIZE] = { '#', '#', 'S', 'B', '#', '#', '\n', 0};
        Disk_Write(0, buf);
        int root_inode_num = set_inode_bmp(0);
        if (root_inode_num == -1) {
            printf("ERROR: Unable to create inode\n");
            osErrno = E_GENERAL;
            return -1;
        }
        if (create_dir(root_inode_num, -1, "/") == -1) {
            printf("ERROR: Unable to create root directory\n");
            osErrno = E_GENERAL;
            return -1;
        }
    }
    disk_path = path;

    return 0;
}

int set_inode_bmp(int inode_num)
{
    //TODO: critical section
    Sector inode_bmp;
    Disk_Read(INODE_BMP_SECTOR, inode_bmp.data);
    if (inode_num < 0) {
        //find the vacant inode block
        int i;
        for (i=0; i<SECTOR_SIZE; i++) {
            if (inode_bmp.data[i] != 0xF) {
                int bit_index = 0;
                while ((inode_bmp.data[i] >> bit_index) & 0x1) bit_index++;
                inode_num = (i*8) + bit_index;
                inode_bmp.data[i] |= (0x1 << bit_index);
                Disk_Write(INODE_BMP_SECTOR, inode_bmp.data);
                break;
            }
        }
    }
    else {
        //check if given inode is already taken
        int arr_idx = inode_num / 8;
        int bit_idx = inode_num % 8;
        if (inode_bmp.data[arr_idx] 
            & (0x1 << bit_idx))
            return -1;
        inode_bmp.data[arr_idx] |= (0x1 << bit_idx);
        Disk_Write(INODE_BMP_SECTOR, inode_bmp.data);
    }
    return inode_num;
}

int create_dir(int inode_num, int parent_inode_num,
    const char *name)
{
    if (parent_inode_num >= 0) {
        //TODO
    }
    int sector_num = INODE_SECTOR(inode_num);//INODE_BLOCK_START + ((inode_num * sizeof(inode_t)) / SECTOR_SIZE);
    int sector_index = INODE_SECTOR_INDEX(inode_num);//(inode_num % sizeof(inode_t)) / SECTOR_SIZE;
    Sector sector;

    //Update inode block
    Disk_Read(sector_num, sector.data);
    inode_t *inode = (inode_t *) (sector.data + sector_index);
    inode->size = 2 * sizeof(dir_entry);    
    inode->type = DIRECTORY;
    int block_num = get_free_blk();
    if (block_num == -1) {
        return -1;
    }
    inode->blocks[0] = DATA_BLOCK_START + block_num;
    Disk_Write(sector_num, sector.data);

    //Update data block
    Disk_Read(DATA_BLOCK_START + block_num, sector.data);
    dir_entry *dentry = (dir_entry *) sector.data;
    strcpy(dentry->name, ".");
    dentry->inode = inode_num;
    dentry++;
    strcpy(dentry->name, "..");
    if (parent_inode_num < 0)
        dentry->inode = inode_num;
    else
        dentry->inode = parent_inode_num;
    Disk_Write(DATA_BLOCK_START + block_num, sector.data);
    return 0;
}

int get_free_blk(void)
{
    //TODO: Critical section
    int i, j;
    Sector data_bmp;
    for (i = 0; i < NUM_OF_DATABLK_BITMAP; i++) {
        Disk_Read(DATABLK_BMP_SECTOR + i, data_bmp.data);
        for (j = 0; j < SECTOR_SIZE; j++) {
            if (data_bmp.data[j] != 0xF) {
                int bit = 0;
                int blk;
                while ((data_bmp.data[j] >> bit) & 0x01) bit++;
                blk = (i * SECTOR_SIZE) + (j * 8) + bit;
                // No more free data blocks
                if ((DATA_BLOCK_START + blk) > NUM_SECTORS) {
                    return -1;
                }
                data_bmp.data[j] |= (0x01 << bit);
                Disk_Write(DATABLK_BMP_SECTOR + i, data_bmp.data);
                return blk;
            }
        }
    }
    return -1;
}

int
FS_Sync()
{
    printf("FS_Sync\n");
    if (disk_path && Disk_Save(disk_path) == -1) {
	    printf("Disk_Save() failed for %s: %d\n", disk_path, diskErrno);
	    osErrno = E_GENERAL;
	    return -1;
    }
    return 0;
}

int list_dirs(dir_entry *dentries, int inode_num)
{
    Sector inode_blk;
    int sector_num = INODE_SECTOR(inode_num); //INODE_BLOCK_START + (inode_num * sizeof(inode_t)) / SECTOR_SIZE; 
    int sector_index = INODE_SECTOR_INDEX(inode_num);
    Disk_Read(sector_num, inode_blk.data);
    inode_t *inode = (inode_t *) (inode_blk.data + sector_index);// ((inode_num * sizeof(inode_t)) % SECTOR_SIZE));
    if (inode->type == REGULAR)
        return -1;

    Sector data_blk;
    if (inode->blocks[0] > 0) {
        int count = 0;
        Disk_Read(inode->blocks[0], data_blk.data);
        dir_entry *dentry = (dir_entry *) data_blk.data;
        while (dentry->name[0]) {
            memcpy(dentries++, dentry++, sizeof(dir_entry));
            count++;
        }
        return count;
    }
    return -1;
}

int dentry_exists(const char *name, int inode_num,
    int *num)
{
    dir_entry dirs[MAX_ENTRIES];
    int num_entries = list_dirs(dirs, inode_num);
    *num = num_entries;

    int i=0, retval = -1;
    for (i=0; i<num_entries; i++) {
        if (strcmp(dirs[i].name, name) == 0) {
            retval = dirs[i].inode;
            break;
        }
    }
    return retval;
}
 
int add_dir_entry(const char *name, int new_inum,
    int p_inum)
{
    int sector_num = INODE_SECTOR(p_inum);
    int sector_index = INODE_SECTOR_INDEX(p_inum);
    Sector inode_blks;
    Disk_Read(sector_num, inode_blks.data);
    inode_t *p_inode = (inode_t *) (inode_blks.data + sector_index);
    if (p_inode->type != DIRECTORY)
        return -1;

    Sector data_blk;
    if(p_inode->blocks[0] < DATA_BLOCK_START)
        return -1;
    Disk_Read(p_inode->blocks[0], data_blk.data);
    dir_entry *dentry = (dir_entry *) data_blk.data;
    while (dentry->name[0]) dentry++;
    strcpy(dentry->name, name);
    dentry->inode = new_inum;
    Disk_Write(p_inode->blocks[0], data_blk.data);

    return 0;
}

int create_file(const char *name, int p_inode_num)
{
    int new_inode_num;
    int num_entries = 0;
    if (dentry_exists(name, p_inode_num, &num_entries) > 0) {
        printf("create_file: File exists\n");
        goto er;
    }
    if (num_entries >= MAX_ENTRIES) {
        printf("create_file: Max files per directory reached\n");
        goto er;
    }
    if ((new_inode_num = set_inode_bmp(-1)) == -1) {
        printf("create_file: Error allocating inode\n");
        goto er;
    }

    //update parent directory
    if (add_dir_entry(name, new_inode_num, p_inode_num) == -1) {
        //TODO: reset inode bmp and clear inode entry
        goto er;
    }
    return 0;

er: osErrno = E_CREATE;
    return -1;
}

int path_lookup(char *path, char *file_name)
{
    if (path[0] != '/')
        goto er;

    char *name = strtok(path, "/");
    if (!name)
        goto er;

    int num_entries=0, inode_num = 0;  //root inode
    while(1) {
        printf ("%s\n", name);
        strcpy(file_name, name);
        name = strtok(NULL, "/");
        if (name) {
            if((inode_num = dentry_exists(file_name, 
                inode_num, &num_entries)) == -1)
                goto er;            
        }
        else
            break;
    }
    return inode_num;
    
er: printf("Error: Path does not exist\n");
    return -1;
}

int
File_Create(char *file)
{
    printf("File_Create\n");
    char file_name[MAX_FILE_LEN] = {0};
    int inode_num = -1;
    inode_num =  path_lookup(file, file_name);
    if (inode_num == -1) {
        osErrno = E_CREATE;
        return -1;
    }
    printf("name: %s\n", file_name);
    if (file_name[MAX_FILE_LEN -1]) {
        printf("Error: Name too long\n");
        osErrno = E_CREATE;
        return -1;
    }
    return create_file(file_name, inode_num);
}

int
File_Open(char *file)
{
    printf("FS_Open\n");
    char file_name[MAX_FILE_LEN] = {0};
    int inode_num = -1;
    inode_num =  path_lookup(file, file_name);
    if (inode_num == -1) 
        goto er;
    int num_entries = 0;
    inode_num = dentry_exists(file_name, inode_num, 
            &num_entries);
    if (inode_num == -1)
        goto er;
    int sect_num = INODE_SECTOR(inode_num);
    int sect_idx = INODE_SECTOR_INDEX(inode_num);
    Sector inode_blk;
    Disk_Read(sect_num, inode_blk.data);
    inode_t *inode = (inode_t *) (inode_blk.data + sect_idx);
    if (inode->type != REGULAR)
        goto er;

    int i = 0;
    while (i < MAX_OPEN_FILES && open_files[i].fp != -1) {
        if (sect_num == open_files[i].isect_num
            && sect_idx == open_files[i].isect_idx) {
            osErrno = E_FILE_IN_USE;
            return -1;
        }
        i++;
    }
    if (i == MAX_OPEN_FILES) {
        osErrno = E_TOO_MANY_OPEN_FILES;
        return -1;
    }
    open_files[i].fp = 0;
    open_files[i].block = 0;
    open_files[i].isect_num = sect_num;
    open_files[i].isect_idx = sect_idx;

    return i;

er: osErrno = E_NO_SUCH_FILE;
    return -1;
}

int
File_Read(int fd, void *buffer, int size)
{
    printf("FS_Read\n");
    return 0;
}

int
File_Write(int fd, void *buffer, int size)
{
    printf("FS_Write\n");
    return 0;
}

int
File_Seek(int fd, int offset)
{
    printf("FS_Seek\n");
    return 0;
}

int
File_Close(int fd)
{
    printf("FS_Close\n");
    return 0;
}

int
File_Unlink(char *file)
{
    printf("FS_Unlink\n");
    return 0;
}


// directory ops
int
Dir_Create(char *path)
{
    printf("Dir_Create %s\n", path);
    return 0;
}

int
Dir_Size(char *path)
{
    printf("Dir_Size\n");
    return 0;
}

int
Dir_Read(char *path, void *buffer, int size)
{
    printf("Dir_Read\n");
    return 0;
}

int
Dir_Unlink(char *path)
{
    printf("Dir_Unlink\n");
    return 0;
}

