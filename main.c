#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "curl/curl.h"
#include "cjson/cJSON.h"

// 用于接收 HTTP 响应数据的结构体
struct MemoryStruct {
    char *memory;
    size_t size;
};

// libcurl 的回调函数，用于将网络流数据写入内存
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        printf("内存不足，realloc 失败\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int main(void) {
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1); 
    chunk.size = 0; 

    // 1. 从环境变量获取 API Key (推荐做法，避免硬编码)
    char *api_key = getenv("GEMINI_API_KEY");
    if (!api_key) {
        fprintf(stderr, "错误: 请先设置环境变量 GEMINI_API_KEY\n");
        free(chunk.memory);
        return 1;
    }

    // 2. 拼接完整的 API URL (使用主力推荐的 gemini-2.5-flash)
    char url[512];
    snprintf(url, sizeof(url), 
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=%s", 
             api_key);

    // 3. 使用 cJSON 构建请求体 (Payload)
    // 目标结构: {"contents": [{"parts": [{"text": "你的问题"}]}]}
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *content_item = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();
    cJSON *part_item = cJSON_CreateObject();

    cJSON_AddStringToObject(part_item, "text", "帮我拆解一下cloude是怎么实现agent的，我要模拟一下");
    cJSON_AddItemToArray(parts, part_item);
    cJSON_AddItemToObject(content_item, "parts", parts);
    cJSON_AddItemToArray(contents, content_item);
    cJSON_AddItemToObject(root, "contents", contents);

    char *json_post_data = cJSON_Print(root);

    // 4. 初始化 curl 会话
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (curl_handle) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // 设置 curl 选项
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_post_data);
        
        // 设置回调函数来接收响应
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        // 执行请求
        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() 失败: %s\n", curl_easy_strerror(res));
        } else {
            // 5. 解析返回的 JSON 数据
            cJSON *response_json = cJSON_Parse(chunk.memory);
            if (response_json) {
                // 逐层解析获取文本: candidates[0].content.parts[0].text
                cJSON *candidates = cJSON_GetObjectItemCaseSensitive(response_json, "candidates");
                cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
                cJSON *content = cJSON_GetObjectItemCaseSensitive(candidate, "content");
                cJSON *res_parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
                cJSON *res_part = cJSON_GetArrayItem(res_parts, 0);
                cJSON *text = cJSON_GetObjectItemCaseSensitive(res_part, "text");

                if (cJSON_IsString(text) && (text->valuestring != NULL)) {
                    printf("\n--- Gemini 回复 ---\n%s\n-------------------\n", text->valuestring);
                } else {
                    printf("解析文本失败，原始响应:\n%s\n", chunk.memory);
                }
                cJSON_Delete(response_json);
            } else {
                printf("JSON 解析失败。\n");
            }
        }

        // 清理
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
    }

    // 释放动态分配的内存
    free(json_post_data);
    cJSON_Delete(root);
    free(chunk.memory);
    curl_global_cleanup();

    return 0;
}
