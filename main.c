#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =========================================================================
// 🔬 日志控制宏定义
// =========================================================================
#define ML_DEBUG 1  // 👈 改为 0 可以一键关闭所有的底层追踪日志

#if ML_DEBUG
    #define ML_LOG(fmt, ...) printf("[TRACE][%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define ML_LOG(fmt, ...) do {} while(0)
#endif

// =========================================================================
// 📦 引入你手搓的两个核心轮子
// =========================================================================
#define ML_HTTPS_IMPLEMENTATION
#include "net/ml_https.h"

#define ML_JSON_IMPLEMENTATION
#include "tools/ml_json.h"

// 宿主系统的真实文件读取工具
int host_read_file(const char *path, char *buffer, size_t max_size) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    size_t bytes = fread(buffer, 1, max_size - 1, fp);
    buffer[bytes] = '\0';
    fclose(fp);
    return 1;
}

int main(void) {
    ML_LOG("正在启动 Agent 宿主状态机...");
    curl_global_init(CURL_GLOBAL_ALL);

    char *api_key = getenv("GEMINI_API_KEY");
    if (!api_key) {
        fprintf(stderr, "【严重错误】: 请先设置环境变量 GEMINI_API_KEY\n");
        curl_global_cleanup();
        return 1;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=%s", api_key);
    ML_LOG("API 路由拼接完成，准备发往: %s", "gemini-2.5-flash");

    // =========================================================================
    // 🧠 核心：初始化全局多轮对话历史记录缓存（cJSON 数组）
    // =========================================================================
    cJSON *history_root = cJSON_CreateObject();
    cJSON *history_contents = cJSON_CreateArray();
    cJSON_AddItemToObject(history_root, "contents", history_contents);

    // 注入全局系统指令和配置
    cJSON *sys_instruction = cJSON_CreateObject();
    cJSON *sys_parts = cJSON_CreateArray();
    cJSON *sys_part_item = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_part_item, "text", ML_AGENT_SYSTEM_INSTRUCTION);
    cJSON_AddItemToArray(sys_parts, sys_part_item);
    cJSON_AddItemToObject(sys_instruction, "parts", sys_parts);
    cJSON_AddItemToObject(history_root, "systemInstruction", sys_instruction);

    cJSON *gen_config = cJSON_CreateObject();
    cJSON_AddStringToObject(gen_config, "responseMimeType", "application/json");
    cJSON_AddItemToObject(history_root, "generationConfig", gen_config);

    // 1. 压入初始宏观任务
    const char *initial_prompt = "查看本地 src/hello.c 是否有语法错误，如果有就把它编译并运行。";
    ML_LOG("用户初始请求输入: %s", initial_prompt);

    cJSON *user_turn = cJSON_CreateObject();
    cJSON_AddStringToObject(user_turn, "role", "user");
    cJSON *user_parts = cJSON_CreateArray();
    cJSON *user_part_item = cJSON_CreateObject();
    cJSON_AddStringToObject(user_part_item, "text", initial_prompt);
    cJSON_AddItemToArray(user_parts, user_part_item);
    cJSON_AddItemToObject(user_turn, "parts", user_parts);
    cJSON_AddItemToArray(history_contents, user_turn);

    // 变量准备
    ml_mem_t response_chunk;
    CURLcode res;
    ml_agent_intent_t intent;
    static char tool_output_buf[1024 * 64] = {0}; // 真实环境观测结果缓存
    int loop_count = 1;

    // =========================================================================
    // 🔄 Agent 自主迭代无限状态机循环
    // =========================================================================
    while (1) {
        printf("\n==================== [ 第 %d 轮 智 能 体 迭 代 ] ====================\n", loop_count++);
        
        // 将当前完整的历史记录序列化为发送 Payload
        char *json_payload = cJSON_PrintUnformatted(history_root);
        ML_LOG("发送包构建成功，当前序列化长度: %lu 字节", strlen(json_payload));

        // 清空接收缓存
        response_chunk.memory = NULL;
        response_chunk.count = 0;
        response_chunk.capacity = 0;

        ML_LOG("正在向 Gemini 发送网络请求 (阻塞等待中)...");
        ML_HTTPS_FIRE(res, url, json_payload, &response_chunk);
        free(json_payload); // 及时释放发送临时文本

        if (res != CURLE_OK) {
            fprintf(stderr, "【网络层失败】: %s\n", curl_easy_strerror(res));
            break;
        }

        ML_LOG("网络响应成功。收到数据大小: %lu 字节，准备解析核心协议...", response_chunk.count);

        // 解析当前轮次大模型的工具呼叫决策
        if (!ml_json_parse_gemini_res(response_chunk.memory, &intent)) {
            ML_LOG("【协议解包失败】大模型返回的数据不符合 Schema 约束。");
            printf("原始返回内容:\n%s\n", response_chunk.memory);
            free(response_chunk.memory);
            break;
        }

        // 打印大模型的“脑回路”
        printf("\n🧠 [Agent 脑回路思考]:\n%s\n", intent.text_thought);

        // 2. 【历史滚动第一步】: 把大模型的回复（包含他的 tool_use 请求）作为 model 角色压入历史记录
        cJSON *model_turn = cJSON_CreateObject();
        cJSON_AddStringToObject(model_turn, "role", "model");
        cJSON *model_parts = cJSON_CreateArray();
        cJSON *model_part_item = cJSON_CreateObject();
        
        // 直接从返回的原生核心字段中提取文本作为记忆保存
        cJSON *root_temp = cJSON_Parse(response_chunk.memory);
        if (root_temp) {
            cJSON *cands = cJSON_GetObjectItemCaseSensitive(root_temp, "candidates");
            cJSON *cand = cJSON_GetArrayItem(cands, 0);
            cJSON *cont = cJSON_GetObjectItemCaseSensitive(cand, "content");
            cJSON *pts = cJSON_GetObjectItemCaseSensitive(cont, "parts");
            cJSON *pt = cJSON_GetArrayItem(pts, 0);
            cJSON *txt = cJSON_GetObjectItemCaseSensitive(pt, "text");
            if (txt) cJSON_AddStringToObject(model_part_item, "text", txt->valuestring);
            cJSON_Delete(root_temp);
        }
        cJSON_AddItemToArray(model_parts, model_part_item);
        cJSON_AddItemToObject(model_turn, "parts", model_parts);
        cJSON_AddItemToArray(history_contents, model_turn);

        free(response_chunk.memory); // 释放当前网络 chunk 空间

        // --- 3. 宿主系统根据意图驱动真实的原子工具，并捕获反馈信息 ---
        tool_output_buf[0] = '\0';
        int should_continue = 1;

        switch (intent.tool_type) {
            case ML_TOOL_READ_FILE:
                printf("📁 [Host 动作]: 读取文件 -> %s\n", intent.arg_path);
                if (host_read_file(intent.arg_path, tool_output_buf, sizeof(tool_output_buf))) {
                    ML_LOG("原子工具 [READ_FILE] 执行成功");
                } else {
                    ML_LOG("原子工具 [READ_FILE] 执行失败，目标文件不存在");
                    snprintf(tool_output_buf, sizeof(tool_output_buf), "Error: Cannot read file %s. File does not exist.", intent.arg_path);
                }
                break;

            case ML_TOOL_WRITE_FILE:
                printf("💾 [Host 动作]: 自动写盘 -> %s\n", intent.arg_path);
                FILE *fp = fopen(intent.arg_path, "w");
                if (fp) {
                    fputs(intent.arg_content, fp);
                    fclose(fp);
                    ML_LOG("原子工具 [WRITE_FILE] 写入磁盘闭环成功");
                    snprintf(tool_output_buf, sizeof(tool_output_buf), "Success: File %s has been written successfully.", intent.arg_path);
                } else {
                    ML_LOG("原子工具 [WRITE_FILE] 打开目标路径失败");
                    snprintf(tool_output_buf, sizeof(tool_output_buf), "Error: Failed to write to path %s.", intent.arg_path);
                }
                break;

            case ML_TOOL_EXECUTE_COMMAND:
                printf("⚡ [Host 动作]: 终端执行 -> %s\n", intent.arg_content);
                // 工业级应当使用 popen 抓取输出，这里先简单模拟编译反馈闭环
                ML_LOG("正在捕获终端 Stdout/Stderr...");
                snprintf(tool_output_buf, sizeof(tool_output_buf), "gcc: error: %s: No such file or directory", intent.arg_path);
                break;

            case ML_TOOL_NONE:
            default:
                printf("\n💬 [Agent 智能体任务圆满终结]: %s\n", intent.text_thought);
                should_continue = 0;
                break;
        }

        if (!should_continue) {
            ML_LOG("检测到 Agent 状态进入终结节点（ML_TOOL_NONE），优雅退出状态机。");
            break;
        }

        // 4. 【历史滚动第二步】: 把 Host 执行原子工具捞出来的真实结果，以 user 身份追加进历史记录数组中
        char observation_payload[1024 * 68];
        snprintf(observation_payload, sizeof(observation_payload), 
                 " [Tool Result Observation] ID: %s\nOutput:\n%s", intent.tool_call_id, tool_output_buf);
        
        ML_LOG("正在将本地观测数据压入全局 Context 历史链表...");

        cJSON *result_turn = cJSON_CreateObject();
        cJSON_AddStringToObject(result_turn, "role", "user");
        cJSON *result_parts = cJSON_CreateArray();
        cJSON *result_part_item = cJSON_CreateObject();
        cJSON_AddStringToObject(result_part_item, "text", observation_payload);
        cJSON_AddItemToArray(result_parts, result_part_item);
        cJSON_AddItemToObject(result_turn, "parts", result_parts);
        cJSON_AddItemToArray(history_contents, result_turn);
    }

    // =========================================================================
    // 🧹 完美的内存终结释放
    // =========================================================================
    ML_LOG("正在销毁全局多轮对话上下文 cJSON 树...");
    cJSON_Delete(history_root);
    
    ML_LOG("释放底层网络环境句柄...");
    curl_global_cleanup();
    
    printf("\n[系统通知]: Agent 彻底停止工作，安全退出。\n");
    return 0;
}
