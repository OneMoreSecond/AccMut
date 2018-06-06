//
// Created by txh on 18-4-12.
//
// This file implements low-level fork for accmut_GoodVar.cpp
//

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "sys/time.h"
#include "sys/mman.h"
#include "sys/wait.h"

#include "llvm/AccMutRuntime/accmut_config.h"
#include "llvm/AccMutRuntime/accmut_exitcode.h"

extern struct itimerval ACCMUT_PROF_TICK;
extern struct itimerval ACCMUT_REAL_TICK;

extern int pnum;

#define PAGESIZE (4096)
#define __real_fprintf fprintf

int32_t goodvar_fork(int mutID)
{
#if 1
    fprintf(stderr, "#\n");
#endif

    struct itimerval empty;
    empty.it_value.tv_sec = 0;
    empty.it_value.tv_usec = 0;
    empty.it_interval.tv_sec = 0;
    empty.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &empty, NULL);

    pid_t pid = fork();

    if(pid < 0){
        ERRMSG("fork FAILED ");
        exit(ENV_ERR);
    }

    if(pid == 0)
    {
        int r1 = setitimer(ITIMER_REAL, &ACCMUT_REAL_TICK, NULL);
        int r2 = setitimer(ITIMER_PROF, &ACCMUT_PROF_TICK, NULL);

        if(r1 < 0 || r2 < 0){
            ERRMSG("setitimer ERR ");
            exit(ENV_ERR);
        }

        if (mprotect((void *)(&MUTATION_ID), PAGESIZE, PROT_READ | PROT_WRITE)) {
            perror("mprotect ERR : PROT_READ | PROT_WRITE");
            exit(ENV_ERR);
        }

        MUTATION_ID = mutID;

        if (mprotect((void *)(&MUTATION_ID), PAGESIZE, PROT_READ)) {
            perror("mprotect ERR : PROT_READ");
            exit(ENV_ERR);
        }

        // isChild = true
        return 1;
    }
    else
    {
        int pr = waitpid(pid, NULL, 0);
        if(pr < 0){
            ERRMSG("waitpid ERR ");
            exit(ENV_ERR);
        }

        if (MUTATION_ID != 0)
        {
            //int r1 =
            setitimer(ITIMER_REAL, &ACCMUT_REAL_TICK, NULL);
            //int r2 =
            setitimer(ITIMER_PROF, &ACCMUT_PROF_TICK, NULL);
        }
        // isChild = false
        return 0;
    }

}