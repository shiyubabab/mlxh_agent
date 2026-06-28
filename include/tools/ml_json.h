/*************************************************************************
	> File Name: ml_json.h
	> Author: mlxh
	> Mail: mlxh_gto@163.com 
	> Created Time: Sun 28 Jun 2026 10:15:32 AM CST
 ************************************************************************/

#ifndef ML_JSON_H
#define ML_JSON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cjson/cJSON.h"

// 1. 定义大模型可以呼叫的原子工具枚举
typedef enum {
    ML_TOOL_NONE = 0,
    ML_TOOL_WRITE_FILE,     // 写入/创建文件
    ML_TOOL_READ_FILE,      // 读取文件内容
    ML_TOOL_EXECUTE_COMMAND // 执行终端命令
} ml_tool_type_t;

// 2. 宿主程序解析出来的 Agent 意图结构体（下行通道）
typedef struct {
    char text_thought[1024 * 4];  // 大模型的内心思考（Thought 文本）
    ml_tool_type_t tool_type;     // 模型决定呼叫的工具类型
    char tool_call_id[64];        // 唯一工具调用 ID (用于上下文闭环对齐)
    char arg_path[512];           // 工具参数：文件路径（对应 write_file/read_file）
    char arg_content[1024 * 64];  // 工具参数：写入的内容（或执行的命令）
} ml_agent_intent_t;

// 3. 【工业级 Schema 注入】向大模型声明其可用的原子工具集清单
#define ML_AGENT_SYSTEM_INSTRUCTION \
    "You are a core autonomous Coding Agent. You interact with the host system via strict Tool Use.\n" \
    "You MUST reply with a pure JSON object matching this schema precisely:\n" \
    "{\n" \
    "  \"thought\": \"Your inner chain-of-thought reasoning before taking action\",\n" \
    "  \"tool_use\": {\n" \
    "    \"id\": \"a_random_unique_string_id\",\n" \
    "    \"name\": \"write_file\" | \"read_file\" | \"execute_command\",\n" \
    "    \"arguments\": {\n" \
    "      \"path\": \"target_file_path_or_null\",\n" \
    "      \"content\": \"file_content_to_write_or_shell_command_to_run\"\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "If you just want to talk to the user without calling tools, set \"tool_use\" to null.\n" \
    "Do NOT wrap your output in markdown code blocks like ```json."

// --- API 声明 ---
// 生成带系统级原子工具注入的最初请求体
char * ml_json_make_gemini_req(const char *prompt);

// 生成包含历史工具执行结果（Tool Result）的闭环上下文请求体，让 Agent 得知执行结果并继续思考
char * ml_json_make_tool_result_req(const char *prompt, const char *last_tool_id, const char *tool_output);

// 深度双重解包：解析大模型的回复，将数据精准分流灌入 ml_agent_intent_t 结构体
int ml_json_parse_gemini_res(const char *json_str, ml_agent_intent_t *out_intent);

#endif // ML_JSON_H

#if defined(ML_JSON_IMPLEMENTATION) && !defined(ML_JSON_IMPLEMENTATION_DOWN)
#define ML_JSON_IMPLEMENTATION_DOWN

char * ml_json_make_gemini_req(const char *prompt)
{
    assert(prompt != NULL);
    char *serialized_string = NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // 1. 系统级硬性约束
    cJSON *sys_instruction = cJSON_CreateObject();
    cJSON *sys_parts = cJSON_CreateArray();
    cJSON *sys_part_item = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_part_item, "text", ML_AGENT_SYSTEM_INSTRUCTION);
    cJSON_AddItemToArray(sys_parts, sys_part_item);
    cJSON_AddItemToObject(sys_instruction, "parts", sys_parts);
    cJSON_AddItemToObject(root, "systemInstruction", sys_instruction);

    // 2. 强制 JSON 模式
    cJSON *gen_config = cJSON_CreateObject();
    cJSON_AddStringToObject(gen_config, "responseMimeType", "application/json");
    cJSON_AddItemToObject(root, "generationConfig", gen_config);

    // 3. 用户 Prompt
    cJSON *contents = cJSON_CreateArray();
    cJSON *content_item = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();
    cJSON *part_item = cJSON_CreateObject();
    cJSON_AddStringToObject(part_item, "text", prompt);
    cJSON_AddItemToArray(parts, part_item);
    cJSON_AddItemToObject(content_item, "parts", parts);
    cJSON_AddItemToArray(contents, content_item);
    cJSON_AddItemToObject(root, "contents", contents);

    serialized_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); 
    return serialized_string;
}

char * ml_json_make_tool_result_req(const char *prompt, const char *last_tool_id, const char *tool_output)
{
    assert(prompt != NULL && last_tool_id != NULL && tool_output != NULL);
    char *serialized_string = NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // 同样保持系统指令注入
    cJSON *sys_instruction = cJSON_CreateObject();
    cJSON *sys_parts = cJSON_CreateArray();
    cJSON *sys_part_item = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_part_item, "text", ML_AGENT_SYSTEM_INSTRUCTION);
    cJSON_AddItemToArray(sys_parts, sys_part_item);
    cJSON_AddItemToObject(sys_instruction, "parts", sys_parts);
    cJSON_AddItemToObject(root, "systemInstruction", sys_instruction);

    cJSON *gen_config = cJSON_CreateObject();
    cJSON_AddStringToObject(gen_config, "responseMimeType", "application/json");
    cJSON_AddItemToObject(root, "generationConfig", gen_config);

    cJSON *contents = cJSON_CreateArray();

    // 历史会话第一步：用户的请求
    cJSON *user_content = cJSON_CreateObject();
    cJSON_AddStringToObject(user_content, "role", "user");
    cJSON *user_parts = cJSON_CreateArray();
    cJSON *user_part_item = cJSON_CreateObject();
    cJSON_AddStringToObject(user_part_item, "text", prompt);
    cJSON_AddItemToArray(user_parts, user_part_item);
    cJSON_AddItemToObject(user_content, "parts", user_parts);
    cJSON_AddItemToArray(contents, user_content);

    // 历史会话第二步：反馈工具执行结果（按照主流多轮对话协议，将结果再次反馈给 user 角色或特殊结果块）
    char result_payload[1024 * 68];
    snprintf(result_payload, sizeof(result_payload), 
             " [Tool Result Observation] ID: %s\nOutput:\n%s", last_tool_id, tool_output);

    cJSON *result_content = cJSON_CreateObject();
    cJSON_AddStringToObject(result_content, "role", "user");
    cJSON *result_parts = cJSON_CreateArray();
    cJSON *result_part_item = cJSON_CreateObject();
    cJSON_AddStringToObject(result_part_item, "text", result_payload);
    cJSON_AddItemToArray(result_parts, result_part_item);
    cJSON_AddItemToObject(result_content, "parts", result_parts);
    cJSON_AddItemToArray(contents, result_content);

    cJSON_AddItemToObject(root, "contents", contents);

    serialized_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return serialized_string;
}

int ml_json_parse_gemini_res(const char *json_str, ml_agent_intent_t *out_intent)
{
    if (!json_str || !out_intent) return 0;

    // 初始化结构体
    memset(out_intent, 0, sizeof(ml_agent_intent_t));
    out_intent->tool_type = ML_TOOL_NONE;

    int success = 0;
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return 0;

    // 深度挖掘底层 text 节点
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
                        cJSON *text_node = cJSON_GetObjectItemCaseSensitive(part, "text");
                        if (cJSON_IsString(text_node) && (text_node->valuestring != NULL)) {
                            
                            // 二次解包：解析我们定义的脑回路和工具协议
                            cJSON *inner_json = cJSON_Parse(text_node->valuestring);
                            if (inner_json) {
                                success = 1; // 结构匹配成功
                                
                                // 1. 抓取 thought 
                                cJSON *thought_item = cJSON_GetObjectItemCaseSensitive(inner_json, "thought");
                                if (cJSON_IsString(thought_item) && thought_item->valuestring) {
                                    strncpy(out_intent->text_thought, thought_item->valuestring, sizeof(out_intent->text_thought) - 1);
                                }

                                // 2. 抓取 tool_use 块
                                cJSON *tool_use = cJSON_GetObjectItemCaseSensitive(inner_json, "tool_use");
                                if (tool_use && !cJSON_IsNull(tool_use)) {
                                    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(tool_use, "id");
                                    cJSON *name_item = cJSON_GetObjectItemCaseSensitive(tool_use, "name");
                                    cJSON *args = cJSON_GetObjectItemCaseSensitive(tool_use, "arguments");

                                    if (cJSON_IsString(id_item)) {
                                        strncpy(out_intent->tool_call_id, id_item->valuestring, sizeof(out_intent->tool_call_id) - 1);
                                    }

                                    if (cJSON_IsString(name_item) && args) {
                                        const char *name = name_item->valuestring;
                                        if (strcmp(name, "write_file") == 0) out_intent->tool_type = ML_TOOL_WRITE_FILE;
                                        else if (strcmp(name, "read_file") == 0) out_intent->tool_type = ML_TOOL_READ_FILE;
                                        else if (strcmp(name, "execute_command") == 0) out_intent->tool_type = ML_TOOL_EXECUTE_COMMAND;

                                        // 3. 提取具体原子参数
                                        cJSON *path_item = cJSON_GetObjectItemCaseSensitive(args, "path");
                                        cJSON *content_item = cJSON_GetObjectItemCaseSensitive(args, "content");

                                        if (cJSON_IsString(path_item) && path_item->valuestring) {
                                            strncpy(out_intent->arg_path, path_item->valuestring, sizeof(out_intent->arg_path) - 1);
                                        }
                                        if (cJSON_IsString(content_item) && content_item->valuestring) {
                                            strncpy(out_intent->arg_content, content_item->valuestring, sizeof(out_intent->arg_content) - 1);
                                        }
                                    }
                                }
                                cJSON_Delete(inner_json);
                            }
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