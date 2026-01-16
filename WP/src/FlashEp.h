
#include <stdint.h>
#include "lfs.h"

class FlashEp {
public:
    static int32_t process_uf2(lfs_t *lfs, const char *path);

private:
    static bool handle_metablock(uint32_t start_addr, uint8_t *buffer, size_t size);
};
