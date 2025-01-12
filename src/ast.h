#ifndef AST_H
#define AST_H
#include <stdint.h>
#include <stdbool.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include <jq.h>
#pragma clang diagnostic pop
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "arena.h"

// Forward declare the struct
struct PipelineStepNode;
struct ServerContext;

// Add function pointer type for step execution
typedef json_t* (*StepExecutor)(struct PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, struct ServerContext *ctx);

// Node types
typedef enum {
    NODE_WEBSITE,
    NODE_LAYOUT,
    NODE_PAGE,
    NODE_CONTENT,
    NODE_API,
    NODE_QUERY,
    NODE_PIPELINE,
    NODE_PIPELINE_STEP,
    NODE_FIELD,
    NODE_FIELD_VALIDATION,
    NODE_TRANSFORM,
    NODE_SCRIPT,
    NODE_INCLUDE
} NodeType;

typedef struct ContentNode {
    char *type;
    char *arg1;
    char *arg2;
    struct ContentNode *next;
    struct ContentNode *children;
} ContentNode;

typedef struct PageNode {
    char *identifier;
    char *route;
    char *layout;
    char *title;
    char *description;
    ContentNode *contentHead;
    struct PipelineStepNode *pipeline;  // Pipeline for preparing mustache template data
    struct PageNode *next;
} PageNode;

typedef struct StylePropNode {
    char *property;
    char *value;
    struct StylePropNode *next;
} StylePropNode;

typedef struct StyleBlockNode {
    char *selector;
    StylePropNode *propHead;
    struct StyleBlockNode *next;
} StyleBlockNode;

typedef struct LayoutNode {
    const char *identifier;
    char *doctype;
    ContentNode *headContent;
    ContentNode *bodyContent;
    struct LayoutNode *next;
} LayoutNode;

typedef struct ResponseField {
    char *name;
    struct ResponseField *next;
} ResponseField;

typedef struct ApiField {
    char *name;
    char *type;
    char *format;
    union {
        struct {
            int min;
            int max;
        } range;
        struct {
            char *pattern;
        } match;
    } validate;
    int minLength;
    int maxLength;
    bool required;
    uint64_t : 56;
    struct ApiField *next;
} ApiField;

typedef enum FilterType {
    FILTER_JQ,
    FILTER_LUA
} FilterType;

typedef enum StepType {
    STEP_JQ,
    STEP_LUA,
    STEP_SQL,
    STEP_DYNAMIC_SQL
} StepType;

typedef struct PipelineStepNode {
    StepExecutor execute;  // Function pointer for execution (8 bytes)
    char *code;           // The actual filter/query code (8 bytes)
    char *name;           // Optional name for SQL queries (8 bytes)
    StepType type;        // Type of step (4 bytes)
    bool is_dynamic;      // For SQL steps (1 byte)
    uint8_t _padding[3];  // Explicit padding (3 bytes)
    struct PipelineStepNode *next;  // Next step in pipeline (8 bytes)
} PipelineStepNode;

typedef struct ApiEndpoint {
    char *route;
    char *method;
    PipelineStepNode *pipeline;
    bool uses_pipeline;  // Flag to indicate which union member to use
    uint8_t _padding[7];
    ResponseField *fields;
    ApiField *apiFields;
    struct ApiEndpoint *next;
} ApiEndpoint;

typedef struct QueryParam {
    char *name;
    struct QueryParam *next;
} QueryParam;

typedef struct QueryNode {
    char *name;
    char *sql;
    QueryParam *params;
    struct QueryNode *next;
} QueryNode;

typedef struct RouteMap {
    PageNode *page;
    char *route;
    struct RouteMap *next;
} RouteMap;

typedef struct LayoutMap {
    LayoutNode *layout;
    char *identifier;
    struct LayoutMap *next;
} LayoutMap;

typedef struct TransformNode {
    char *name;
    char *code;
    FilterType type;  // Will be FILTER_JQ
    uint8_t _padding[4];
    struct TransformNode *next;
} TransformNode;

typedef struct ScriptNode {
    char *name;
    char *code;
    FilterType type;  // Will be FILTER_LUA
    uint8_t _padding[4];
    struct ScriptNode *next;
} ScriptNode;

typedef struct IncludeNode {
    char *filepath;
    int line;
    uint64_t : 32;
    struct IncludeNode *next;
} IncludeNode;

typedef struct WebsiteNode {
    char *name;
    char *author;
    char *version;
    char *base_url;
    char *databaseUrl;
    int port;
    int _padding;
    PageNode *pageHead;
    StyleBlockNode *styleHead;
    LayoutNode *layoutHead;
    ApiEndpoint *apiHead;
    QueryNode *queryHead;
    TransformNode *transformHead;
    ScriptNode *scriptHead;
    RouteMap *routeMap;
    LayoutMap *layoutMap;
    IncludeNode *includeHead;
} WebsiteNode;

#endif // AST_H
