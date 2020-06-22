#include <sys/mman.h>
#include "common.h"

int main() {
    shm_unlink(SHM_ID);
    return 0;
}