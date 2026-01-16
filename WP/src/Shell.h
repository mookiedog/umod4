#ifndef SHELL_H
#define SHELL_H

#include "umod4_WP.h"

#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lfs.h"

class Shell {
    TaskHandle_t shell_taskHandle;
    lfs_t* lfs;

    FILE* infile;
    char cmdBuf[256];

    const char* cmd;
    char* args;
    uint32_t dbg;

    #if 0
    static void operation_ls(struct lfs_info* info);
    void iterate(const char* directory, const char* globName, void (*operation)(struct lfs_info* info));
    #endif

    void cmd_ls(char* args);
    void cmd_touch(char* args);
    void cmd_rm(char* args);
    void cmd_hd(char* args);
    void cmd_pwd(char* args);
    void cmd_sdperf(char* args);
    void cmd_flashEp(char* args);

    const char* cwd;

    public:
        Shell(lfs_t* lfs);
        void shell_task();

        static char* decompose(char** theString, const char* separatorList);
        static char* decomposeArg(char* originalArg);
        static char* skipWhite(char* s);
};

#endif
