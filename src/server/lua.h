#ifndef LUA_H
#define LUA_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "../ast.h"
#include "../arena.h"
#include "server.h"
#include <time.h>
#include <curl/curl.h>

// Structure to hold bytecode
typedef struct {
    unsigned char* bytecode;
    size_t bytecode_len;
} LuaBytecode;

// Structure for a loaded Lua file
typedef struct {
    char *filename;           // Full path relative to workspace
    LuaBytecode bytecode;    // Compiled bytecode
    time_t last_modified;    // For cache invalidation
} LuaFileEntry;

// Registry of all loaded Lua files
typedef struct {
    LuaFileEntry *entries;
    size_t count;
    size_t capacity;
} LuaFileRegistry;

// HTTP response buffer structure
typedef struct {
    char *data;
    size_t size;
} ResponseBuffer;

// Initialize Lua subsystem
bool initLua(ServerContext *ctx);

// Clean up Lua subsystem
void cleanupLua(void);

// Execute a Lua pipeline step
json_t* executeLuaStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, ServerContext *ctx);

// Register HTTP functions with Lua state
void registerHttpFunctions(lua_State *L);

// Register database functions with Lua state
void registerDbFunctions(lua_State *L);

#endif
