#ifndef TOOL_CALLS_H
#define TOOL_CALLS_H

#include <stddef.h>
#include "cjson/cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>

#define MAX_TOOL_CALL_ITER 5

// 单个工具调用上下文
struct ToolCallContext {
    int index;                  // 工具调用的索引
    char *id;                   // 工具调用ID
    char *name;                 // 函数名
    char *arguments;            // 累积的参数JSON
    size_t args_size;           // 参数缓冲区大小
    int is_complete;            // 是否已接收完整
    char* result;               // 工具执行结果
};

// 管理多个工具调用的状态
struct ToolCallManager {
    struct ToolCallContext *calls;  // 工具调用数组
    int call_count;                 // 当前工具数量
    int capacity;                   // 数组容量
    int all_complete;               // 是否全部接收完成
};

void print_tool_call_mgr(const struct ToolCallManager *);
void tool_call_manager_init(struct ToolCallManager*);
void tool_call_manager_free(struct ToolCallManager*);
void process_tool_calls(struct ToolCallManager *, cJSON *);
void execute_all_tools(struct ToolCallManager *);
void add_tool_call_to_message(struct ToolCallManager *, cJSON*, cJSON*, size_t*);

#endif