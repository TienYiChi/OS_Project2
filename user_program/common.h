#ifndef _COMMON_H
#define _COMMON_H

#define SHM_ID "/IPC_SHM"
#define SHM_SIZE PAGE_SIZE*100

struct shm_comm_info {
    void *from_addr;
    void *to_addr;
    int len;
};

#endif