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
#include "value.h"

// Forward declare the struct
struct PipelineStepNode;
struct ServerContext;
struct AuthNode;  // Add forward declaration
struct EmailNode; // Add forward declaration

// Add function pointer type for step execution
typedef json_t* (*StepExecutor)(struct PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, struct ServerContext *ctx);

typedef enum {
    TEMPLATE_MUSTACHE,
    TEMPLATE_HTML,
    TEMPLATE_RAW
} TemplateType;

typedef struct TemplateNode {
    TemplateType type;
    uint64_t : 32;
    char *content;
} TemplateNode;

typedef struct ResponseBlockNode {
    char *redirect;           // Optional redirect URL
    TemplateNode *template;   // Optional template (mutually exclusive with redirect)
} ResponseBlockNode;

typedef struct PageNode {
    char *identifier;
    char *route;
    char *layout;
    char *title;
    char *description;
    char *method;
    struct ApiField *fields;
    char *redirect;          // Deprecated - kept for backward compatibility
    TemplateNode *template;
    ResponseBlockNode *errorBlock;    // New error block structure
    ResponseBlockNode *successBlock;  // New success block structure
    struct PipelineStepNode *pipeline;
    struct PipelineStepNode *referenceData;  // Reference data pipeline
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
    TemplateNode *headTemplate;
    TemplateNode *bodyTemplate;
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

typedef struct PartialNode {
    char *name;
    TemplateNode *template;  // Will be of type TEMPLATE_MUSTACHE
    struct PartialNode *next;
} PartialNode;

typedef struct GithubNode {
    Value clientId;     // Can be string or env var
    Value clientSecret; // Can be string or env var
} GithubNode;

typedef struct AuthNode {
    Value salt;
    GithubNode *github;  // NULL if not using GitHub auth
} AuthNode;

typedef struct EmailTemplateNode {
    char *name;
    char *subject;
    TemplateNode *template;
    struct EmailTemplateNode *next;
} EmailTemplateNode;

typedef struct SendGridNode {
    Value apiKey;
    Value fromEmail;
    Value fromName;
} SendGridNode;

typedef struct EmailNode {
    SendGridNode *sendgrid;
    EmailTemplateNode *templateHead;
} EmailNode;

typedef struct WebsiteNode {
    char *name;
    char *author;
    char *version;
    char *baseUrl;
    Value databaseUrl;
    Value port;
    AuthNode *auth;  // Authentication configuration
    EmailNode *email; // Email configuration
    PageNode *pageHead;
    StyleBlockNode *styleHead;
    LayoutNode *layoutHead;
    ApiEndpoint *apiHead;
    QueryNode *queryHead;
    TransformNode *transformHead;
    ScriptNode *scriptHead;
    PartialNode *partialHead;
    RouteMap *routeMap;
    LayoutMap *layoutMap;
    IncludeNode *includeHead;
} WebsiteNode;

#endif // AST_H
