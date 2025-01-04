#ifndef AST_H
#define AST_H
#include <stdint.h>
#include <stdbool.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include <jq.h>
#pragma clang diagnostic pop

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
    char *code;       // The actual filter/query code
    char *name;
    StepType type;    // Optional name for SQL queries
    bool is_dynamic;  // For SQL steps
    uint64_t : 24;    // Padding for alignment
    struct PipelineStepNode *next;
} PipelineStepNode;

typedef struct ApiEndpoint {
    char *route;
    char *method;
    union {
        // Legacy style
        struct {
            char *executeQuery;
            char *preFilter;
            char *postFilter;
            FilterType preFilterType;
            FilterType postFilterType;
            bool isDynamicQuery;
            uint8_t _padding[7];
        } legacy;
        // New style
        PipelineStepNode *pipeline;
    } handler;
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
    RouteMap *routeMap;
    LayoutMap *layoutMap;
} WebsiteNode;

#endif // AST_H
