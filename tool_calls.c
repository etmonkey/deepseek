#include "tool_calls.h"

/**
 * 获取当前工作目录
 */
char* get_cwd() {
    char *cwd = NULL;

    cwd = getcwd(NULL, 0);
    if (cwd != NULL) {
        return cwd;
    }
    return NULL;
}

/**
 * 列出目录下所有文件
 */
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

void print_tool_call_mgr(const struct ToolCallManager *mgr) {
    printf("tool call manager: call_count=%d, capacity=%d\n", mgr->call_count, mgr->capacity);
    for (int i = 0; i < mgr->call_count; i++) {
        printf("calls: {index=%d, id=%s, name=%s, arguments=%s, result=%s}\n", 
            mgr->calls[i].index, mgr->calls[i].id, mgr->calls[i].name, mgr->calls[i].arguments, mgr->calls[i].result);
    }
}

/**
 * 初始化
 */
void tool_call_manager_init(struct ToolCallManager *mgr) {
    mgr->calls = NULL;
    mgr->call_count = 0;
    mgr->capacity = 0;
    mgr->all_complete = 0;
}

/**
 * 释放资源
 */
void tool_call_manager_free(struct ToolCallManager *mgr) {
    for (int i = 0; i < mgr->call_count; i++) {
        free(mgr->calls[i].id);
        free(mgr->calls[i].name);
        free(mgr->calls[i].arguments);
        free(mgr->calls[i].result);
    }
    free(mgr->calls);
    mgr->calls = NULL;
    mgr->call_count = 0;
    mgr->capacity = 0;
}

/**
 * 根据索引查找或创建工具上下文
 */
struct ToolCallContext* find_or_create_tool(struct ToolCallManager *mgr, int index) {
    // 先查找是否已存在
    for (int i = 0; i < mgr->call_count; i++) {
        if (mgr->calls[i].index == index) {
            return &mgr->calls[i];
        }
    }
    
    // 不存在则创建新的
    if (mgr->call_count >= mgr->capacity) {
        mgr->capacity = (mgr->capacity == 0) ? 4 : mgr->capacity * 2;
        mgr->calls = realloc(mgr->calls, mgr->capacity * sizeof(struct ToolCallContext));
        if (!mgr->calls) {
            perror("allocating ToolCallContext failed!");
            return NULL;
        }
    }
    
    // 初始化新工具上下文
    struct ToolCallContext *ctx = &mgr->calls[mgr->call_count];
    ctx->index = index;
    ctx->id = NULL;
    ctx->name = NULL;
    ctx->arguments = malloc(1);
    ctx->arguments[0] = '\0';
    ctx->args_size = 0;
    ctx->is_complete = 0;
    mgr->call_count++;
    
    return ctx;
}

/**
 * 处理每个流式数据块中的tool_calls
 */ 
void process_tool_calls(struct ToolCallManager *mgr, cJSON *tool_calls) {
    int array_size = cJSON_GetArraySize(tool_calls);

    for (int i = 0; i < array_size; i++) {
        cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
        if (!tool_call) continue;

        // 获取索引
        cJSON *index_json = cJSON_GetObjectItem(tool_call, "index");
        if (!index_json || !cJSON_IsNumber(index_json)) {
            continue;
        }
        int index = index_json->valueint;

        // 查找或创建上下文
        struct ToolCallContext *ctx = find_or_create_tool(mgr, index);
        if (!ctx) continue;

        // 检查是否有function对象
        cJSON *function = cJSON_GetObjectItem(tool_call, "function");
        if (!function) continue;

        // 处理ID（只在第一次设置）
        cJSON *id_json = cJSON_GetObjectItem(tool_call, "id");
        if (id_json && cJSON_IsString(id_json) && id_json->valuestring && 
            strlen(id_json->valuestring) > 0 && ctx->id == NULL) {
            ctx->id = strdup(id_json->valuestring);
        }

        // 处理函数名（只在第一次设置）
        cJSON *name_json = cJSON_GetObjectItem(function, "name");
        if (name_json && cJSON_IsString(name_json) && name_json->valuestring &&
            strlen(name_json->valuestring) > 0 && ctx->name == NULL) {
            ctx->name = strdup(name_json->valuestring);
        }

        // 处理参数（需要累积）
        cJSON *args_json = cJSON_GetObjectItem(function, "arguments");
        if (args_json && cJSON_IsString(args_json) && args_json->valuestring) {
            char *args_chunk = args_json->valuestring;
            size_t chunk_len = strlen(args_chunk);

            // 追加到现有参数
            char *new_args = realloc(ctx->arguments, ctx->args_size + chunk_len + 1);
            if (new_args) {
                ctx->arguments = new_args;
                memcpy(ctx->arguments + ctx->args_size, args_chunk, chunk_len);
                ctx->args_size += chunk_len;
                ctx->arguments[ctx->args_size] = '\0';
            }
        }
    }
}

/**
 * 执行所有工具并构造工具结果消息
 */
void execute_all_tools(struct ToolCallManager *mgr) {
    // 为每个工具调用执行对应的函数
    for (int i = 0; i < mgr->call_count; i++) {
        struct ToolCallContext *ctx = &(mgr->calls[i]);
        
        // 根据函数名执行不同的操作
        char *result = NULL;
        if (strcmp(ctx->name, "list_dir") == 0) {
            // 解析参数并调用天气函数
            cJSON *args = cJSON_Parse(ctx->arguments);
            cJSON *dir_path = cJSON_GetObjectItem(args, "dir_path");
            if (dir_path && cJSON_IsString(dir_path)) {
                result = list_dir(dir_path->valuestring);
            }
            cJSON_Delete(args);
        } else if (strcmp(ctx->name, "get_cwd") == 0) {
            result = get_cwd();
        } else {
            result = strdup("未知工具");
        }
        
        // 构造工具结果消息
        ctx->is_complete = 1;
        ctx->result = result;
    }
    mgr->all_complete = 1;
}

/**
 * 构造role: tool的消息
 */
void add_tool_call_to_message(struct Memory* pmem, cJSON* data_root) {
    struct ToolCallManager *mgr = pmem->tc_mgr;
    cJSON* messages = cJSON_GetObjectItem(data_root, "messages");
    if (messages == NULL || !cJSON_IsArray(messages)) return;

    cJSON* assistant = cJSON_CreateObject();
    cJSON_AddStringToObject(assistant, "role", "assistant");
    // 写入回答
    if(pmem->reply && strlen(pmem->reply)>0) {
        cJSON_AddStringToObject(assistant, "content", pmem->reply);
    } else {
        cJSON_AddNullToObject(assistant, "content");
    }
    // 写入tool calls
    cJSON* tool_calls = cJSON_CreateArray();
    for(int i=0; i<mgr->call_count; i++) {
        struct ToolCallContext *ctx = &((mgr->calls)[i]);
        char* tool_call_id = strdup(ctx->id);
        char* name = strdup(ctx->name);
        char* arguments = strdup(ctx->arguments);
        cJSON* call = cJSON_CreateObject();
        cJSON_AddStringToObject(call, "id", tool_call_id);
        cJSON_AddStringToObject(call, "type", "function");
        cJSON* function = cJSON_CreateObject();
        cJSON_AddStringToObject(function, "name", name);
        cJSON_AddStringToObject(function, "arguments", arguments);
        cJSON_AddItemToObject(call, "function", function);
        cJSON_AddItemToArray(tool_calls, call);
    }
    cJSON_AddItemToObject(assistant, "tool_calls", tool_calls);
    cJSON_AddItemToArray(messages, assistant);
    cJSON* dup_assistant = cJSON_Duplicate(assistant, 1);
    cJSON_AddItemToArray(pmem->msg_arr, dup_assistant);
    (pmem->msg_arr_size)++;

    for(int i=0; i<mgr->call_count; i++) {
        struct ToolCallContext *ctx = &((mgr->calls)[i]);
        char* tool_call_id = strdup(ctx->id);
        char* content = strdup(ctx->result);
        cJSON* tool = cJSON_CreateObject();
        if(tool==NULL) return;
        cJSON_AddStringToObject(tool, "role", "tool");
        cJSON_AddStringToObject(tool, "tool_call_id", tool_call_id);
        cJSON_AddStringToObject(tool, "content", content);
        cJSON_AddItemToArray(messages, tool);
        cJSON* dup_tool = cJSON_Duplicate(tool, 1);
        cJSON_AddItemToArray(pmem->msg_arr, dup_tool);
        (pmem->msg_arr_size)++;
    }
}