#include "routing.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

static RouteHashEntry *routeTable[HASH_TABLE_SIZE];
static LayoutHashEntry *layoutTable[HASH_TABLE_SIZE];
static ApiHashEntry *apiTable[HASH_TABLE_SIZE];
static QueryHashEntry *queryTable[HASH_TABLE_SIZE];

static uint32_t hashString(const char *str) __attribute__((no_sanitize("unsigned-integer-overflow"))) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

void buildRouteMaps(WebsiteNode *website, Arena *arena) {
    memset(routeTable, 0, sizeof(routeTable));
    memset(layoutTable, 0, sizeof(layoutTable));
    memset(apiTable, 0, sizeof(apiTable));
    memset(queryTable, 0, sizeof(queryTable));

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
}

PageNode* findPage(const char *url) {
    uint32_t hash = hashString(url) & HASH_MASK;
    RouteHashEntry *entry = routeTable[hash];
    
    while (entry) {
        if (strcmp(entry->route, url) == 0) {
            return entry->page;
        }
        entry = entry->next;
    }
    return NULL;
}

LayoutNode* findLayout(const char *identifier) {
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

ApiEndpoint* findApi(const char *url, const char *method) {
    uint32_t hash = hashString(url);
    hash ^= hashString(method);
    hash &= HASH_MASK;
    ApiHashEntry *entry = apiTable[hash];
    
    while (entry) {
        if (strcmp(entry->route, url) == 0 && strcmp(entry->method, method) == 0) {
            return entry->endpoint;
        }
        entry = entry->next;
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
