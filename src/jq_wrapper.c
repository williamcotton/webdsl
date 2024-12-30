#include "jq_wrapper.h"
#include <string.h>

char* applyJqFilter(Arena *arena, const char *json, const char *filter) {
    if (!json || !filter) return NULL;

    // Initialize JQ
    jq_state *jq = jq_init();
    if (!jq) return NULL;

    // Compile the filter
    if (!jq_compile(jq, filter)) {
        jq_teardown(&jq);
        return NULL;
    }

    // Parse input JSON
    jv input = jv_parse(json);
    if (!jv_is_valid(input)) {
        jv_free(input);
        jq_teardown(&jq);
        return NULL;
    }

    // Process the filter
    jq_start(jq, input, 0);
    jv result = jq_next(jq);
    
    char *output = NULL;
    if (jv_is_valid(result)) {
        // Dump the result to a string
        jv dumped = jv_dump_string(result, 0);
        const char *str = jv_string_value(dumped);
        output = arenaDupString(arena, str);
        jv_free(dumped);
    }
    
    jv_free(result);
    jq_teardown(&jq);
    return output;
}
