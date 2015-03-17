#include "fs.h"
#include <string.h>
#include <stdio.h>

void 
usage(char *prog)
{
    fprintf(stderr, "usage: %s <disk image file>\n", prog);
    exit(1);
}

int
main(int argc, char *argv[])
{
    if (argc != 2) {
	usage(argv[0]);
    }
    char *path = argv[1];

    if (FS_Boot(path) == -1) {
        fprintf(stderr, "FS_Boot Error: %d\n", osErrno);
        return -1;
    }
    
    char cmdstr[512];
    while (1) {
        printf("shell> ");
        fgets(cmdstr, 512, stdin);
        int i=0;
        while (cmdstr[i] != '\n' && i<511)  i++;
        cmdstr[i] = 0;
        if (strcmp(cmdstr, "exit;") == 0)
            break;
        char *cmd = strtok(cmdstr, " ");
        char *arg = strtok(NULL, " ");
        if (!cmd)
            continue;
        else if (strcmp(cmd, "create") == 0) {
            if(File_Create(arg) == -1) {
                fprintf(stderr, "File_Create Error: %d\n", osErrno);
            }
        }
        else if (strcmp(cmd, "open") == 0) {
            int fd = File_Open(arg);
            if(fd == -1) {
                fprintf(stderr, "File_Open Error: %d\n", osErrno);
            }
            else
                fprintf(stdout, "fd: %d\n", fd);
        }
        else
            printf("shell: command not found\n");
    }

    FS_Sync();
    return 0;
}

