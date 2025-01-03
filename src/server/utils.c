#include "utils.h"
#include <string.h>

char* generateErrorJson(const char *errorMessage) {
    json_t *root = json_object();
    json_object_set_new(root, "error", json_string(errorMessage));
    
    char *jsonStr = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return jsonStr;
}
