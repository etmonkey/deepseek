#ifndef GLOBAL_VARS_H
#define GLOBAL_VARS_H

struct Memory {
    char* data;         // 返回的数据块
    char* reply;        // 回答的字符串
    cJSON* msg_arr;    // 对话历史
    int think_start_flag;   // 思考数据块开始标志
    int think_end_flag;     // 思考数据块结束标志
    int direct_chat_flag;   // 是否直接进入对话模式
    size_t size;            // 返回的数据块长度
    size_t reply_size;      // 回答的字符串长度
    size_t msg_arr_size;    // 对话长度
    struct ToolCallManager *tc_mgr; //tool calls管理器
};

#endif