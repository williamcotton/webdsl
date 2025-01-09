#ifndef SERVER_ROUTING_H
#define SERVER_ROUTING_H

#include "../ast.h"
#include "../arena.h"
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

typedef struct JQHashEntry {
    const char *filter;
    jq_state *jq;
    struct JQHashEntry *next;
} JQHashEntry;

// Thread cleanup function for JQ states
extern pthread_key_t jq_key;
void jq_thread_cleanup(void *ptr);

void buildRouteMaps(WebsiteNode *website, Arena *arena);
PageNode* findPage(const char *url);
LayoutNode* findLayout(const char *identifier);
ApiEndpoint* findApi(const char *url, const char *method);
QueryNode* findQuery(const char *name);
jq_state* findOrCreateJQ(const char *filter, Arena *arena);
void cleanupJQCache(void);

// Find a named transform by name
TransformNode* findTransform(const char *name);

// Find a named script by name
ScriptNode* findScript(const char *name);

#endif // SERVER_ROUTING_H
