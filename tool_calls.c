#include "tool_calls.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>

char* get_cwd() {
    char *cwd = NULL;
    
    cwd = getcwd(NULL, 0);
    if (cwd != NULL) {
        return cwd;
    }
    return NULL;
}

char* list_dir(const char* dir_path) {
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        perror("无法打开目录");
        return NULL;
    }
    
    char* res = (char*)malloc(sizeof(char));
    res[0] = '\0';
    struct dirent *entry;
    // 逐个读取目录项
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".")==0 || strcmp(entry->d_name, "..")==0) continue;

        char* temp = (char*)malloc(strlen(res)+strlen(entry->d_name)+2);
        if(temp==NULL) {
            perror("allocating memory failed!");
            return NULL;
        }
        if(strlen(res)==0) {
            snprintf(temp, strlen(entry->d_name)+2, "%s", entry->d_name);
        } else {
            snprintf(temp, strlen(res)+strlen(entry->d_name)+2, "%s\n%s", res, entry->d_name);
        }
        free(res);
        res = temp;
    }
    
    closedir(dir);
    return res;
}
