#include "Shell.h"
#include "string.h"
#include "ctype.h"

// The tiny_regex_c interface library:
#include "re.h"

// ----------------------------------------------------------------------------------
extern "C" void start_shell_task(void *pvParameters);

void start_shell_task(void *pvParameters)
{
  // The task parameter is the specific object instance we should be using in the ISR
  Shell* s = static_cast<Shell*>(pvParameters);

  // This allows us to invoke the task method on the correct instance
  s->shell_task();
  panic(LOCATION("Task should never return"));
}


// ----------------------------------------------------------------------------------
Shell::Shell(lfs_t* _lfs)
{
    lfs = _lfs;
    dbg =  1;

    // For the moment, our current working directory is hardcoded to be the top level directory '/'
    cwd = "/";

    xTaskCreate(start_shell_task, "Shell", 4096 /* words */, this, TASK_NORMAL_PRIORITY, &shell_taskHandle);
}


// ----------------------------------------------------------------------------------
const char* lfs_err_decode(int32_t err)
{
    static char buf[32];

    switch (err) {
        case  0: return "No error";
        case  -5: return "IO Error during device operation";
        case -84: return "Corrupted";
        case  -2: return "No directory entry";
        case -17: return "Entry already exists";
        case -20: return "Entry is not a directory";
        case -21: return "Entry is a directory";
        case -39: return "Directory not empty";
        case  -9: return "Bad file number";
        case -27: return "File too large";
        case -22: return "Invalid parameter";
        case -28: return "No space left on device";
        case -12: return "No more memory available";
        case -61: return "No data/attr available";
        case -36: return "File name too long";
        default:
            snprintf(buf, sizeof(buf), "Unknown error %d", err);
            return buf;
    }
}

// ----------------------------------------------------------------------------------
char* Shell::skipWhite(char* s)
{
	if (s) {
		while (isblank(*s)) {
			s++;
		}
	}

	return(s);
}

// ----------------------------------------------------------------------------------
// Decompose a string into two parts, separated by any char in the separatorList string.
// Leading whitespace at the start of theString is always ignored.
//
// Examples
// Input strings of the form:
//   NULL,                return NULL,  theString is unchanged
//   "",                  return "",    theString is unchanged
//   "foo<separator>bar", return "foo", set restOfString to "bar".
//   "foo<separator>",    return "foo", set restOfString to "".
//   "foo",               return "foo", set restOfString to NULL.
//
// Note that either <foo> or <bar> in the examples above can be quoted strings.

char* Shell::decompose(char** theString, const char* separatorList)
{
    if (!theString) {
        return NULL;
    }

    // As a convenience, skip leading whitespace
    *theString = skipWhite(*theString);


    // Split the originalString into two pieces:
    //   1) token gets set to point at everything up to, but not including the separator.
    //   2) restOfString gets set to point at everything after the separator.
    // The separator is not part of either string, and is discarded from further consideration.

    enum {PLAIN_TEXT, QUOTED_TEXT, LITERAL_CHAR};
    uint8_t state = PLAIN_TEXT;

    // The token is the chunk of string up to but not including any character in the separator string
    char* stringP = *theString;
    char* token = stringP;
    uint8_t separatorFound = false;

    while (stringP && *stringP) {
        if (state == PLAIN_TEXT) {
        // We are processing plain text.

        // We are done if the next char matches something in the separator list
        if (strchr(separatorList, *stringP)) {
            separatorFound = true;

            // Replace the separator char with a NULL char. This terminates the token.
            *stringP = 0;
            stringP++;
            break;
        }

        // If the next character is a double-quote, enter quoted-text processing mode.
        if (*stringP == '"') {
            state = QUOTED_TEXT;
        }

        stringP++;
        }
        else if (state == QUOTED_TEXT) {
        if (*stringP == '\\') {
            // A back-whack tells us to ignore the next non-NULL char.
            state = LITERAL_CHAR;

            // We destroy the \ char.
            // Copying by the original string's length copies the terminating NULL too.
            memmove(stringP, stringP+1, strlen(stringP));
        }
        else if (*stringP == '"') {
            // Found a closing double-quote.
            state = PLAIN_TEXT;
            stringP++;
        }
        else {
            // A regular char within the string
            stringP++;
        }
        }
        else if (state == LITERAL_CHAR) {
        // In literal mode, we also perform special replacements for \n and \r sequences
        if (*stringP) {
            if (*stringP == 'n') {
            *stringP = '\n';
            }
            else if (*stringP == 'r') {
            *stringP = '\r';
            }
            stringP++;
            // Literals are only recognized within a string.  Therefore, go back to the normal un-quoted state:
            state = QUOTED_TEXT;
        }
        }
        else {
        // illegal state.  Should never happen.  Abort further processing:
        stringP = NULL;
        token = NULL;
        }
    }

    // According to the rules, if no separator was found, indicate that by setting the restOfString to NULL.
    // Otherwise, the remainder of the string starts with everything after the separator
    *theString = (separatorFound) ? stringP : NULL;

    return token;
    }


    // ----------------------------------------------------------------------------------
    // Decompose an argument of the form argname[=[value]].
    // Note that whitespace is not allowed around the '=' character!
    //
    // Returns the value (if any) as a string, and modifies the originalArg to be just the argument name.
    // Examples:
    // 1) For args of the form "foo=bar":
    //      originalArg becomes "foo"
    //      return value is "bar"
    // 2) For args of the form "foo=":
    //      originalArg becomes "foo"
    //      return value is ""
    // 3) For args of the form "foo":
    //      originalArg becomes "foo"
    //      return value is nullptr
    char* Shell::decomposeArg(char* originalArg)
    {
        if (!originalArg) {
            return 0;
        }

        char* argValue = strchr(originalArg, '=');
        if (argValue) {
            *argValue++ = 0;
        }

        return argValue;
}


// ----------------------------------------------------------------------------------
void Shell::cmd_touch(char* argList)
{
    int32_t err;
    lfs_file_t fp;
    char* path;

    // Prime the loop with the first arg
    argList = skipWhite(argList);
    char* arg = strsep(&argList, " ");

    while (arg) {
        char* value = decomposeArg(arg);
        path = arg;
        if (value) {
            printf("unexpected value %s for path %s\n", value, path);
            return;
        }

        if (dbg) printf("%s: pathname=%s\n", __FUNCTION__, path);
        // Open the file argument, creating it if it does not exist
        err = lfs_file_open(lfs, &fp, path, LFS_O_CREAT | LFS_O_RDWR);
        if (err != LFS_ERR_OK) {
            printf("Unable to create path <%s>\n", path, err);
            return;
        }

        lfs_file_close(lfs, &fp);

        // Get the next arg from the argList (if any)
        argList = skipWhite(argList);
        arg = strsep(&argList, " ");
    }
}

// ----------------------------------------------------------------------------------
void Shell::cmd_rm(char* argList)
{
    int32_t err;
    lfs_file_t fp;
    char* path;

    // Prime the loop with the first arg
    argList = skipWhite(argList);
    char* arg = strsep(&argList, " ");

    while (arg) {
        char* value = decomposeArg(arg);
        path = arg;
        if (value) {
            printf("unexpected value %s for path %s\n", value, path);
            return;
        }

        if (dbg) printf("%s: pathname=%s\n", __FUNCTION__, path);

        // Remove the file at the specified path
        err = lfs_remove(lfs, path);
        if (err != LFS_ERR_OK) {
            printf("Unable to remove %s: %s\n", path, lfs_err_decode(err));
            return;
        }

        // Get the next arg from the argList (if any)
        argList = skipWhite(argList);
        arg = strsep(&argList, " ");
    }
}

// ----------------------------------------------------------------------------------
// hex dump of the indicated file
// usage:
//  hd [width=N] path
void Shell::cmd_hd(char* argList)
{
    int32_t err;
    lfs_file_t fp;
    char* path;
    int32_t lineWidth = 16;
    int32_t count;
    uint8_t lineBuf[64];
    uint32_t totalRead=0;

    // arg might be the optional width arg or the pathname
    argList = skipWhite(argList);
    char* arg = strsep(&argList, " ");
    char* value = decomposeArg(arg);
    if ((strcmp(arg, "width") == 0) && (value != nullptr)) {
        // We were passed in an argument of the form "width=[value]"
        // We treat this as an option to modify the width of the line we will output
        sscanf(value, "%d", &lineWidth);
        if ((lineWidth < 1) || (lineWidth>sizeof(lineBuf))) {
            printf("hd: width specifier out of range [1..%d]\n", sizeof(lineBuf));
            return;
        }

        // Skip to the next argument after the "width=value" we just processed.
        // It should be the pathname to dump
        argList = skipWhite(argList);
        arg = strsep(&argList, " ");
    }

    // The arg should be pointing at the path to be dumped.
    // The pathname arg should NOT have a value associated with it:
    value = decomposeArg(arg);
    path = arg;
    if (value) {
        printf("path %s should not have a value associated with it (%s)\n", path, value);
        return;
    }

    if (dbg) printf("%s: pathname=%s\n", __FUNCTION__, path);

    err = lfs_file_open(lfs, &fp, path, LFS_O_RDONLY);
    if (err != LFS_ERR_OK) {
        printf("Unable to open %s: %s\n", path, lfs_err_decode(err));
        return;
    }

    do {
        // Dump the file as hex data
        count = lfs_file_read(lfs, &fp, lineBuf, lineWidth);
        if (count>0) {
            printf("%04X: ", totalRead);

            for (int i=0; i<lineWidth; i++) {
                if (i<count) {
                    printf("%02X ", lineBuf[i]);
                }
                else {
                    printf("   ");
                }
            }
            for (int i=0; i<lineWidth; i++) {
                if (i<count) {
                    if (isprint(lineBuf[i]) && !iscntrl(lineBuf[i])) {
                        printf("%c", lineBuf[i]);
                    }
                    else {
                        printf(".");
                    }
                }
            }
            printf("\n");
            totalRead += count;
        }
    } while (count>0);

    lfs_file_close(lfs, &fp);
}

#if 0
// ----------------------------------------------------------------------------------
// Splits the path argument into a directory and a globbed filename.
// Iterates across every file in the specified directory.
// For filename in the directory that matches the globname,
// call the operation function with the exact full name of the file that matched the globname.
void Shell::iterate(const char* directory, const char* globName, void (*operation)(struct lfs_info* info))
{
    lfs_dir_t dir;
    int32_t lfs_err;
    struct lfs_info info;

    int matchLen;
    int matchIdx;

    char* gp;
    char reName[256];

    lfs_err = lfs_dir_open(lfs, &dir, directory);
    if (lfs_err < 0) {
        printf("unable to open directory %s\n", directory);
    }
    else {
        // Simplistically convert a globname to an equivalent regular expression
        gp = reName;
        *gp++ = '^';
        while (*globName) {
            if (*globName == '*') {
                *gp++ = '.';
                *gp++ = '*';
            }
            else if (*globName == '.') {
                *gp++ = '[';
                *gp++ = '.';
                *gp++ = ']';
            }
            else {
                *gp++ = *globName;
            }
            globName++;
        }
        *gp++ = '$';
        *gp++ = 0;

        re_t pattern = re_compile(globName);

        // Scan through every file in the directory
        do {
            uint32_t size;
            lfs_err = lfs_dir_read(lfs, &dir, &info);
            if (lfs_err > 0) {
                matchIdx = re_matchp(pattern, info.name, &matchLen);
                if (matchIdx == 0) {
                    (*operation)(&info);
                }
            }
        } while (lfs_err > 0);
        lfs_dir_close(lfs, &dir);
    }
}

// ----------------------------------------------------------------------------------
void Shell::operation_ls(struct lfs_info* info)
{
    if (info->type == LFS_TYPE_DIR) {
        // directories do not have a size
        printf("d %8s %s\n", "", info->name);
    }
    else if (info->type == LFS_TYPE_REG) {
        int32_t size = 0;
        // For normal files, get their size
        lfs_file_t file;
        int32_t err = lfs_file_open(lfs, &file, info->name, LFS_O_RDONLY);
        if (err == LFS_ERR_OK) {
            size = lfs_file_size(lfs, &file);
        }
        printf("- %8d %s\n", size, info->name);
        lfs_file_close(lfs, &file);
    }
}

// ----------------------------------------------------------------------------------
void Shell::cmd_ls(char* args)
{
    char* name = decompose(&args, " ");

    if ((!name) || (*name==0)) {
        name = "*";
    }

    iterate(nullptr, name, operation_ls);
}

#else
// ----------------------------------------------------------------------------------
void Shell::cmd_ls(char* args)
{
    lfs_dir_t dir;
    int32_t lfs_err;
    struct lfs_info info;

    const char* path;
    char globname[256];
    char* gp;

    const char* name;
    int matchLen;
    int matchIdx;

    char* complete = decompose(&args, " ");

    re_t pattern = re_compile("^*[.]*[/]");
    matchIdx = re_matchp(pattern, complete, &matchLen);
    if (matchIdx == -1) {
        // there was no path info present
        path = ".";
        name = complete;

    }
    else {

    }

    if ((!name) || (*name == 0)) {
        name = "*";
    }

    // Simplistically convert a globname to an equivalent regular expression
    gp = globname;
    *gp++ = '^';
    while (*name) {
        if (*name == '*') {
            *gp++ = '.';
            *gp++ = '*';
        }
        else if (*name == '.') {
            *gp++ = '[';
            *gp++ = '.';
            *gp++ = ']';
        }
        else {
            *gp++ = *name;
        }
        name++;
    }
    *gp++ = '$';
    *gp++ = 0;

    if (dbg) printf("%s: globname <%s>\n", __FUNCTION__, globname);
    pattern = re_compile(globname);

    lfs_err = lfs_dir_open(lfs, &dir, path);
    if (lfs_err < 0) {
        printf("unable to open directory %s\n", path);
    }
    else {
        // Scan through every file in the directory
        do {
            uint32_t size;
            lfs_err = lfs_dir_read(lfs, &dir, &info);
            if (lfs_err > 0) {
                matchIdx = re_matchp(pattern, info.name, &matchLen);
                if ((matchIdx==0) && (matchLen == strlen(info.name))) {
                    if (info.type == LFS_TYPE_DIR) {
                        // directories do not have a size
                        printf("d %8s %s\n", "", info.name);
                    }
                    else if (info.type == LFS_TYPE_REG) {
                        size = 0;
                        // For normal files, get their size
                        lfs_file_t file;
                        int32_t err = lfs_file_open(lfs, &file, info.name, LFS_O_RDONLY);
                        if (err == LFS_ERR_OK) {
                            size = lfs_file_size(lfs, &file);
                        }
                        printf("- %8d %s\n", size, info.name);
                        lfs_file_close(lfs, &file);
                    }
                }
            }
        } while (lfs_err > 0);
        lfs_dir_close(lfs, &dir);
    }
}
#endif

#if 0
// ----------------------------------------------------------------------------------
void Shell::cmd_rm(char* args)
{
    lfs_dir_t dir;
    int32_t lfs_err;
    struct lfs_info info;

    const char* path;
    char globname[256];
    char* gp;

    const char* name;
    int matchLen;
    int matchIdx;

    char* complete = decompose(&args, " ");

    re_t pattern = re_compile("^*[.]*[/]");
    matchIdx = re_matchp(pattern, complete, &matchLen);
    if (matchIdx == -1) {
        // there was no path info present
        path = ".";
        name = complete;

    }
    else {

    }

    if ((!name) || (*name == 0)) {
        printf("rm: filename expected\n");
    }

    // Simplistically convert a globname to an equivalent regular expression
    gp = globname;
    *gp++ = '^';
    while (*name) {
        if (*name == '*') {
            *gp++ = '.';
            *gp++ = '*';
        }
        else if (*name == '.') {
            *gp++ = '[';
            *gp++ = '.';
            *gp++ = ']';
        }
        else {
            *gp++ = *name;
        }
        name++;
    }
    *gp++ = '$';
    *gp++ = 0;

    if (dbg) printf("%s: globname <%s>\n", __FUNCTION__, globname);
    pattern = re_compile(globname);

    lfs_err = lfs_dir_open(lfs, &dir, path);
    if (lfs_err < 0) {
        printf("unable to open directory %s\n", path);
    }
    else {
        // Scan through every file in the directory
        do {
            uint32_t size;
            lfs_err = lfs_dir_read(lfs, &dir, &info);
            if (lfs_err > 0) {
                matchIdx = re_matchp(pattern, info.name, &matchLen);
                if ((matchIdx==0) && (matchLen == strlen(info.name))) {
                    if (info.type == LFS_TYPE_DIR) {
                        // directories do not have a size
                        printf("d %8s %s\n", "", info.name);
                    }
                    else if (info.type == LFS_TYPE_REG) {
                        size = 0;
                        // For normal files, get their size
                        lfs_file_t file;
                        int32_t err = lfs_file_open(lfs, &file, info.name, LFS_O_RDONLY);
                        if (err == LFS_ERR_OK) {
                            size = lfs_file_size(lfs, &file);
                        }
                        printf("- %8d %s\n", size, info.name);
                        lfs_file_close(lfs, &file);
                    }
                }
            }
        } while (lfs_err > 0);
        lfs_dir_close(lfs, &dir);
    }
}
#endif

// ----------------------------------------------------------------------------------
void Shell::cmd_pwd(char* args)
{
    printf("%s\n", cwd);
}

// ----------------------------------------------------------------------------------
void Shell::shell_task()
{
    bool done;
    char* cmdP;
    const char* lastCharP = &cmdBuf[sizeof(cmdBuf)]-1;
    const char* promptString = "$ ";

    while (1) {
        printf("%s%s", cwd, promptString);
        if (fgets(cmdBuf, sizeof(cmdBuf), stdin)) {
            char* p = strrchr(cmdBuf, '\n');
            if (p) {
                *p = 0;

                args = cmdBuf;
                //if (dbg) printf("processing <%s>\n", cmdBuf);
                cmd = decompose(&args, " ");

                if (cmd) {
                    if ((strcmp(cmd, "ls") == 0) || (strcmp(cmd, "ll") == 0)) {
                        cmd_ls(args);
                    }
                    else if (strcmp(cmd, "touch") == 0) {
                        cmd_touch(args);
                    }
                    else if (strcmp(cmd, "rm") == 0) {
                        cmd_rm(args);
                    }
                    else if (strcmp(cmd, "hd") == 0) {
                        cmd_hd(args);
                    }
                    else if (strcmp(cmd, "pwd") == 0) {
                        cmd_pwd(args);
                    }
                    #if 0
                    else if (strcmp(cmd, "cp") == 0) {
                        printf("cp!\n");
                    }
                    else if (strcmp(cmd, "mv") == 0) {
                        printf("mv!\n");
                    }
                    #endif
                    else if (strcmp(cmd, "") == 0) {
                    }
                    else {
                        printf("Unknown cmd: %s\n", cmd);
                    }
                }
            }
        }
    }
}