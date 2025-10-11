#if !defined HEAP_H
#define HEAP_H

extern "C" {
    extern const char* heap_start;
    extern const char* heap_end;

    void * pvPortMalloc( size_t xWantedSize );
    void vPortFree( void * pv );
}

#endif
