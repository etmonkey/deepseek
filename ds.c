#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include "cjson/cJSON.h"

#define INPUT_SIZE 1000

struct Memory {
    char* data;         // 返回的数据块
    char* reply;        // 回答的字符串
    char** chat_arr;    // 对话历史
    int think_start_flag;   // 思考数据块开始标志
    int think_end_flag;     // 思考数据块结束标志
    size_t size;
    size_t reply_size;
    size_t chat_arr_size;
};

void rstrip(char *str) {
    int end = strlen(str) - 1;

    // 去掉末尾空格
    while (isspace((unsigned char)str[end])) {
        end--;
    }

    str[end+1] = '\0';
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
                // cJSON_Delete(root);
            }
            free(json_str);
            start_idx = end_idx + 1;
        }
    }

    return total_size;
}

/**
 * 调用本地引擎
 */
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

/**
 * 调用字节引擎
 */
int ask_volc(cJSON *msg_jarr, cJSON* config, struct Memory* pmem) {
    CURL* curl;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl==NULL) {
        return 1;
    }
    char* api_key = NULL;
    if (cJSON_HasObjectItem(config, "api_key")) {
        api_key = cJSON_GetObjectItem(config, "api_key")->valuestring;
    } else {
        perror("no api key found!");
        exit(1);
    }
    char* str_auth = (char*) malloc(sizeof(char)*(strlen(api_key) + 23));
    sprintf(str_auth, "Authorization: Bearer %s", api_key);

    char* model_name = NULL;
    if (cJSON_HasObjectItem(config, "model")) {
        model_name = cJSON_GetObjectItem(config, "model")->valuestring;
    } else {
        perror("no model found!");
        exit(1);
    }

    char* model = NULL;
    if (cJSON_HasObjectItem(config, model_name)) {
        model = cJSON_GetObjectItem(config, model_name)->valuestring;
    } else {
        fprintf(stderr, "no model named %s found!\n", model_name);
        exit(1);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddBoolToObject(root, "stream", 1);
    cJSON_AddItemToObject(root, "messages", msg_jarr);
    char *post_fields = cJSON_Print(root);
    // printf("post_fields:%s\n", post_fields);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, str_auth);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, "https://ark.cn-beijing.volces.com/api/v3/chat/completions");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_stream_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)pmem);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return 1;
    }

    cJSON_Delete(root);
    free(post_fields);
    free(str_auth);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}

/**
 * 展示帮助信息
 */
void show_help() {
    printf("usage: ds <args> <question>\n");
    printf("args: \n");
    printf("\t-l: use local deepseek model\n");
    printf("\t-n: use online deepseek model(default)\n");
    printf("\t-p: add prompt\n");
    printf("\t-q: add to question\n");
    printf("\t-c: chat mode\n");
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
    return json;
}

int ask_for_chat(cJSON* config, char* final_msg, struct Memory mem) {
    char* str_prompt = NULL;
    if (cJSON_HasObjectItem(config, "prompt")) {
        str_prompt = cJSON_GetObjectItem(config, "prompt")->valuestring;
    }
    while (1)
    {
        cJSON* msg_jarr = cJSON_CreateArray();
        cJSON* prompt_nested_object = NULL;
        cJSON* final_nested_object = NULL;
        if(str_prompt) {
            prompt_nested_object = cJSON_CreateObject();
            cJSON_AddStringToObject(prompt_nested_object, "role", "system");
            cJSON_AddStringToObject(prompt_nested_object, "content", str_prompt);
            cJSON_AddItemToArray(msg_jarr, prompt_nested_object);
        }
        cJSON** nested_objects = (cJSON**)malloc(sizeof(cJSON*)*mem.chat_arr_size);
        for(int i=0; i<mem.chat_arr_size; i+=2) {
            nested_objects[i] = cJSON_CreateObject();
            cJSON_AddStringToObject(nested_objects[i], "role", "user");
            cJSON_AddStringToObject(nested_objects[i], "content", mem.chat_arr[i]);
            cJSON_AddItemToArray(msg_jarr, nested_objects[i]);
            nested_objects[i+1] = cJSON_CreateObject();
            cJSON_AddStringToObject(nested_objects[i+1], "role", "assistant");
            cJSON_AddStringToObject(nested_objects[i+1], "content", mem.chat_arr[i+1]);
            cJSON_AddItemToArray(msg_jarr, nested_objects[i+1]);
        }
        final_nested_object = cJSON_CreateObject();
        cJSON_AddStringToObject(final_nested_object, "role", "user");
        cJSON_AddStringToObject(final_nested_object, "content", final_msg);
        cJSON_AddItemToArray(msg_jarr, final_nested_object);
        
        char* model_name = cJSON_GetObjectItem(config, "model")->valuestring;
        if(strcmp(model_name, "r1")==0) {
            mem.think_start_flag = mem.think_end_flag = 1;
        } else {
            mem.think_start_flag = mem.think_end_flag = 0;
        }
        ask_volc(msg_jarr, config, &mem);

        char** chat_arr_temp = (char**)realloc(mem.chat_arr, sizeof(char*)*(mem.chat_arr_size+2));
        if(chat_arr_temp) {
            mem.chat_arr = chat_arr_temp;
            char* final_msg_temp = (char*) malloc(sizeof(char)*(strlen(final_msg)+1));
            char* reply_temp = (char*) malloc(sizeof(char)*(strlen(mem.reply)+1));
            if(final_msg_temp && reply_temp) {
                strncpy(final_msg_temp, final_msg, strlen(final_msg));
                final_msg_temp[strlen(final_msg)] = '\0';
                strncpy(reply_temp, mem.reply, strlen(mem.reply));
                reply_temp[strlen(mem.reply)] = '\0';
                free(final_msg);
                mem.reply_size = 0;
            } else {
                perror("allocating memory error!");
                exit(1);
            }
            mem.chat_arr[mem.chat_arr_size] = final_msg_temp;
            mem.chat_arr[mem.chat_arr_size+1] = reply_temp;
            mem.chat_arr_size += 2;
        } else {
            perror("allocating chat array memory failed");
            exit(1);
        }
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
            return 0;
        }
        final_msg = (char*)malloc(sizeof(char)*(strlen(buffer)+1));
        sprintf(final_msg, "%s", buffer);
        free(buffer);
    }
    return 0;
}

int ask_one_shot(cJSON* config, char* final_msg, struct Memory mem) {
    char* str_prompt = NULL;
    if (cJSON_HasObjectItem(config, "prompt")) {
        str_prompt = cJSON_GetObjectItem(config, "prompt")->valuestring;
    }
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

    char* model_name = cJSON_GetObjectItem(config, "model")->valuestring;
    if(strcmp(model_name, "r1")==0) {
        mem.think_start_flag = mem.think_end_flag = 1;
    } else {
        mem.think_start_flag = mem.think_end_flag = 0;
    }
    ask_volc(msg_jarr, config, &mem);
    return 0;
}

int main(int argc, char **argv)
{
    // 获取参数
    int opt;
    int local_flag = 0;
    int volc_flag = 1;
    int chat_flag = 0;
    int help_flag = 0;
    cJSON* config = read_config();
    char* question = NULL;
    while ((opt = getopt(argc, argv, "hclm:p:q:")) != -1) {
        switch(opt) {
            case 'l':
                local_flag = 1;
                volc_flag = 0;
                break;
            case 'm':
                local_flag = 0;
                volc_flag = 1;
                cJSON* model = cJSON_GetObjectItem(config, "model");
                if (model) {
                    cJSON_SetValuestring(model, strdup(optarg)); // 修改 name 的值
                } else {
                    cJSON_AddStringToObject(config, "model", strdup(optarg)); // 如果键不存在，添加键值对
                }
                break;
            case 'c':
                chat_flag = 1;
                break;
            case 'p':
                cJSON* prompt = cJSON_GetObjectItem(config, "prompt");
                if (prompt) {
                    cJSON_SetValuestring(prompt, strdup(optarg)); // 修改 name 的值
                } else {
                    cJSON_AddStringToObject(config, "prompt", strdup(optarg)); // 如果键不存在，添加键值对
                }
                break;
            case 'q':
                question = strdup(optarg);
                break;
            case 'h':
                help_flag = 1;
                break;
            case '?':
                fprintf(stderr, "未知选项或缺少参数: -%c\n", optopt);
                return 1;
        }
    }
    char* msg = NULL;
    struct stat statbuf;
    const int buffer_size = 1024;
    char buffer[buffer_size];
    if(fstat(STDIN_FILENO, &statbuf)==0 && S_ISFIFO(statbuf.st_mode)) { //从管道获得参数
        int i = 1;
        while(fgets(buffer, buffer_size, stdin) != NULL) {
            rstrip(buffer);
            char* temp = (char*)realloc(msg, sizeof(char)*buffer_size*i);
            if (temp) {
                msg = temp;
                strncat(msg, buffer, buffer_size*i);
            } else {
                free(msg);  // 失败时释放旧内存
                return 1;
            }
            i++;
        }
        // printf("%s\n", msg);
    } else {    // 从标准输入获得参数
        if(optind<argc) {
            for(int i=optind; i<argc; i++) {
                char* msg_temp = NULL;
                if(msg==NULL) {
                    msg = "";
                }
                msg_temp = (char*)malloc(sizeof(char)*(strlen(msg)+1));
                if(msg_temp) {
                    strcpy(msg_temp, msg);
                } else {
                    return 1;
                }

                msg = (char*)malloc(sizeof(char)*(strlen(msg_temp)+strlen(argv[i])+2));
                if (msg) {
                    sprintf(msg, "%s %s", msg_temp, argv[i]);
                    free(msg_temp);
                } else {  // 失败时释放旧内存
                    free(msg);
                    free(msg_temp);
                    return 1;
                }
            }
        }
    }
    char* final_msg = NULL;
    if(question) {
        final_msg = (char*) malloc(sizeof(char)*(strlen(question)+strlen(msg)+2));
        sprintf(final_msg, "%s:%s", question, msg);
    } else {
        final_msg = msg;
    }
    
    // 获取deepseek返回值
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
    } else if(local_flag) {
        ask_local(final_msg, &mem);
    } else if(volc_flag) {
        if(chat_flag){
            ask_for_chat(config, final_msg, mem);
        } else {
            ask_one_shot(config, final_msg, mem);
        }
    }
    return 0;
}
