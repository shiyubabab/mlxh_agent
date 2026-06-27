/*************************************************************************
	> File Name: ml_https.h
	> Author: mlxh
	> Mail: mlxh_gto@163.com 
	> Created Time: Sat 27 Jun 2026 10:37:07 PM CST
 ************************************************************************/

#ifndef ML_HTTPS_H
#define ML_HTTPS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "curl/curl.h"

typedef struct _ml_mem_s {
	char *memory;
	size_t count;
	size_t capacity;
} ml_mem_t;

#define ml_mem_append(m, s, rs) do { \
    size_t _rs = (rs); \
    if ((m)->count + _rs + 1 > (m)->capacity) { \
        size_t _new_cap = (m)->capacity * 2; \
        if (_new_cap < (m)->count + _rs + 1) { \
            _new_cap = (m)->count + _rs + 1; \
        } \
        char *_tm = realloc((m)->memory, _new_cap); \
        if (!_tm) { \
            printf("Fail to realloc\n"); \
            return 0; \
        } \
        (m)->memory = _tm; \
        (m)->capacity = _new_cap; \
    } \
    memcpy((m)->memory + (m)->count, (s), _rs); \
    (m)->count += _rs; \
    (m)->memory[(m)->count] = '\0';\
} while(0)

CURL * _ml_https_create(const char * url, const char *json_data, ml_mem_t * chunk, struct curl_slist **out_headers);

#define ML_HTTPS_FIRE(out_res, url, json_data, chunk) do { \
    struct curl_slist *local_headers = NULL; \
    CURL *handle = _ml_https_create(url, json_data, chunk, &local_headers); \
    if (handle == NULL) { \
        fprintf(stderr, "Fail to create curl handle!\n"); \
        exit(1); \
    } else { \
        out_res = curl_easy_perform(handle); \
        curl_slist_free_all(local_headers); \
        curl_easy_cleanup(handle);         \
    } \
} while(0)

#endif // ML_HTTPS_H

#if defined(ML_HTTPS_IMPLEMENTATION) && !defined(ML_HTTPS_IMPLEMENTATION_DOWN)
#define ML_HTTPS_IMPLEMENTATION_DOWN

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	ml_mem_t *mem = (ml_mem_t *)userp;
	ml_mem_append(mem, contents, realsize);
	return realsize;
}

CURL * _ml_https_create(const char * url, const char *json_data, ml_mem_t * chunk, struct curl_slist **out_headers)
{
	assert(url != NULL && chunk != NULL && json_data != NULL);
	
	chunk->memory = malloc(512);
	chunk->count = 0;
	chunk->capacity = 512;
    if (chunk->memory) chunk->memory[0] = '\0';

	CURL *curl_handle = curl_easy_init();
	if (curl_handle) {
		struct curl_slist *headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json"); 
        *out_headers = headers;

		curl_easy_setopt(curl_handle, CURLOPT_URL, url);
		curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_data); 

		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)chunk);
	} else {
		printf("Fail to init curl.\n");
	}

	return curl_handle;
}

#endif // ML_HTTPS_IMPLEMENTATION
