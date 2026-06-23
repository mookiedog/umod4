#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>

#define TRACE_MAX_ENTRIES 16

struct TraceEntry {
    const char* name;
    uint8_t* level;
};

namespace Trace {
    void reg(const char* name, uint8_t* level);
    int  getCount();
    const TraceEntry* getEntries();
    void loadConfig();
    void saveConfig();
    void deleteConfig();
    void cmdTrace(char* args);
}

#endif
