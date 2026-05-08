#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include "cjson/cJSON.h"

#define MAX_CONTEXT_SIZE 1024*1024
#define MAX_MSG_FACTOR 0.7

void parse_config(const cJSON*, cJSON*, char*, char*, char*, char*);

struct Memory {
    char* data;         // 返回的数据块
    char* reply;        // 回答的字符串
    char** chat_arr;    // 对话历史
    int think_start_flag;   // 思考数据块开始标志
    int think_end_flag;     // 思考数据块结束标志
    size_t size;
    size_t reply_size;
    size_t chat_arr_size;   // 对话长度
};

int rstrip(char *str) {
    int end = strlen(str) - 1;

    // 去掉末尾空格
    while (isspace((unsigned char)str[end]) && str[end]!='\n') {
        end--;
    }

    str[end+1] = '\0';
    return end + 1;
}

// 回调函数：将接收到的数据存储到 Memory 结构体中
size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb;
    struct Memory *mem = (struct Memory *)userdata;

    // 重新分配内存
    char *temp = realloc(mem->data, mem->size + total_size + 1);
    if (temp == NULL) {
        printf("Not enough memory\n");
        return 1;
    }

    mem->data = temp;
    memcpy(&(mem->data[mem->size]), ptr, total_size);
    mem->size += total_size;
    mem->data[mem->size] = '\0';  // 确保是 C 字符串

    return total_size;  // 必须返回接收的字节数，否则会终止传输
}

// 流式数据回调函数
static size_t curl_write_stream_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb;
    struct Memory *pmem = (struct Memory *)userdata;

    // 追加数据到缓冲区
    char *temp = realloc(pmem->data, pmem->size + total_size + 1);
    if (temp == NULL) {
        printf("Not enough memory\n");
        return 1;
    }

    pmem->data = temp;
    memcpy(&(pmem->data[pmem->size]), ptr, total_size);
    pmem->size += total_size;
    pmem->data[pmem->size] = '\0';  // 确保是 C 字符串

    // 尝试解析每个数据块
    char* all_line = pmem->data;
    char* line = NULL;
    char* end = NULL;
    // printf("line: %s\n", line);
    static int start_idx = 0;
    static int end_idx = 0;
    while((line = strstr(all_line + start_idx, "data: "))) {
        line += 6; // 跳过 "data: "
        end = strchr(line, '\n');
        if(!end) break;
        start_idx = line - all_line;
        end_idx = end - all_line;
        char* json_str = strndup(all_line + start_idx, end_idx - start_idx);

        if(strcmp(json_str, "[DONE]")==0) {
            printf("\n");
        }
        cJSON *root = cJSON_Parse(json_str);
        if(root) {
            if (cJSON_HasObjectItem(root, "error")) {
                cJSON *error = cJSON_GetObjectItem(root, "error");
                char* error_code = cJSON_GetObjectItem(error, "code")->valuestring;
                char* error_msg = cJSON_GetObjectItem(error, "message")->valuestring;
                printf("error requesting data, error code: %s\n", error_code);
                printf("error message: %s\n", error_msg);
                cJSON_Delete(root);
                return 1;
            }
            if (cJSON_HasObjectItem(root, "choices")) {
                cJSON *choices = cJSON_GetObjectItem(root, "choices");
                int choices_count = cJSON_GetArraySize(choices);
                if(choices && cJSON_IsArray(choices)) {
                    for(int i=0; i<choices_count; i++){
                        cJSON *a_choice = cJSON_GetArrayItem(choices, i);
                        if(a_choice) {
                            cJSON *delta = cJSON_GetObjectItem(a_choice, "delta");
                            if(delta) {
                                cJSON *reasoning_content = cJSON_GetObjectItem(delta, "reasoning_content");
                                cJSON *content = cJSON_GetObjectItem(delta, "content");
                                if(reasoning_content && cJSON_IsString(reasoning_content)) {
                                    if(pmem->think_start_flag) {printf("<think>\n"); pmem->think_start_flag=0;}
                                    printf("%s", reasoning_content->valuestring);
                                    fflush(stdout); // 立即输出思考内容
                                }
                                if(content && cJSON_IsString(content)) {
                                    if(pmem->think_end_flag && strlen(content->valuestring)) {printf("<\\think>\n"); pmem->think_end_flag=0;}
                                    printf("%s", content->valuestring);
                                    fflush(stdout); // 立即输出回答内容
                                    int content_size = strlen(content->valuestring);
                                    char *reply_temp = realloc(pmem->reply, pmem->reply_size + content_size + 1);
                                    if(reply_temp == NULL) {
                                        printf("Not enough memory\n");
                                        return 1;
                                    }
                                    pmem->reply = reply_temp;
                                    memcpy(&(pmem->reply[pmem->reply_size]), content->valuestring, content_size);
                                    pmem->reply_size += content_size;
                                    pmem->reply[pmem->reply_size] = '\0';
                                }
                            }
                            
                        }
                    }
                }
            }
            cJSON_Delete(root);
            start_idx = end_idx + 1;
        }
        free(json_str);
    }

    return total_size;
}

/**
 * 调用本地引擎
 */
/*
int ask_local(char* msg, struct Memory* pmem) {
    CURL* curl;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl==NULL) {
        return 1;
    }
    char* post_fields = (char*) malloc(sizeof(char) * (strlen(msg)+58));
    sprintf(post_fields, "{\"model\": \"deepseek-r1:14b\",\"prompt\": \"%s\",\"stream\": false}", msg);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:11434/api/generate");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)pmem);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %sn", curl_easy_strerror(res));
        exit(1);
    }
    free(post_fields);
    // 解析返回值
    cJSON* json = cJSON_Parse(pmem->data);
    if(json == NULL) {
        fprintf(stderr, "解析返回值失败\n");
        return 1;
    }
    cJSON* cjson_response = cJSON_GetObjectItem(json, "response");
    printf("%s\n", cjson_response->valuestring);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
*/

/**
 * 调用deepseek引擎
 */
int ask_online(struct Memory* pmem, cJSON* data_root, char* base_url, char* api_key, char* model_choice) {
    CURL* curl;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl==NULL) {
        return 1;
    }

    char* str_auth = (char*) malloc(sizeof(char)*(strlen(api_key) + 23));
    sprintf(str_auth, "Authorization: Bearer %s", api_key);

    char *post_fields = cJSON_Print(data_root);
    // printf("post_fields:%s\n", post_fields);
    
    char* thinking_type = cJSON_GetObjectItem(cJSON_GetObjectItem(data_root, "thinking"), "type")->valuestring;
    if(strcmp(thinking_type, "enabled")==0 || strcmp(model_choice, "r1")==0) {
        pmem->think_start_flag = pmem->think_end_flag = 1;
    } else {
        pmem->think_start_flag = pmem->think_end_flag = 0;
    }

    int is_stream = cJSON_GetObjectItem(data_root, "stream")->valueint;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, str_auth);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, base_url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    is_stream==1 ? curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_stream_cb) :
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)pmem);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        fprintf(stderr, "cURL request failed: %s\n", curl_easy_strerror(res));
        return 1;
    }

    free(post_fields);
    free(str_auth);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}

/**
 * 对话式询问
 */
int ask_for_chat(cJSON* config, char* final_msg, struct Memory* pmem) {
    cJSON* data_root = cJSON_CreateObject();
    char* base_url = NULL;
    char* api_key = NULL;
    char* str_prompt = NULL;
    char* model_choice = NULL;
    parse_config(config, data_root, base_url, api_key, str_prompt, model_choice);

    cJSON* msg_jarr = cJSON_CreateArray();
    cJSON* prompt_nested_object = NULL;
    if(str_prompt) {
        prompt_nested_object = cJSON_CreateObject();
        cJSON_AddStringToObject(prompt_nested_object, "role", "system");
        cJSON_AddStringToObject(prompt_nested_object, "content", str_prompt);
        cJSON_AddItemToArray(msg_jarr, prompt_nested_object);
    }
    // 值为1时代表不从命令行参数获取提问，直接进入对话
    int direct_chat_flag = strlen(final_msg)==0;
    
    while (1)
    {
        if(direct_chat_flag==1) {
            direct_chat_flag = 0;
            goto chat_prompt;
        }
        char* dup_msg = strndup(final_msg, MAX_CONTEXT_SIZE*MAX_MSG_FACTOR);
        if(dup_msg) {
            cJSON* msg_nested_object = cJSON_CreateObject();
            cJSON_AddStringToObject(msg_nested_object, "role", "user");
            cJSON_AddStringToObject(msg_nested_object, "content", dup_msg);
            cJSON_AddItemToArray(msg_jarr, msg_nested_object);
        } else {
            perror("allocating memory error!");
            exit(1);
        }

        cJSON_AddItemToObject(data_root, "messages", msg_jarr);
        // 调用deepseek服务
        ask_online(pmem, data_root, base_url, api_key, model_choice);

        char* dup_reply = strndup(pmem->reply, MAX_CONTEXT_SIZE);
        if(dup_reply) {
            cJSON* reply_nested_objects = cJSON_CreateObject();
            cJSON_AddStringToObject(reply_nested_objects, "role", "assistant");
            cJSON_AddStringToObject(reply_nested_objects, "content", dup_reply);
            cJSON_AddItemToArray(msg_jarr, reply_nested_objects);
        } else {
            perror("allocating memory error!");
            exit(1);
        }

        char** chat_arr_temp = (char**)realloc(pmem->chat_arr, sizeof(char*)*(pmem->chat_arr_size+2));
        if(chat_arr_temp) {
            pmem->chat_arr = chat_arr_temp;
            char* final_msg_temp = strndup(final_msg, MAX_CONTEXT_SIZE*MAX_MSG_FACTOR);
            char* reply_temp = strndup(pmem->reply, MAX_CONTEXT_SIZE);
            if(final_msg_temp && reply_temp) {
                free(final_msg);
                pmem->reply_size = 0;
            } else {
                perror("allocating memory error!");
                exit(1);
            }
            pmem->chat_arr[pmem->chat_arr_size] = final_msg_temp;
            pmem->chat_arr[pmem->chat_arr_size+1] = reply_temp;
            pmem->chat_arr_size += 2;
        } else {
            perror("allocating chat array memory failed");
            exit(1);
        }

chat_prompt:
        printf(">");
        if (!isatty(fileno(stdin))) {
            fclose(stdin);
            stdin = fopen("/dev/tty", "r");
            if (stdin == NULL) {
                perror("can not open terminal");
                return 1;
            }
        }
        char* buffer = NULL;
        size_t len = 0;
        ssize_t read = getline(&buffer, &len, stdin); // 读取一行
        if (read != -1) {
            // 去掉换行符（如果存在）
            if (buffer[read - 1] == '\n') {
                buffer[read - 1] = '\0';
            }
        } else {
            perror("read question buffer error!");
            return 1;
        }
        if(strcmp(buffer, "/bye")==0) {
            // free(buffer);
            break;
        }
        
        final_msg = strndup(buffer, MAX_CONTEXT_SIZE*MAX_MSG_FACTOR);
        if(!final_msg) {
            perror("allocating memory error!");
            exit(1);
        }
        free(buffer);
    }

    cJSON_Delete(data_root);
    return 0;
}

/**
 * 单次询问
 */
int ask_one_shot(cJSON* config, char* final_msg, struct Memory* pmem) {
    cJSON* data_root = cJSON_CreateObject();
    char* base_url = NULL;
    char* api_key = NULL;
    char* str_prompt = NULL;
    char* model_choice = NULL;
    parse_config(config, data_root, base_url, api_key, str_prompt, model_choice);

    cJSON *msg_jarr = cJSON_CreateArray();
    cJSON *nested_object = NULL;
    if(str_prompt) {
        nested_object = cJSON_CreateObject();
        cJSON_AddStringToObject(nested_object, "role", "system");
        cJSON_AddStringToObject(nested_object, "content", str_prompt);
        cJSON_AddItemToArray(msg_jarr, nested_object);
    }
    nested_object = cJSON_CreateObject();
    cJSON_AddStringToObject(nested_object, "role", "user");
    cJSON_AddStringToObject(nested_object, "content", final_msg);
    cJSON_AddItemToArray(msg_jarr, nested_object);

    cJSON_AddItemToObject(data_root, "messages", msg_jarr);

    ask_online(pmem, data_root, base_url, api_key, model_choice);

    cJSON_Delete(data_root);
    return 0;
}

/**
 * 将config整理为直接可用的JSON，读取base_url和api_key
 */
void parse_config(const cJSON* config, cJSON* data_root, char* base_url, char* api_key, char* prompt, char* model_choice) {
    char* provider_choice = NULL;
    if (cJSON_HasObjectItem(config, "provider_choice")) {
        provider_choice = cJSON_GetObjectItem(config, "provider_choice")->valuestring;
    } else {
        perror("no provider choice found!");
        exit(1);
    }
    cJSON* provider = NULL;
    if (cJSON_HasObjectItem(config, "provider")) {
        provider = cJSON_GetObjectItem(config, "provider");
    } else {
        perror("no provider found!");
        exit(1);
    }
    cJSON* sel_provider = NULL;
    if (cJSON_HasObjectItem(provider, provider_choice)) {
        sel_provider = cJSON_GetObjectItem(provider, provider_choice);
    } else {
        char* err_msg = (char*) malloc(sizeof(char)*(strlen(provider_choice)+21));
        sprintf(err_msg, "provider %s not found!", provider_choice);
        perror(err_msg);
        free(err_msg);
        exit(1);
    }
    // 获取base_url和api_key
    if (cJSON_HasObjectItem(sel_provider, "base_url")) {
        base_url = strndup(cJSON_GetObjectItem(sel_provider, "base_url")->valuestring, 1024);
        if(!base_url) {
            perror("allocation for base url failed!");
            exit(1);
        }
    } else {
        char* err_msg = (char*) malloc(sizeof(char)*(strlen(provider_choice)+25));
        sprintf(err_msg, "base url for %s not found!", provider_choice);
        perror(err_msg);
        free(err_msg);
        exit(1);
    }
    if (cJSON_HasObjectItem(sel_provider, "api_key")) {
        api_key = strndup(cJSON_GetObjectItem(sel_provider, "api_key")->valuestring, 256);
        if(!api_key) {
            perror("allocation for api key failed!");
            exit(1);
        }
    } else {
        char* err_msg = (char*) malloc(sizeof(char)*(strlen(provider_choice)+24));
        sprintf(err_msg, "api key for %s not found!", provider_choice);
        perror(err_msg);
        free(err_msg);
        exit(1);
    }
    // 构造直接可用的JSON body
    // 获取model名
    cJSON* model = NULL;
    if (cJSON_HasObjectItem(sel_provider, "model")) {
        model = cJSON_GetObjectItem(sel_provider, "model");
    } else {
        char* err_msg = (char*) malloc(sizeof(char)*(strlen(provider_choice)+21));
        sprintf(err_msg, "model in %s not found!", provider_choice);
        perror(err_msg);
        free(err_msg);
        exit(1);
    }
    if (cJSON_HasObjectItem(config, "model_choice")) {
        model_choice = cJSON_GetObjectItem(config, "model_choice")->valuestring;
    } else {
        perror("no model choice found!");
        exit(1);
    }
    char* sel_model = NULL;
    if (cJSON_HasObjectItem(model, model_choice)) {
        sel_model = strndup(cJSON_GetObjectItem(model, model_choice)->valuestring, 256);
    } else {
        perror("no model choice found!");
        exit(1);
    }
    // 获取prompt
    if (cJSON_HasObjectItem(config, "prompt")) {
        prompt = strndup(cJSON_GetObjectItem(config, "prompt")->valuestring, 2048);
    } else {
        prompt = NULL;
    }
    // 获取thinking type
    char* thinking_type = NULL;
    if (cJSON_HasObjectItem(config, "thinking")) {
        thinking_type = strndup(cJSON_GetObjectItem(config, "thinking")->valuestring, 2048);
    } else {
        perror("no thinking type found!");
        exit(1);
    }
    // 获取reasoning effort
    char* reasoning_effort = NULL;
    if (cJSON_HasObjectItem(config, "reasoning_effort")) {
        reasoning_effort = strndup(cJSON_GetObjectItem(config, "reasoning_effort")->valuestring, 2048);
    } else {
        perror("no reasoning effort found!");
        exit(1);
    }
    // 获取temperature
    int temperature = 1;
    if (cJSON_HasObjectItem(config, "temperature")) {
        temperature = cJSON_GetObjectItem(config, "temperature")->valueint;
    } else {
        perror("no temperature found!");
        exit(1);
    }
    // 获取max tokens
    int max_tokens = 0;
    if (cJSON_HasObjectItem(config, "max_tokens")) {
        max_tokens = cJSON_GetObjectItem(config, "max_tokens")->valueint;
    } else {
        perror("no max tokens found!");
        exit(1);
    }
    // 获取stream setting
    int stream = 1;
    if (cJSON_HasObjectItem(config, "stream")) {
        cJSON* stream_item = cJSON_GetObjectItem(config, "stream");
        if(cJSON_IsBool(stream_item)) {
            if(cJSON_IsTrue(stream_item)) {
                stream = 1;
            } else {
                stream = 0;
            }
        }
    } else {
        perror("no stream setting found!");
        exit(1);
    }
    // 获取include_usage
    int include_usage = 0;
    if (cJSON_HasObjectItem(config, "include_usage")) {
        cJSON* include_usage_item = cJSON_GetObjectItem(config, "include_usage");
        if(cJSON_IsBool(include_usage_item)) {
            if(cJSON_IsTrue(include_usage_item)) {
                include_usage = 1;
            } else {
                include_usage = 0;
            }
        }
    } else {
        perror("no include usage found!");
        exit(1);
    }

    // 构造JSON
    cJSON_AddStringToObject(data_root, "model", sel_model);
    cJSON* thinking = cJSON_CreateObject();
    cJSON_AddStringToObject(thinking, "type", thinking_type);
    cJSON_AddItemToObject(data_root, "thinking", thinking);
    cJSON_AddStringToObject(data_root, "reasoning_effort", reasoning_effort);
    cJSON_AddNumberToObject(data_root, "max_tokens", max_tokens);
    cJSON_AddNumberToObject(data_root, "temperature", temperature);
    cJSON_AddBoolToObject(data_root, "stream", stream);
    cJSON* stream_options = cJSON_CreateObject();
    cJSON_AddBoolToObject(stream_options, "include_usage", include_usage);
    cJSON_AddItemToObject(data_root, "stream_options", stream_options);
}

cJSON* read_config() {
    FILE *file = fopen("/etc/deepseek/config.json", "r");
    if(file == NULL) {
        perror("no config file!");
        exit(1);
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 分配内存并读取文件内容
    char *json_data = malloc(file_size + 1);
    if (!json_data) {
        perror("memory allocation failed!");
        fclose(file);
        return NULL;
    }
    fread(json_data, 1, file_size, file);
    json_data[file_size] = '\0'; // 添加字符串终止符

    // 解析 JSON 数据
    cJSON *json = cJSON_Parse(json_data);
    if (!json) {
        printf("resolving json failed: %s\n", cJSON_GetErrorPtr());
        free(json_data);
        exit(1);
    }

    fclose(file);
    free(json_data);
    return json;
}

/**
 * 展示帮助信息
 */
void show_help() {
    printf("usage: ds [args] [question]\n");
    printf("args: \n");
    printf("\t-h: show help\n");
    printf("\t-c: chat mode\n");
    printf("\t-p: add prompt\n");
    printf("\t-q: add to question\n");
    printf("\t-v: choose deepseek provider\n");
    printf("\t-m: choose deepseek model flash(default)/pro/v3/r1\n");
    printf("\t-e: choose reasoning effort\n");
    printf("\t-t: set temperature(0~2), higher for more random result\n");
    printf("\t-o: max output tokens(max 384K)\n");
    printf("\t-k: set thinking mode enabled\n");
    printf("\t-u: set outputing usage\n");
}

int main(int argc, char **argv)
{
    // 获取参数
    int opt;
    int chat_flag = 0;
    int help_flag = 0;
    cJSON* config = read_config();
    char* question = NULL;
    while ((opt = getopt(argc, argv, "hcp:q:v:m:e:t:o:ku")) != -1) {
        switch(opt) {
            case 'h':
                help_flag = 1;
                break;
            case 'c':
                chat_flag = 1;
                break;
            case 'p':
                cJSON* prompt = cJSON_GetObjectItem(config, "prompt");
                if (prompt) {
                    cJSON_SetValuestring(prompt, strdup(optarg)); // 修改 prompt 的值
                } else {
                    cJSON_AddStringToObject(config, "prompt", strdup(optarg)); // 如果键不存在，添加键值对
                }
                break;
            case 'q':
                question = strdup(optarg);
                break;
            case 'V':
                cJSON* provider_choice = cJSON_GetObjectItem(config, "provider_choice");
                if (provider_choice) {
                    cJSON_SetValuestring(provider_choice, strdup(optarg));
                } else {
                    cJSON_AddStringToObject(config, "provider_choice", strdup(optarg));
                }
                break;
            case 'm':
                cJSON* model_choice = cJSON_GetObjectItem(config, "model_choice");
                if (model_choice) {
                    cJSON_SetValuestring(model_choice, strdup(optarg));
                } else {
                    cJSON_AddStringToObject(config, "model_choice", strdup(optarg));
                }
                break;
            case 'e':
                cJSON* reasoning_effort = cJSON_GetObjectItem(config, "reasoning_effort");
                if (reasoning_effort) {
                    cJSON_SetValuestring(reasoning_effort, strdup(optarg));
                } else {
                    cJSON_AddStringToObject(config, "reasoning_effort", strdup(optarg));
                }
                break;
            case 't':
                cJSON* temperature = cJSON_GetObjectItem(config, "temperature");
                if (temperature) {
                    cJSON_SetValuestring(temperature, strdup(optarg));
                } else {
                    cJSON_AddStringToObject(config, "temperature", strdup(optarg));
                }
                break;
            case 'o':
                cJSON* max_tokens = cJSON_GetObjectItem(config, "max_tokens");
                if (max_tokens) {
                    cJSON_SetValuestring(max_tokens, strdup(optarg));
                } else {
                    cJSON_AddStringToObject(config, "max_tokens", strdup(optarg));
                }
                break;
            case 'k':
                cJSON* thinking = cJSON_GetObjectItem(config, "thinking");
                if (thinking) {
                    cJSON_SetValuestring(thinking, "enabled");
                } else {
                    cJSON_AddStringToObject(config, "thinking", "enabled");
                }
                break;
            case 'u':
                cJSON* include_usage = cJSON_GetObjectItem(config, "include_usage");
                if(include_usage) {
                    cJSON_SetNumberValue(include_usage, 1);
                } else {
                    cJSON_AddNumberToObject(config, "include_usage", 1);
                }
                break;
            case '?':
                fprintf(stderr, "未知选项或缺少参数: -%c\n", optopt);
                return 1;
        }
    }
    char* msg = (char*) malloc(sizeof(char)*1);
    *msg = '\0';
    struct stat statbuf;
    if(fstat(STDIN_FILENO, &statbuf)==0 && S_ISFIFO(statbuf.st_mode)) { //从管道获得参数
        const int buffer_size = 1024;
        char buffer[buffer_size];
        int msg_size = 1;
        while(fgets(buffer, buffer_size, stdin) != NULL) {
            msg_size += rstrip(buffer);
            char* temp = (char*)realloc(msg, sizeof(char)*msg_size);
            if (temp) {
                msg = temp;
                strncat(msg, buffer, msg_size);
            } else {
                free(msg);  // 失败时释放旧内存
                return 1;
            }
        }
        // printf("%s\n", msg);
    } else {    // 从标准输入获得参数
        if(optind<argc) {
            int msg_size = 1;
            for(int i=optind; i<argc; i++) {
                msg_size += strlen(argv[i]) + 1;
                char* msg_temp = (char*)realloc(msg, sizeof(char)*msg_size);
                if(msg_temp) {
                    msg = msg_temp;
                    strcat(msg, " ");
                    strncat(msg, argv[i], strlen(argv[i]));
                } else {
                    free(msg);
                    return 1;
                }
            }
        }
    }
    char* final_msg = NULL;
    if(question) {
        final_msg = (char*) malloc(sizeof(char)*(strlen(question)+strlen(msg)+2));
        snprintf(final_msg, MAX_CONTEXT_SIZE*MAX_MSG_FACTOR, "%s:%s", question, msg);
    } else {
        final_msg = strndup(msg, MAX_CONTEXT_SIZE*MAX_MSG_FACTOR);
    }
    free(msg);
    
    // 与deepseek对话
    struct Memory mem;
    mem.data = (char*)malloc(1);  // 初始分配
    mem.size = 0;
    mem.chat_arr = (char**)malloc(sizeof(char*));
    mem.chat_arr_size = 0;
    mem.reply = (char*)malloc(1);
    mem.reply_size = 0;
    mem.think_start_flag = 0;   // 1表示还未进入回答callback
    mem.think_end_flag = 0;
    if(help_flag) {
        show_help();
    } else {
        if(chat_flag){
            ask_for_chat(config, final_msg, &mem);
        } else {
            ask_one_shot(config, final_msg, &mem);
        }
    }
    cJSON_Delete(config);
    free(final_msg);
    free(mem.data);
    free(mem.reply);
    for(int i=0; i<mem.chat_arr_size; i++) {
        free(mem.chat_arr[i]);
    }
    free(mem.chat_arr);
    return 0;
}
