#include "../Utility/cJSON.h"

int json_get_int(cJSON *json, const char *key){
    cJSON *item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : -1;
}

char* json_get_string(cJSON *json, const char *key){
    cJSON *item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsString(item)) ? item->valuestring : "";
}