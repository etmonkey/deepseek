#include "tool_calls.h"
#include <stdlib.h>
#include <unistd.h>

char* get_cwd() {
    char *cwd = NULL;
    
    cwd = getcwd(NULL, 0);
    if (cwd != NULL) {
        return cwd;
    }
    return NULL;
}
