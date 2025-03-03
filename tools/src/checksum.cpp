// Checksum binary files in a variety of fashions


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <climits>

const char* progname;

const char* usageMsg =
"Usage: %s [OPTIONs] ... <infile>\n";

const char* helpMsg =
"Checksum a file. Checksum algorithm can be one of:\n"
"  -s --s16             A standard 16-bit additive checksum (default)\n"
"  -3 --murmur3         A murmur3 hash\n"
"  -o --start-offset    start offset where calculation begins (default: 0)\n"
"  -l --length          number of bytes to include in the calculation (default: all)"

"\n"
"  -v --verbose\n"
"  -h --help       Print this message\n"
;

enum ALGO {ALGO_SUM16, ALGO_MURMUR3};
enum ALGO algo = ALGO_SUM16;

int32_t verbose_flag = false;
int32_t debug_flag = false;

const uint32_t murmurHash_seed = ~0x0;

const char* inPath;

// Buffer size must be a multiple of words
#define BUFFER_SIZE_BYTES 2048

#if (BUFFER_SIZE_BYTES & 3) != 0
    #error "BUFFER_SIZE_BYTES must be a multiple of 4!"
#endif

// The default starting offset is 0
int32_t offset_arg = 0;

// Make a note that a length argument has not been seen
int32_t length_arg = -1;

void processOptions(int argc, char** argv)
{
    char* eptr;

    int c;
    while (1) {
        static struct option long_options[] =  {
            /* These options set a flag. */
            {"s16",          no_argument,       0, 's'},
            {"murmur3",      no_argument,       0, '3'},

            {"debug",        no_argument,       0, 'd'},
            {"verbose",      no_argument,       0, 'v'},
            {"help",         no_argument,       0, 'h'},

            {"start-offset", required_argument, 0, 'o'},
            {"length",       optional_argument, 0, 'l'},

            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        // A ':' following a short option letter means that the option requires an argument
        c = getopt_long (argc, argv, "s3dhvo:l:", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
        break;

        switch (c) {
        case 0:
            /* If this option set a flag, do nothing else now. */
            if (long_options[option_index].flag != 0) {
                break;
            }

            if (debug_flag) printf ("option %s", long_options[option_index].name);
            if (optarg) {
                printf (" with arg %s", optarg);
            }
            printf ("\n");
            break;

        case 'h':
            printf(usageMsg, progname);
            printf("%s", helpMsg);
            exit(-1);
            break;

        case 's':
            algo = ALGO_SUM16;
            break;

        case '3':
            algo = ALGO_MURMUR3;
            break;

        case 'd':
            debug_flag = true;
            if (debug_flag) printf ("option -d with value `%s'\n", optarg);
            break;

        case 'v':
            if (debug_flag) printf ("option -v with value `%s'\n", optarg);
            verbose_flag = true;
            break;

        case 'o':
            if (debug_flag) printf ("option -o with value `%s'\n", optarg);
            offset_arg = strtol(optarg, &eptr, 16);
            if (*eptr) {
                fprintf(stderr, "%s: Illegal character in offset argument: '%c'\n", progname, *eptr);
                exit(-1);
            }
            if (offset_arg == 0) {
                if (errno == EINVAL) {
                    fprintf(stderr, "%s: Bad offset argument: %d\n", progname, errno);
                    exit(-1);
                }
            }
            if (offset_arg == LONG_MIN || offset_arg == LONG_MAX) {
                // If the value provided was out of range, display a warning message
                if (errno == ERANGE) {
                    fprintf(stderr, "%s: Offset argument is out of range\n", progname);
                }
            }
            break;

        case 'l':
            if (debug_flag) printf ("option -l with value `%s'\n", optarg);
            length_arg = strtol(optarg, &eptr, 16);
            if (*eptr) {
                fprintf(stderr, "%s: Illegal character in length argument: '%c'\n", progname, *eptr);
                exit(-1);
            }
            if (length_arg <= 0) {
                if (errno == EINVAL) {
                    fprintf(stderr, "%s: Bad length argument: %d\n", progname, errno);
                    exit(0);
                }
            }
            if (length_arg == LONG_MIN || length_arg == LONG_MAX) {
                // If the value provided was out of range, display a warning message
                if (errno == ERANGE) {
                    fprintf(stderr, "%s: length argument is out of range\n", progname);
                }
            }
            break;

        case '?':
            /* getopt_long already printed an error message. */
            break;

        default:
            abort ();
        }
    }
}

void sum16(FILE* fptr)
{
    uint8_t buffer[BUFFER_SIZE_BYTES];
    uint32_t bytesRead, totalBytesRead;

    totalBytesRead = 0;
    uint16_t cksum16 = 0;
    do {
        bytesRead = fread((void*)buffer, 1, sizeof(buffer), fptr);
        totalBytesRead += bytesRead;
        if (bytesRead > 0) {
            for (uint32_t i=0; i< bytesRead; i++) {
                cksum16 += buffer[i];
            }
        }
    } while (bytesRead > 0);
    printf("0x%04X\n", cksum16);

    if (verbose_flag) {printf("Total bytes read: %d\n", totalBytesRead);}
}


static inline uint32_t murmur_32_scramble(uint32_t k)
{
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}

void murmur3(FILE* fptr, int32_t offset, int32_t length)
{
    uint32_t h, k;
    uint32_t murmur3_hash;

    #if 0
    // Do a sanity test to verify that our murmur3 algo works as expected.
    // This test comes from here: https://stackoverflow.com/questions/14747343/murmurhash3-test-vectors
    h = seed = 0x9747b28c;
    const char* test_key = "The quick brown fox jumps over the lazy dog";
    murmur3_hash = murmur3_32((uint8_t*)test_key, strlen(test_key), seed);
    if (murmur3_hash != 0x2FA826CD) {
        fprintf(stderr, "%s: murmur3 test hash failed! saw %08x, expected %08x\n", progname, murmur3_hash, 0x2FA826CD);
        exit(-2);
    }
    #endif

    uint8_t buffer[BUFFER_SIZE_BYTES];
    const uint8_t *key;
    uint32_t bytesRead;

    // If an offset was specified, skip to the start of where the calculation should begin
    if (offset>0) {
        fseek(fptr, offset, SEEK_SET);
    }

    // Seed the calculation
    h = ~0x0;

    int32_t lengthRemaining = length;
    bool done;
    do {
        // Process the file in chunks no bigger than the size of our buffer
        uint32_t bytesToProcess = (sizeof(buffer) < lengthRemaining) ? sizeof(buffer) : lengthRemaining;

        // The last chunk might be smaller than our buffer
        bytesRead = fread((void*)buffer, 1, bytesToProcess, fptr);
        if (verbose_flag) {printf("bytes requested: %d, bytes actually read: %d\n", bytesToProcess, bytesRead);}

        if ((length>-1) && (bytesRead < bytesToProcess)) {
            fprintf(stderr, "%s: Requested starting offset and length goes past end of data file\n", progname);
            exit(-1);
        }

        lengthRemaining -= bytesRead;

        // Reset the key pointer to the start of the new chunk
        key = buffer;

        // Read from the buffer in groups of 4 bytes
        for (size_t i = bytesRead >> 2; i; i--)
        {
            // Here is a source of differing results across endiannesses.
            // A swap here has no effects on hash properties though.
            memcpy(&k, key, sizeof(uint32_t));
            key += sizeof(uint32_t);
            h ^= murmur_32_scramble(k);
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64;
        }

        done = (length == -1) ? (bytesRead != bytesToProcess) : (lengthRemaining>0);
    } while (!done);

    // Read the final bytes in the last word to be processed (if any)
    k = 0;
    for (size_t i = bytesRead & 3; i; i--)
    {
        k <<= 8;
        k |= key[i - 1];
    }

    // A swap is *not* necessary here because the preceding loop already
    // places the low bytes in the low places according to whatever endianness
    // we use. Swaps only apply when the memory is copied in a chunk.
    h ^= murmur_32_scramble(k);

    // Finalize
    h ^= length;            // The total length of the file that was processed
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    // Send the result to stdout
    printf("0x%08X\n", h);
}


int main(int argc, char **argv)
{
    FILE *fptr;

    // Extract the name we were invoked with, discarding any path info in front of the name.
    // We'll use the resulting string for prefacing error messages.
    progname = strrchr(argv[0], '/');
    if (!progname) {
        progname++;         // skip over the final '/' in front of the name
    }
    else {
        progname = argv[0];
    }

    processOptions(argc, argv);

    if (optind < argc) {
        inPath = argv[optind];
    }
    else {
        fprintf(stderr, "%s: Input pathname missing\n", progname);
        exit(-3);
    }

    if (debug_flag) {
        printf("input file:  <%s>\n", inPath);
    }

    if (verbose_flag) {printf("Processing file: <%s>\n", inPath);}
    fptr = fopen(inPath,"rb");
    if (!fptr) {
        fprintf(stderr, "%s: Unable to open input pathname <%s>\n", progname, inPath);
        exit(-4);
    }

    switch (algo) {
        case ALGO_SUM16:
            sum16(fptr);
            break;

        case ALGO_MURMUR3:
            murmur3(fptr, offset_arg, length_arg);
            break;

        default:
            fprintf(stderr, "%s: Internal Error: unknown checksum method\n", progname);
            exit(-6);
    }

    return(0);
}

