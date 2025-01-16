#include "routing.h"
#include "utils.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <jq.h>
#include <pthread.h>

typedef struct TransformHashEntry {
    const char *name;
    TransformNode *transform;
    struct TransformHashEntry *next;
} TransformHashEntry;

typedef struct ScriptHashEntry {
    const char *name;
    ScriptNode *script;
    struct ScriptHashEntry *next;
} ScriptHashEntry;

static RouteHashEntry *routeTable[HASH_TABLE_SIZE];
static LayoutHashEntry *layoutTable[HASH_TABLE_SIZE];
static ApiHashEntry *apiTable[HASH_TABLE_SIZE];
static QueryHashEntry *queryTable[HASH_TABLE_SIZE];
static TransformHashEntry *transformTable[HASH_TABLE_SIZE];
static ScriptHashEntry *scriptTable[HASH_TABLE_SIZE];
static PartialHashEntry *partialTable[HASH_TABLE_SIZE];
static JQHashEntry *jqTable[HASH_TABLE_SIZE];
static __thread JQHashEntry **threadJQTable = NULL;
pthread_key_t jq_key;
static pthread_once_t jq_key_once = PTHREAD_ONCE_INIT;

void jq_thread_cleanup(void *ptr) {
    JQHashEntry **table = (JQHashEntry **)ptr;
    if (!table) return;
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        JQHashEntry *entry = table[i];
        while (entry) {
            JQHashEntry *next = entry->next;
            if (entry->jq) {
                jq_teardown(&entry->jq);
            }
            entry = next;
        }
        table[i] = NULL;
    }
    pthread_setspecific(jq_key, NULL);
}

static void jq_key_create(void) {
    pthread_key_create(&jq_key, jq_thread_cleanup);
}

static JQHashEntry** getThreadJQTable(Arena *arena) {
    pthread_once(&jq_key_once, jq_key_create);
    
    JQHashEntry **table = pthread_getspecific(jq_key);
    if (!table) {
        table = arenaAlloc(arena, HASH_TABLE_SIZE * sizeof(JQHashEntry*));
        memset(table, 0, HASH_TABLE_SIZE * sizeof(JQHashEntry*));
        pthread_setspecific(jq_key, table);
    }
    return table;
}

void buildRouteMaps(WebsiteNode *website, Arena *arena) {
    memset(routeTable, 0, sizeof(routeTable));
    memset(layoutTable, 0, sizeof(layoutTable));
    memset(apiTable, 0, sizeof(apiTable));
    memset(queryTable, 0, sizeof(queryTable));
    memset(transformTable, 0, sizeof(transformTable));
    memset(scriptTable, 0, sizeof(scriptTable));
    memset(partialTable, 0, sizeof(partialTable));
    memset(jqTable, 0, sizeof(jqTable));

    // Build page routes
    for (PageNode *page = website->pageHead; page; page = page->next) {
        uint32_t hash = hashString(page->route) & HASH_MASK;
        RouteHashEntry *entry = arenaAlloc(arena, sizeof(RouteHashEntry));
        entry->route = page->route;
        entry->page = page;
        entry->next = routeTable[hash];
        routeTable[hash] = entry;
    }

    // Build layout routes
    for (LayoutNode *layout = website->layoutHead; layout; layout = layout->next) {
        uint32_t hash = hashString(layout->identifier) & HASH_MASK;
        LayoutHashEntry *entry = arenaAlloc(arena, sizeof(LayoutHashEntry));
        entry->identifier = layout->identifier;
        entry->layout = layout;
        entry->next = layoutTable[hash];
        layoutTable[hash] = entry;
    }

    // Build API routes
    for (ApiEndpoint *api = website->apiHead; api; api = api->next) {
        uint32_t hash = hashString(api->route);
        hash ^= hashString(api->method);
        hash &= HASH_MASK;
        
        ApiHashEntry *entry = arenaAlloc(arena, sizeof(ApiHashEntry));
        entry->route = api->route;
        entry->method = api->method;
        entry->endpoint = api;
        entry->next = apiTable[hash];
        apiTable[hash] = entry;
    }

    // Build query routes
    for (QueryNode *query = website->queryHead; query; query = query->next) {
        uint32_t hash = hashString(query->name) & HASH_MASK;
        QueryHashEntry *entry = arenaAlloc(arena, sizeof(QueryHashEntry));
        entry->name = query->name;
        entry->query = query;
        entry->next = queryTable[hash];
        queryTable[hash] = entry;
    }

    // Build transform routes
    for (TransformNode *transform = website->transformHead; transform; transform = transform->next) {
        uint32_t hash = hashString(transform->name) & HASH_MASK;
        TransformHashEntry *entry = arenaAlloc(arena, sizeof(TransformHashEntry));
        entry->name = transform->name;
        entry->transform = transform;
        entry->next = transformTable[hash];
        transformTable[hash] = entry;
    }
    
    // Build script routes
    for (ScriptNode *script = website->scriptHead; script; script = script->next) {
        uint32_t hash = hashString(script->name) & HASH_MASK;
        ScriptHashEntry *entry = arenaAlloc(arena, sizeof(ScriptHashEntry));
        entry->name = script->name;
        entry->script = script;
        entry->next = scriptTable[hash];
        scriptTable[hash] = entry;
    }

    // Build partial routes
    for (PartialNode *partial = website->partialHead; partial; partial = partial->next) {
        uint32_t hash = hashString(partial->name) & HASH_MASK;
        PartialHashEntry *entry = arenaAlloc(arena, sizeof(PartialHashEntry));
        entry->name = partial->name;
        entry->partial = partial;
        entry->next = partialTable[hash];
        partialTable[hash] = entry;
    }
}

PageNode* findPage(const char *url, RouteParams *params, Arena *arena) {
    uint32_t hash = hashString(url) & HASH_MASK;
    RouteHashEntry *entry = routeTable[hash];
    
    // First try exact match for performance
    while (entry) {
        if (strcmp(entry->route, url) == 0) {
            // For exact matches, params will be empty
            params->count = 0;
            return entry->page;
        }
        entry = entry->next;
    }
    
    // If no exact match, try pattern matching
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        entry = routeTable[i];
        while (entry) {
            if (parseRouteParams(entry->route, url, params, arena)) {
                return entry->page;
            }
            entry = entry->next;
        }
    }
    
    return NULL;
}

LayoutNode* findLayout(const char *identifier) {
    // return null if identifier is null
    if (identifier == NULL) {
        return NULL;
    }
    uint32_t hash = hashString(identifier) & HASH_MASK;
    LayoutHashEntry *entry = layoutTable[hash];
    
    while (entry) {
        if (strcmp(entry->identifier, identifier) == 0) {
            return entry->layout;
        }
        entry = entry->next;
    }
    return NULL;
}

ApiEndpoint* findApi(const char *url, const char *method, RouteParams *params, Arena *arena) {
    uint32_t hash = hashString(url);
    hash ^= hashString(method);
    hash &= HASH_MASK;
    ApiHashEntry *entry = apiTable[hash];

    // First try exact match for performance
    while (entry) {
        if (strcmp(entry->route, url) == 0 && strcmp(entry->method, method) == 0) {
            params->count = 0;
            return entry->endpoint;
        }
        entry = entry->next;
    }
    
    // If no exact match, try pattern matching
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        entry = apiTable[i];
        while (entry) {
            if (parseRouteParams(entry->route, url, params, arena)) {
                return entry->endpoint;
            }
            entry = entry->next;
        }
    }

    return NULL;
}

QueryNode* findQuery(const char *name) {
    uint32_t hash = hashString(name) & HASH_MASK;
    QueryHashEntry *entry = queryTable[hash];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->query;
        }
        entry = entry->next;
    }
    return NULL;
}

TransformNode* findTransform(const char *name) {
    uint32_t hash = hashString(name) & HASH_MASK;
    TransformHashEntry *entry = transformTable[hash];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->transform;
        }
        entry = entry->next;
    }
    return NULL;
}

ScriptNode* findScript(const char *name) {
    uint32_t hash = hashString(name) & HASH_MASK;
    ScriptHashEntry *entry = scriptTable[hash];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->script;
        }
        entry = entry->next;
    }
    return NULL;
}

jq_state* findOrCreateJQ(const char *filter, Arena *arena) {
    JQHashEntry **table = getThreadJQTable(arena);
    uint32_t hash = hashString(filter) & HASH_MASK;
    JQHashEntry *entry = table[hash];
    
    // Look for existing entry
    while (entry) {
        if (strcmp(entry->filter, filter) == 0) {
            return entry->jq;
        }
        entry = entry->next;
    }
    
    // Create new entry
    entry = arenaAlloc(arena, sizeof(JQHashEntry));
    if (!entry) {
        fprintf(stderr, "Failed to allocate memory for JQ hash entry\n");
        return NULL;
    }

    // Duplicate the filter string to ensure we own the memory
    char *filter_copy = arenaDupString(arena, filter);
    if (!filter_copy) {
        fprintf(stderr, "Failed to duplicate filter string\n");
        return NULL;
    }
    entry->filter = filter_copy;

    entry->jq = jq_init();
    
    if (!entry->jq) {
        return NULL;
    }

    // Compile the filter
    int compiled = jq_compile(entry->jq, filter);
    if (!compiled) {
        jv error = jq_get_error_message(entry->jq);
        if (jv_is_valid(error)) {
            const char *error_msg = jv_string_value(error);
            fprintf(stderr, "JQ compilation error: %s\n", error_msg);
            jv_free(error);
        }
        jq_teardown(&entry->jq);
        return NULL;
    }

    // Add to hash table
    entry->next = table[hash];
    table[hash] = entry;
    
    return entry->jq;
}

void cleanupJQCache(void) {
    if (!threadJQTable) {
        return;
    }

    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        JQHashEntry *entry = threadJQTable[i];
        while (entry) {
            JQHashEntry *next = entry->next;
            
            if (entry->jq) {
                jq_teardown(&entry->jq);
            }
            
            entry = next;
        }
        threadJQTable[i] = NULL;
    }
}

RouteMatch findRoute(const char *url, const char *method, Arena *arena) {
    RouteMatch match = {
        .type = ROUTE_TYPE_NONE,
        .endpoint = {.api = NULL}  // Initialize union to prevent undefined behavior
    };
    memset(&match.params, 0, sizeof(RouteParams));
    
    // Try API first (maintaining current precedence)
    match.endpoint.api = findApi(url, method, &match.params, arena);
    if (match.endpoint.api) {
        match.type = ROUTE_TYPE_API;
        return match;
    }
    
    // Try page routes
    match.endpoint.page = findPage(url, &match.params, arena);
    if (match.endpoint.page) {
        match.type = ROUTE_TYPE_PAGE;
    }
    
    return match;
}

PartialNode* findPartial(const char *name) {
    uint32_t hash = hashString(name) & HASH_MASK;
    PartialHashEntry *entry = partialTable[hash];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->partial;
        }
        entry = entry->next;
    }
    return NULL;
}
