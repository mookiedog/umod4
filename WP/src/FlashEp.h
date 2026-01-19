
#include <stdint.h>
#include "lfs.h"
#include "FlashBuffer.h"

class FlashEp {
public:
    static int32_t flashUf2(const char* args, bool verbose=false);


private:
    static bool handle_metablock(uint32_t start_addr, uint8_t *buffer, size_t size, bool verbose=false);
    static int32_t process_uf2(lfs_t *lfs, const char *path, bool verbose=false);

    static flashBufferInterface_1_t fbi_1;
};
