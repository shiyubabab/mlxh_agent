#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 引入你的 HTTPS 轮子
#define ML_HTTPS_IMPLEMENTATION
#include "net/ml_https.h"

// 引入你的 JSON 轮子
#define ML_JSON_IMPLEMENTATION
#include "tools/ml_json.h"

int main(void) {
    // 基础环境初始化
    curl_global_init(CURL_GLOBAL_ALL);

    char *api_key = getenv("GEMINI_API_KEY");
    if (!api_key) {
        fprintf(stderr, "错误: 请先设置环境变量 GEMINI_API_KEY\n");
        curl_global_cleanup();
        return 1;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=%s", api_key);

    // 1. 【一句话组包】
    char *json_payload = ml_json_make_gemini_req("帮我拆解一下claude是怎么实现agent的，我要模拟一下");
    if (!json_payload) {
        fprintf(stderr, "JSON 组包失败！\n");
        curl_global_cleanup();
        return 1;
    }

    // 2. 【一句话发送】
    ml_mem_t response_chunk;
    CURLcode res;
    ML_HTTPS_FIRE(res, url, json_payload, &response_chunk);

    // 3. 【一句话拆包】
    if (res == CURLE_OK) {
        // 准备一个足够大的缓冲区用来装 AI 的回复
        static char ai_reply[1024 * 32]; 
        
        if (ml_json_parse_gemini_res(response_chunk.memory, ai_reply, sizeof(ai_reply))) {
            printf("\n--- Gemini 回复 ---\n%s\n-------------------\n", ai_reply);
        } else {
            printf("解析文本失败，可能接口报错。原始响应:\n%s\n", response_chunk.memory);
        }
    } else {
        fprintf(stderr, "网络请求失败: %s\n", curl_easy_strerror(res));
    }

    // 4. 清理释放
    free(json_payload);
    if (response_chunk.memory) free(response_chunk.memory);
    curl_global_cleanup();

    return 0;
}
