#ifndef SERVER_ROUTING_H
#define SERVER_ROUTING_H

#include "../ast.h"
#include "../arena.h"
#include "route_params.h"
#include <jq.h>
#include <pthread.h>

#define HASH_TABLE_SIZE 64  // Should be power of 2
#define HASH_MASK (HASH_TABLE_SIZE - 1)

typedef struct RouteHashEntry {
    const char *route;
    PageNode *page;
    struct RouteHashEntry *next;
} RouteHashEntry;

typedef struct LayoutHashEntry {
    const char *identifier;
    LayoutNode *layout;
    struct LayoutHashEntry *next;
} LayoutHashEntry;

typedef struct ApiHashEntry {
    const char *route;
    const char *method;
    ApiEndpoint *endpoint;
    struct ApiHashEntry *next;
} ApiHashEntry;

typedef struct QueryHashEntry {
    const char *name;
    QueryNode *query;
    struct QueryHashEntry *next;
} QueryHashEntry;

// Structure to hold a page match with its route parameters
typedef struct PageMatch {
    PageNode *page;
    RouteParams params;
} PageMatch;

typedef struct JQHashEntry {
    const char *filter;
    jq_state *jq;
    struct JQHashEntry *next;
} JQHashEntry;

typedef struct PartialHashEntry {
    const char *name;
    PartialNode *partial;
    struct PartialHashEntry *next;
} PartialHashEntry;

// Thread cleanup function for JQ states
extern pthread_key_t jq_key;
void jq_thread_cleanup(void *ptr);

void buildRouteMaps(WebsiteNode *website, Arena *arena);
PageNode* findPage(const char *url, RouteParams *params, Arena *arena);
PageMatch* findPageWithParams(const char *url, Arena *arena);
LayoutNode* findLayout(const char *identifier);
ApiEndpoint* findApi(const char *url, const char *method, RouteParams *params, Arena *arena);
QueryNode* findQuery(const char *name);
PartialNode* findPartial(const char *name);
jq_state* findOrCreateJQ(const char *filter, Arena *arena);
void cleanupJQCache(void);

// Find a named transform by name
TransformNode* findTransform(const char *name);

// Find a named script by name
ScriptNode* findScript(const char *name);

typedef enum {
    ROUTE_TYPE_API,
    ROUTE_TYPE_PAGE,
    ROUTE_TYPE_NONE
} RouteType;

typedef struct {
    RouteType type;
    uint64_t : 32;
    union {
        ApiEndpoint *api;
        PageNode *page;
    } endpoint;
    RouteParams params;
} RouteMatch;

RouteMatch findRoute(const char *url, const char *method, Arena *arena);

#endif // SERVER_ROUTING_H
