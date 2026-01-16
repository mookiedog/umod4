#ifndef SWD_LOADER_H
#define SWD_LOADER_H

#include "pico/stdlib.h"
#include "hardware/pio.h"

class SWDLoader {
public:
    SWDLoader(PIO pio = pio0, bool verbose = false);

    bool swd_reset();
    bool swd_load_program(const uint* addresses,
                         const uint** data,
                         const uint* data_len_in_bytes,
                         uint num_sections,
                         uint pc = 0x20000001,
                         uint sp = 0x20042000,
                         bool use_xip_as_ram = false);

private:
    // Internal state variables (formerly globals)
    uint pio_offset;
    uint pio_sm;
    const pio_program_t* pio_prog;
    float pio_clkdiv;
    PIO swd_pio;
    bool verbose;

    // Strict logic functions from original source
    void wait_for_idle();
    void switch_program(bool read, bool raw = false);
    void unload_pio();
    bool write_cmd(uint cmd, uint data);
    bool write_block(uint addr, const uint* data, uint len_in_words);
    bool write_reg(uint addr, uint data);
    bool read_cmd(uint cmd, uint& data);
    bool read_reg(uint addr, uint &data);
    void idle();
    bool connect(bool first = true, uint core = 0);
    bool load(uint address, const uint* data, uint len_in_bytes);
    bool start(uint pc, uint sp);
    bool swd_reset_internal();
    bool swd_load_program_internal(const uint* addresses,
                                  const uint** data,
                                  const uint* data_len_in_bytes,
                                  uint num_sections,
                                  uint pc,
                                  uint sp,
                                  bool use_xip_as_ram);
};

extern SWDLoader* swdLoader;

#endif // SWD_LOADER_H