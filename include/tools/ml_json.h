/*************************************************************************
	> File Name: ml_json.h
	> Author: mlxh
	> Mail: mlxh_gto@163.com 
	> Created Time: Sat 27 Jun 2026 11:45:12 PM CST
 ************************************************************************/

#ifndef ML_JSON_H
#define ML_JSON_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "cjson/cJSON.h"

// 1. 一句话生成 Gemini 专属请求体，返回 malloc 分配的字符串，记得用完 free
char * ml_json_make_gemini_req(const char *prompt);

// 2. 一句话安全提取 Gemini 返回的文本，将其拷贝到你指定的缓冲区中（防止 cJSON 内部对象销毁后指针失效）
int ml_json_parse_gemini_res(const char *json_str, char *out_text, size_t out_size);

#endif // ML_JSON_H

#if defined(ML_JSON_IMPLEMENTATION) && !defined(ML_JSON_IMPLEMENTATION_DOWN)
#define ML_JSON_IMPLEMENTATION_DOWN

char * ml_json_make_gemini_req(const char *prompt)
{
    assert(prompt != NULL);
    char *serialized_string = NULL;

    // 严密构建 {"contents": [{"parts": [{"text": prompt}]}]}
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON *contents = cJSON_CreateArray();
    if (!contents) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "contents", contents);

    cJSON *content_item = cJSON_CreateObject();
    if (!content_item) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToArray(contents, content_item);

    cJSON *parts = cJSON_CreateArray();
    if (!parts) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(content_item, "parts", parts);

    cJSON *part_item = cJSON_CreateObject();
    if (!part_item) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToArray(parts, part_item);

    cJSON_AddStringToObject(part_item, "text", prompt);

    // 转换为非格式化的紧凑字符串（节省网络带宽）
    serialized_string = cJSON_PrintUnformatted(root);
    
    // 一键释放整个树形结构内存
    cJSON_Delete(root); 
    return serialized_string;
}

int ml_json_parse_gemini_res(const char *json_str, char *out_text, size_t out_size)
{
    if (!json_str || !out_text || out_size == 0) return 0;

    int success = 0;
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return 0;

    // 极为严密的防御性逐层安全解析，任何一层由于 API 报错导致找不到对应 Key，都不会触发空指针崩溃
    cJSON *candidates = cJSON_GetObjectItemCaseSensitive(root, "candidates");
    if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
        if (candidate) {
            cJSON *content = cJSON_GetObjectItemCaseSensitive(candidate, "content");
            if (content) {
                cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
                if (cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
                    cJSON *part = cJSON_GetArrayItem(parts, 0);
                    if (part) {
                        cJSON *text = cJSON_GetObjectItemCaseSensitive(part, "text");
                        if (cJSON_IsString(text) && (text->valuestring != NULL)) {
                            // 安全拷贝，防止缓冲区溢出
                            strncpy(out_text, text->valuestring, out_size - 1);
                            out_text[out_size - 1] = '\0';
                            success = 1;
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return success;
}

#endif // ML_JSON_IMPLEMENTATION