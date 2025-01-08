#include "lua.h"
#include "../arena.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../ast.h"
#include "utils.h"
#include "server.h"

#define LUA_HASH_TABLE_SIZE 64  // Should be power of 2
#define LUA_HASH_MASK (LUA_HASH_TABLE_SIZE - 1)

// Add arena wrapper struct
typedef struct {
    Arena *arena;
    size_t total_allocated;
} LuaArenaWrapper;

typedef struct {
    unsigned char* bytecode;
    size_t bytecode_len;
} LuaBytecode;

typedef struct LuaChunkEntry {
    const char* code;           // Original Lua code for comparison
    LuaBytecode bytecode;
    struct LuaChunkEntry* next;
} LuaChunkEntry;

// Add custom allocator function
static void* luaArenaAlloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    LuaArenaWrapper *wrapper = (LuaArenaWrapper*)ud;
    Arena *arena = wrapper->arena;

    // Free case
    if (nsize == 0) {
        // Avoid underflow when subtracting from total_allocated
        if (wrapper->total_allocated >= osize) {
            wrapper->total_allocated -= osize;
        } else {
            wrapper->total_allocated = 0;
        }
        return NULL;
    }

    // Allocation case
    if (ptr == NULL) {
        // Check for overflow before adding to total_allocated
        size_t new_total = wrapper->total_allocated;
        if (__builtin_add_overflow(new_total, nsize, &new_total)) {
            return NULL;  // Allocation would overflow
        }
        wrapper->total_allocated = new_total;
        return arenaAlloc(arena, nsize);
    }

    // Reallocation case
    void *new_ptr = arenaAlloc(arena, nsize);
    if (new_ptr) {
        // Copy the minimum of old and new sizes
        size_t copy_size = (osize < nsize) ? osize : nsize;
        if (ptr && copy_size > 0) {
            memcpy(new_ptr, ptr, copy_size);
        }
        
        // Update total allocated size
        if (nsize > osize) {
            size_t diff = nsize - osize;
            if (__builtin_add_overflow(wrapper->total_allocated, diff, &wrapper->total_allocated)) {
                wrapper->total_allocated = SIZE_MAX;  // Cap at maximum
            }
        } else if (osize > nsize) {
            size_t diff = osize - nsize;
            if (wrapper->total_allocated >= diff) {
                wrapper->total_allocated -= diff;
            } else {
                wrapper->total_allocated = 0;
            }
        }
    }
    return new_ptr;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
static struct {
    unsigned char* bytecode;
    size_t bytecode_len;
    bool is_initialized;
} QueryBuilder = {0};
#pragma clang diagnostic pop

// Modify bytecodeWriter to handle both cases
static int bytecodeWriter(lua_State *L __attribute__((unused)), 
                         const void* p, 
                         size_t sz, 
                         void* ud) {
    // Check if we're writing querybuilder or pipeline bytecode
    if (!ud) {
        // QueryBuilder case
        unsigned char* new_code = realloc(QueryBuilder.bytecode, 
                                        QueryBuilder.bytecode_len + sz);
        if (!new_code) return 1;
        
        memcpy(new_code + QueryBuilder.bytecode_len, p, sz);
        QueryBuilder.bytecode = new_code;
        QueryBuilder.bytecode_len += sz;
    } else {
        // Pipeline bytecode case
        LuaBytecode* bytecode = (LuaBytecode*)ud;
        unsigned char* new_code = realloc(bytecode->bytecode, 
                                        bytecode->bytecode_len + sz);
        if (!new_code) return 1;
        
        memcpy(new_code + bytecode->bytecode_len, p, sz);
        bytecode->bytecode = new_code;
        bytecode->bytecode_len += sz;
    }
    return 0;
}

static LuaChunkEntry* chunkTable[LUA_HASH_TABLE_SIZE] = {0};

// Add new function to walk AST and compile Lua steps
static bool compilePipelineSteps(ServerContext *ctx) {
    ApiEndpoint* endpoint = ctx->website->apiHead;
    while (endpoint) {
        PipelineStepNode* step = endpoint->pipeline;
        while (step) {
            if (step->type == STEP_LUA) {
                uint32_t hash = hashString(step->code) & LUA_HASH_MASK;
                
                // Check if already cached
                LuaChunkEntry* entry = chunkTable[hash];
                while (entry) {
                    if (strcmp(entry->code, step->code) == 0) {
                        break;
                    }
                    entry = entry->next;
                }
                
                if (!entry) {
                    // New chunk - compile and cache it
                    entry = malloc(sizeof(LuaChunkEntry));
                    entry->code = step->code;
                    entry->bytecode.bytecode = NULL;
                    entry->bytecode.bytecode_len = 0;
                    
                    lua_State *L = luaL_newstate();
                    if (!L) return false;
                    
                    luaL_openlibs(L);
                    
                    if (luaL_loadstring(L, step->code) != 0) {
                        lua_close(L);
                        free(entry);
                        return false;
                    }
                    
                    if (lua_dump(L, bytecodeWriter, &entry->bytecode, 0) != 0) {
                        lua_close(L);
                        free(entry);
                        return false;
                    }
                    
                    lua_close(L);
                    
                    // Add to hash table
                    entry->next = chunkTable[hash];
                    chunkTable[hash] = entry;
                }
            }
            step = step->next;
        }
        endpoint = endpoint->next;
    }
    return true;
}

// Modify initLua to also compile pipeline steps:
bool initLua(ServerContext *ctx) {
    // Keep existing querybuilder initialization
    lua_State *L = luaL_newstate();
    if (!L) return false;
    
    luaL_openlibs(L);
    
    // Load and compile querybuilder.lua
    if (luaL_loadfile(L, "src/server/querybuilder.lua") != 0) {
        fprintf(stderr, "Failed to load querybuilder.lua: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return false;
    }
    
    // Dump the compiled bytecode
    QueryBuilder.bytecode_len = 0;
    if (lua_dump(L, bytecodeWriter, NULL, 0) != 0) {
        fprintf(stderr, "Failed to dump querybuilder bytecode\n");
        free(QueryBuilder.bytecode);
        QueryBuilder.bytecode = NULL;
        lua_close(L);
        return false;
    }
    
    QueryBuilder.is_initialized = true;
    lua_close(L);

    // Add pipeline step compilation
    if (!compilePipelineSteps(ctx)) {
        cleanupLua();
        return false;
    }
    
    return true;
}

// Modify cleanupLua to also cleanup chunk table:
void cleanupLua(void) {
    // Keep existing querybuilder cleanup
    if (QueryBuilder.bytecode) {
        free(QueryBuilder.bytecode);
        QueryBuilder.bytecode = NULL;
        QueryBuilder.bytecode_len = 0;
        QueryBuilder.is_initialized = false;
    }

    // Cleanup chunk table
    for (int i = 0; i < LUA_HASH_TABLE_SIZE; i++) {
        LuaChunkEntry* entry = chunkTable[i];
        while (entry) {
            LuaChunkEntry* next = entry->next;
            free(entry->bytecode.bytecode);
            free(entry);
            entry = next;
        }
        chunkTable[i] = NULL;
    }
}

// Modify loadLuaFile to use cached bytecode
static bool loadLuaFile(lua_State *L, const char *filename) {
    // For querybuilder.lua, use cached bytecode
    if (strcmp(filename, "src/server/querybuilder.lua") == 0) {
        if (!QueryBuilder.is_initialized) {
            return false;
        }
        
        if (luaL_loadbuffer(L, (const char*)QueryBuilder.bytecode, 
                           QueryBuilder.bytecode_len, "querybuilder") != 0) {
            return false;
        }
        
        // Execute the loaded chunk to create the querybuilder table
        if (lua_pcall(L, 0, 1, 0) != 0) {
            return false;
        }
        
        lua_setglobal(L, "querybuilder");
        return true;
    }
    
    // For other files, load normally
    if (luaL_dofile(L, filename) == 0) {
        lua_setglobal(L, "querybuilder");
        return true;
    }
    return false;
}

static lua_State* createLuaState(json_t *requestContext, Arena *arena, bool loadQueryBuilder) {
    // Create and initialize arena wrapper
    LuaArenaWrapper *wrapper = arenaAlloc(arena, sizeof(LuaArenaWrapper));
    wrapper->arena = arena;
    wrapper->total_allocated = 0;

    // Create Lua state with custom allocator
    lua_State *L = lua_newstate(luaArenaAlloc, wrapper);
    if (!L) {
        return NULL;
    }

    lua_gc(L, LUA_GCSTOP, 0);

    luaL_openlibs(L);
    
    // Create tables for request context
    lua_newtable(L);  // Main request context table
    
    // Add query params
    lua_newtable(L);
    json_t *query = json_object_get(requestContext, "query");
    const char *key;
    json_t *value;
    json_object_foreach(query, key, value) {
        lua_pushstring(L, key);
        lua_pushstring(L, json_string_value(value));
        lua_settable(L, -3);
    }
    lua_setglobal(L, "query");
    
    // Add headers
    lua_newtable(L);
    json_t *headers = json_object_get(requestContext, "headers");
    json_object_foreach(headers, key, value) {
        lua_pushstring(L, key);
        lua_pushstring(L, json_string_value(value));
        lua_settable(L, -3);
    }
    lua_setglobal(L, "headers");
    
    // Add body params
    lua_newtable(L);
    json_t *body = json_object_get(requestContext, "body");
    json_object_foreach(body, key, value) {
        lua_pushstring(L, key);
        lua_pushstring(L, json_string_value(value));
        lua_settable(L, -3);
    }
    lua_setglobal(L, "body");
    
    // Load query builder if needed
    if (loadQueryBuilder) {
        if (!loadLuaFile(L, "src/server/querybuilder.lua")) {
            lua_close(L);
            return NULL;
        }
    }
    
    return L;
}

static void pushJsonToLua(lua_State *L, json_t *json) {
    if (!json) {
        lua_pushnil(L);
        return;
    }

    switch (json_typeof(json)) {
        case JSON_OBJECT: {
            lua_newtable(L);
            const char *key;
            json_t *value;
            json_object_foreach(json, key, value) {
                lua_pushstring(L, key);
                pushJsonToLua(L, value);
                lua_settable(L, -3);
            }
            break;
        }
        case JSON_ARRAY: {
            lua_newtable(L);
            size_t index;
            json_t *value;
            json_array_foreach(json, index, value) {
                lua_Integer lua_index = (lua_Integer)(index + 1);
                if (lua_index > 0) {
                    lua_pushinteger(L, lua_index);
                    pushJsonToLua(L, value);
                    lua_settable(L, -3);
                }
            }
            break;
        }
        case JSON_STRING:
            lua_pushstring(L, json_string_value(json));
            break;
        case JSON_INTEGER:
            lua_pushinteger(L, json_integer_value(json));
            break;
        case JSON_REAL:
            lua_pushnumber(L, json_real_value(json));
            break;
        case JSON_TRUE:
            lua_pushboolean(L, 1);
            break;
        case JSON_FALSE:
            lua_pushboolean(L, 0);
            break;
        case JSON_NULL:
            lua_pushnil(L);
            break;
    }
}

static json_t* luaToJson(lua_State *L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TTABLE: {
            bool isArray = true;
            lua_Integer maxIndex = 0;
            
            lua_pushnil(L);
            while (lua_next(L, index - 1) != 0) {
                if (lua_type(L, -2) != LUA_TNUMBER) {
                    isArray = false;
                } else {
                    lua_Integer i = lua_tointeger(L, -2);
                    if (i > 0 && i > maxIndex) {
                        maxIndex = i;
                    } else {
                        isArray = false;
                    }
                }
                lua_pop(L, 1);
            }
            
            if (isArray && maxIndex > 0) {
                json_t *arr = json_array();
                for (lua_Integer i = 1; i <= maxIndex; i++) {
                    lua_rawgeti(L, index, i);
                    json_t *value = luaToJson(L, -1);
                    json_array_append_new(arr, value);
                    lua_pop(L, 1);
                }
                return arr;
            } else {
                json_t *obj = json_object();
                lua_pushnil(L);
                while (lua_next(L, index - 1) != 0) {
                    const char *key = lua_tostring(L, -2);
                    json_t *value = luaToJson(L, -1);
                    if (key) {
                        json_object_set_new(obj, key, value);
                    }
                    lua_pop(L, 1);
                }
                return obj;
            }
        }
        case LUA_TSTRING:
            return json_string(lua_tostring(L, index));
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) {
                return json_integer(lua_tointeger(L, index));
            }
            return json_real(lua_tonumber(L, index));
        case LUA_TBOOLEAN:
            return lua_toboolean(L, index) ? json_true() : json_false();
        case LUA_TNIL:
            return json_null();
        default:
            return json_null();
    }
}

json_t* executeLuaStep(PipelineStepNode *step, json_t *input, json_t *requestContext, Arena *arena, ServerContext *serverCtx) {
    (void)serverCtx;
    // Check for existing error
    json_t *error = json_object_get(input, "error");
    if (error) {
        return json_deep_copy(input);
    }

    lua_State *L = createLuaState(input, arena, step->is_dynamic);
    if (!L) {
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string("Failed to create Lua state"));
        return result;
    }
    
    // Set up input from previous step
    pushJsonToLua(L, input);
    lua_setglobal(L, "request");
    
    // Set up globals from the original request context
    json_t *query = json_object_get(requestContext, "query");
    json_t *body = json_object_get(requestContext, "body");
    json_t *headers = json_object_get(requestContext, "headers");
    
    // Create query table
    lua_newtable(L);
    const char *key;
    json_t *value;
    json_object_foreach(query, key, value) {
        lua_pushstring(L, key);
        lua_pushstring(L, json_string_value(value));
        lua_settable(L, -3);
    }
    lua_setglobal(L, "query");
    
    // Create body table
    lua_newtable(L);
    json_object_foreach(body, key, value) {
        lua_pushstring(L, key);
        lua_pushstring(L, json_string_value(value));
        lua_settable(L, -3);
    }
    lua_setglobal(L, "body");
    
    // Create headers table
    lua_newtable(L);
    json_object_foreach(headers, key, value) {
        lua_pushstring(L, key);
        lua_pushstring(L, json_string_value(value));
        lua_settable(L, -3);
    }
    lua_setglobal(L, "headers");
    
    // Find cached bytecode
    uint32_t hash = hashString(step->code) & LUA_HASH_MASK;
    LuaChunkEntry* entry = chunkTable[hash];
    while (entry) {
        if (strcmp(entry->code, step->code) == 0) {
            break;
        }
        entry = entry->next;
    }
    
    if (!entry) {
        lua_close(L);
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string("Failed to find cached Lua bytecode"));
        return result;
    }
    
    // Load and execute
    if (luaL_loadbuffer(L, (const char*)entry->bytecode.bytecode, 
                       entry->bytecode.bytecode_len, "step") != 0) {
        const char *error_msg = lua_tostring(L, -1);
        lua_close(L);
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string(error_msg));
        return result;
    }
    
    if (lua_pcall(L, 0, 1, 0) != 0) {
        const char *error_msg = lua_tostring(L, -1);
        lua_close(L);
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string(error_msg));
        return result;
    }
    
    json_t *result = luaToJson(L, -1);
    lua_close(L);
    
    if (!result) {
        json_t *error_result = json_object();
        json_object_set_new(error_result, "error", json_string("Failed to convert Lua result to JSON"));
        return error_result;
    }
    
    // Merge input properties into result
    if (input) {
        json_object_update(result, input);
    }
    
    return result;
}
