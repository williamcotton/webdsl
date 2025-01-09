#include "jq.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <jv.h>
#include "routing.h"

static jv janssonToJv(json_t *json) {
  switch (json_typeof(json)) {
  case JSON_OBJECT: {
    jv obj = jv_object();
    const char *key;
    json_t *value;
    json_object_foreach(json, key, value) {
      jv val = janssonToJv(value);
      if (!jv_is_valid(val)) {
        jv_free(obj);
        return val;
      }
      obj = jv_object_set(obj, jv_string(key), val);
    }
    return obj;
  }
  case JSON_ARRAY: {
    jv arr = jv_array();
    size_t index;
    json_t *value;
    json_array_foreach(json, index, value) {
      jv val = janssonToJv(value);
      if (!jv_is_valid(val)) {
        jv_free(arr);
        return val;
      }
      arr = jv_array_append(arr, val);
    }
    return arr;
  }
  case JSON_STRING:
    return jv_string(json_string_value(json));
  case JSON_INTEGER: {
    json_int_t val = json_integer_value(json);
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
    return jv_invalid();
  }
}

static json_t *jvToJansson(jv value) {
  if (!jv_is_valid(value)) {
    jv_free(value);
    return NULL;
  }

  json_t *result = NULL;
  switch (jv_get_kind(value)) {
  case JV_KIND_OBJECT: {
    result = json_object();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconditional-uninitialized"
    jv_object_foreach(value, k, v) {
      const char *key_str = jv_string_value(k);
      json_t *json_val = jvToJansson(v);
      if (json_val) {
        json_object_set_new(result, key_str, json_val);
      }
      jv_free(k);
    }
#pragma clang diagnostic pop
    break;
  }
  case JV_KIND_ARRAY: {
    result = json_array();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconditional-uninitialized"
    jv_array_foreach(value, i, v) {
      json_t *json_val = jvToJansson(v);
      if (json_val) {
        json_array_append_new(result, json_val);
      }
    }
#pragma clang diagnostic pop
    break;
  }
  case JV_KIND_STRING:
    result = json_string(jv_string_value(value));
    break;
  case JV_KIND_NUMBER:
    result = json_real(jv_number_value(value));
    break;
  case JV_KIND_TRUE:
    result = json_true();
    break;
  case JV_KIND_FALSE:
    result = json_false();
    break;
  case JV_KIND_NULL:
    result = json_null();
    break;
  case JV_KIND_INVALID:
    break;
  }
  
  jv_free(value);
  return result;
}

static jv processJqFilter(jq_state *jq, json_t *input_json) {
    if (!jq || !input_json) return jv_invalid();

    jv input = janssonToJv(input_json);
    
    if (!jv_is_valid(input)) {
        jv jv_error = jv_invalid_get_msg(input);
        if (jv_is_valid(jv_error)) {
            fprintf(stderr, "JSON conversion error: %s\n", jv_string_value(jv_error));
            jv_free(jv_error);
        }
        return jv_invalid();
    }

    jq_start(jq, input, 0);  // input is consumed here
    
    jv filtered_result = jq_next(jq);

    if (!jv_is_valid(filtered_result)) {
        jv jv_error = jv_invalid_get_msg(filtered_result);
        if (jv_is_valid(jv_error)) {
            fprintf(stderr, "JQ execution error: %s\n", jv_string_value(jv_error));
            jv_free(jv_error);
        }
        return filtered_result;  // Already invalid and freed
    }

    // Drain any remaining results to prevent memory leaks
    jv next_result;
    while (jv_is_valid(next_result = jq_next(jq))) {
        jv_free(next_result);
    }
    jv_free(next_result);  // Free the invalid result that ended the loop

    return filtered_result;
}

json_t* executeJqStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, ServerContext *ctx) {
    (void)requestContext;
    (void)arena;
    
    // Check for existing error
    json_t *error = json_object_get(input, "error");
    if (error) {
        return json_deep_copy(input);
    }

    // Get code from named transform if specified
    const char* code = step->code;
    if (step->name) {
        TransformNode* namedTransform = findTransform(step->name);
        if (!namedTransform) {
            json_t *result = json_object();
            json_object_set_new(result, "error", json_string("Transform not found"));
            return result;
        }
        code = namedTransform->code;
    }

    if (!code) {
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string("No transform code found"));
        return result;
    }
    
    jq_state *jq = findOrCreateJQ(code, ctx->arena);
    if (!jq) {
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string("Failed to create JQ state"));
        return result;
    }
    
    jv filtered_jv = processJqFilter(jq, input);
    if (!jv_is_valid(filtered_jv)) {
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string("Failed to process JQ filter"));
        return result;
    }
    
    json_t *result = jvToJansson(filtered_jv);  // This frees filtered_jv
    
    if (!result) {
        json_t *error_result = json_object();
        json_object_set_new(error_result, "error", json_string("Failed to convert JQ result to JSON"));
        return error_result;
    }
    
    return result;
}
