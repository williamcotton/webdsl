#include "jq.h"
#include "utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <jv.h>

jv processJqFilter(jq_state *jq, json_t *input_json) {
    if (!jq || !input_json) return jv_invalid();

    // Convert to JV format
    jv input = janssonToJv(input_json);
    if (!jv_is_valid(input)) {
        jv jv_error = jv_invalid_get_msg(input);
        if (jv_is_valid(jv_error)) {
            fprintf(stderr, "JSON conversion error: %s\n", jv_string_value(jv_error));
            jv_free(jv_error);
        }
        jv_free(input);
        return jv_invalid();
    }

    // Process the filter
    jq_start(jq, input, 0);
    jv filtered_result = jq_next(jq);

    if (!jv_is_valid(filtered_result)) {
        jv jv_error = jv_invalid_get_msg(filtered_result);
        if (jv_is_valid(jv_error)) {
            fprintf(stderr, "JQ execution error: %s\n", jv_string_value(jv_error));
            jv_free(jv_error);
        }
        return filtered_result; // Already invalid
    }

    return filtered_result;
}

json_t* executeJqStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena) {
    (void)requestContext;
    (void)arena;
    
    jq_state *jq = findOrCreateJQ(step->code);
    if (!jq) {
        return NULL;
    }
    
    jv filtered_jv = processJqFilter(jq, input);
    if (!jv_is_valid(filtered_jv)) {
        return NULL;
    }
    
    json_t *result = jvToJansson(filtered_jv);
    jv_free(filtered_jv);
    
    if (!result) {
        fprintf(stderr, "Failed to convert JQ result to JSON\n");
    }
    return result;
}

jv janssonToJv(json_t *json) {
    switch (json_typeof(json)) {
        case JSON_OBJECT: {
            jv obj = jv_object();
            const char *key;
            json_t *value;
            json_object_foreach(json, key, value) {
                obj = jv_object_set(obj, jv_string(key), janssonToJv(value));
            }
            return obj;
        }
        case JSON_ARRAY: {
            jv arr = jv_array();
            size_t index;
            json_t *value;
            json_array_foreach(json, index, value) {
                arr = jv_array_append(arr, janssonToJv(value));
            }
            return arr;
        }
        case JSON_STRING:
            return jv_string(json_string_value(json));
        case JSON_INTEGER: {
            json_int_t val = json_integer_value(json);
            // Use explicit cast to avoid implicit conversion warning
            return jv_number((double)val);
        }
        case JSON_REAL:
            return jv_number(json_real_value(json));
        case JSON_TRUE:
            return jv_true();
        case JSON_FALSE:
            return jv_false();
        case JSON_NULL:
            return jv_null();
        default:
            return jv_null();
    }
}

json_t* jvToJansson(jv value) {
    if (!jv_is_valid(value)) {
        return NULL;
    }

    switch (jv_get_kind(value)) {
        case JV_KIND_OBJECT: {
            json_t *obj = json_object();
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wconditional-uninitialized"
            jv_object_foreach(value, key, val) {
                const char *key_str = jv_string_value(key);
                json_t *json_val = jvToJansson(val);
                if (json_val) {
                    json_object_set_new(obj, key_str, json_val);
                }
            }
            #pragma clang diagnostic pop
            return obj;
        }
        case JV_KIND_ARRAY: {
            json_t *arr = json_array();
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wconditional-uninitialized"
            jv_array_foreach(value, index, val) {
                json_t *json_val = jvToJansson(val);
                if (json_val) {
                    json_array_append_new(arr, json_val);
                }
            }
            #pragma clang diagnostic pop
            return arr;
        }
        case JV_KIND_STRING:
            return json_string(jv_string_value(value));
        case JV_KIND_NUMBER:
            return json_real(jv_number_value(value));
        case JV_KIND_TRUE:
            return json_true();
        case JV_KIND_FALSE:
            return json_false();
        case JV_KIND_NULL:
            return json_null();
        case JV_KIND_INVALID:
            return NULL;
        default:
            return NULL;
    }
}
