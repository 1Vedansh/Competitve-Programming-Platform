#ifndef JSON_UTILITY_H
#define JSON_UTILITY_H

#include "../Utility/cJSON.h"

int json_get_int(cJSON *json, const char *key);

char* json_get_string(cJSON *json, const char *key);

#endif