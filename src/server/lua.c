#include "lua.h"
#include "../arena.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../ast.h"
#include "utils.h"
#include "server.h"
#include "routing.h"

#define LUA_HASH_TABLE_SIZE 64  // Should be power of 2
#define LUA_HASH_MASK (LUA_HASH_TABLE_SIZE - 1)
#define INITIAL_REGISTRY_CAPACITY 16
#define SCRIPTS_DIR "src/server/scripts"

// Add arena wrapper struct
typedef struct {
    Arena *arena;
    size_t total_allocated;
} LuaArenaWrapper;

typedef struct LuaChunkEntry {
    const char* code;           // Original Lua code for comparison
    LuaBytecode bytecode;
    struct LuaChunkEntry* next;
} LuaChunkEntry;

static LuaFileRegistry fileRegistry = {0};
static LuaChunkEntry* chunkTable[LUA_HASH_TABLE_SIZE] = {0};

// Modify bytecodeWriter to handle both cases
static int bytecodeWriter(lua_State *L __attribute__((unused)), 
                         const void* p, 
                         size_t sz, 
                         void* ud) {
    LuaBytecode* bytecode = (LuaBytecode*)ud;
    unsigned char* new_code = realloc(bytecode->bytecode, 
                                    bytecode->bytecode_len + sz);
    if (!new_code) return 1;
    
    memcpy(new_code + bytecode->bytecode_len, p, sz);
    bytecode->bytecode = new_code;
    bytecode->bytecode_len += sz;
    return 0;
}

// Initialize the file registry
static bool initFileRegistry(void) {
    fileRegistry.entries = malloc(INITIAL_REGISTRY_CAPACITY * sizeof(LuaFileEntry));
    if (!fileRegistry.entries) return false;
    fileRegistry.capacity = INITIAL_REGISTRY_CAPACITY;
    fileRegistry.count = 0;
    return true;
}

// Get base name from file path and remove extension
static char* getModuleName(const char* filepath) {
    const char* filename = strrchr(filepath, '/');
    if (!filename) {
        filename = filepath;
    } else {
        filename++; // Skip the '/'
    }
    
    char* moduleName = strdup(filename);
    if (!moduleName) return NULL;
    
    // Remove .lua extension if present
    char* dot = strrchr(moduleName, '.');
    if (dot) *dot = '\0';
    
    return moduleName;
}

// Load and compile a Lua file
static bool loadLuaFile(lua_State *L, const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return false;
    }

    // Check if we already have this file loaded and it's up to date
    for (size_t i = 0; i < fileRegistry.count; i++) {
        if (strcmp(fileRegistry.entries[i].filename, filepath) == 0) {
            if (fileRegistry.entries[i].last_modified >= st.st_mtime) {
                // File hasn't changed, use cached bytecode
                if (luaL_loadbuffer(L, 
                    (const char*)fileRegistry.entries[i].bytecode.bytecode,
                    fileRegistry.entries[i].bytecode.bytecode_len,
                    filepath) != 0) {
                    return false;
                }
                return true;
            }
            break;
        }
    }

    // Load and compile the file
    if (luaL_loadfile(L, filepath) != 0) {
        fprintf(stderr, "Failed to load %s: %s\n", filepath, lua_tostring(L, -1));
        return false;
    }

    // Create new entry or reuse existing one
    LuaFileEntry* entry = NULL;
    for (size_t i = 0; i < fileRegistry.count; i++) {
        if (strcmp(fileRegistry.entries[i].filename, filepath) == 0) {
            entry = &fileRegistry.entries[i];
            free(entry->bytecode.bytecode);
            break;
        }
    }

    if (!entry) {
        // Need to add new entry
        if (fileRegistry.count >= fileRegistry.capacity) {
            size_t new_capacity = fileRegistry.capacity * 2;
            LuaFileEntry* new_entries = realloc(fileRegistry.entries, 
                                              new_capacity * sizeof(LuaFileEntry));
            if (!new_entries) return false;
            fileRegistry.entries = new_entries;
            fileRegistry.capacity = new_capacity;
        }
        entry = &fileRegistry.entries[fileRegistry.count++];
        entry->filename = strdup(filepath);
        if (!entry->filename) return false;
    }

    entry->bytecode.bytecode = NULL;
    entry->bytecode.bytecode_len = 0;
    entry->last_modified = st.st_mtime;

    // Compile to bytecode
    if (lua_dump(L, bytecodeWriter, &entry->bytecode, 0) != 0) {
        fprintf(stderr, "Failed to compile %s\n", filepath);
        return false;
    }

    return true;
}

// Load all Lua files from scripts directory
static bool loadAllScripts(lua_State *L) {
    DIR *dir = opendir(SCRIPTS_DIR);
    if (!dir) {
        fprintf(stderr, "Failed to open scripts directory: %s\n", SCRIPTS_DIR);
        return false;
    }

    bool success = true;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // Regular file
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".lua") == 0) {
                char filepath[PATH_MAX];
                snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTS_DIR, entry->d_name);
                
                if (loadLuaFile(L, filepath)) {
                    // Execute the chunk and store it in a global with the module name
                    if (lua_pcall(L, 0, 1, 0) == 0) {
                        char* moduleName = getModuleName(filepath);
                        if (moduleName) {
                            lua_setglobal(L, moduleName);
                            free(moduleName);
                        } else {
                            success = false;
                            break;
                        }
                    } else {
                        fprintf(stderr, "Failed to execute %s: %s\n", 
                                filepath, lua_tostring(L, -1));
                        success = false;
                        break;
                    }
                } else {
                    success = false;
                    break;
                }
            }
        }
    }

    closedir(dir);
    return success;
}

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

// Add new function to walk AST and compile Lua steps
static bool compilePipelineSteps(ServerContext *ctx) {
    // First compile all named scripts
    ScriptNode* script = ctx->website->scriptHead;
    while (script) {
        if (script->type == FILTER_LUA) {
            uint32_t hash = hashString(script->code) & LUA_HASH_MASK;
            
            // Check if already cached
            LuaChunkEntry* entry = chunkTable[hash];
            while (entry) {
                if (strcmp(entry->code, script->code) == 0) {
                    break;
                }
                entry = entry->next;
            }
            
            if (!entry) {
                // New chunk - compile and cache it
                entry = malloc(sizeof(LuaChunkEntry));
                if (!entry) return false;
                
                entry->code = script->code;
                entry->bytecode.bytecode = NULL;
                entry->bytecode.bytecode_len = 0;
                
                lua_State *L = luaL_newstate();
                if (!L) {
                    free(entry);
                    return false;
                }
                
                luaL_openlibs(L);
                
                if (luaL_loadstring(L, script->code) != 0) {
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
        script = script->next;
    }

    // Then compile pipeline steps (existing)
    ApiEndpoint* endpoint = ctx->website->apiHead;
    while (endpoint) {
        PipelineStepNode* step = endpoint->pipeline;
        while (step) {
            if (step->type == STEP_LUA) {
                const char* code = step->code;
                if (step->name) {
                    // For named scripts, get code from script block
                    ScriptNode* namedScript = findScript(step->name);
                    if (!namedScript) {
                        fprintf(stderr, "Script not found: %s\n", step->name);
                        return false;
                    }
                    code = namedScript->code;
                }
                
                if (!code) continue;
                
                uint32_t hash = hashString(code) & LUA_HASH_MASK;
                
                // Check if already cached
                LuaChunkEntry* entry = chunkTable[hash];
                while (entry) {
                    if (strcmp(entry->code, code) == 0) {
                        break;
                    }
                    entry = entry->next;
                }
                
                if (!entry) {
                    // New chunk - compile and cache it
                    entry = malloc(sizeof(LuaChunkEntry));
                    if (!entry) return false;
                    
                    entry->code = code;
                    entry->bytecode.bytecode = NULL;
                    entry->bytecode.bytecode_len = 0;
                    
                    lua_State *L = luaL_newstate();
                    if (!L) {
                        free(entry);
                        return false;
                    }
                    
                    luaL_openlibs(L);
                    
                    if (luaL_loadstring(L, code) != 0) {
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

// Modify initLua to use new loading system:
bool initLua(ServerContext *ctx) {
    if (!initFileRegistry()) {
        return false;
    }

    lua_State *L = luaL_newstate();
    if (!L) return false;
    
    luaL_openlibs(L);
    
    // Load all Lua scripts from scripts directory
    if (!loadAllScripts(L)) {
        lua_close(L);
        cleanupLua();
        return false;
    }

    lua_close(L);

    // Add pipeline step compilation
    if (!compilePipelineSteps(ctx)) {
        cleanupLua();
        return false;
    }
    
    return true;
}

// Modify cleanupLua to cleanup file registry:
void cleanupLua(void) {
    // Cleanup file registry
    for (size_t i = 0; i < fileRegistry.count; i++) {
        free(fileRegistry.entries[i].filename);
        free(fileRegistry.entries[i].bytecode.bytecode);
    }
    free(fileRegistry.entries);
    fileRegistry.entries = NULL;
    fileRegistry.count = 0;
    fileRegistry.capacity = 0;

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

// Load cached scripts into a Lua state
static bool loadCachedScripts(lua_State *L) {
    for (size_t i = 0; i < fileRegistry.count; i++) {
        LuaFileEntry* entry = &fileRegistry.entries[i];
        
        // Load the bytecode
        if (luaL_loadbuffer(L, 
            (const char*)entry->bytecode.bytecode,
            entry->bytecode.bytecode_len,
            entry->filename) != 0) {
            fprintf(stderr, "Failed to load cached script %s\n", entry->filename);
            return false;
        }
        
        // Execute the chunk
        if (lua_pcall(L, 0, 1, 0) != 0) {
            fprintf(stderr, "Failed to execute cached script %s: %s\n", 
                    entry->filename, lua_tostring(L, -1));
            return false;
        }
        
        // Set as global with module name
        char* moduleName = getModuleName(entry->filename);
        if (moduleName) {
            lua_setglobal(L, moduleName);
            free(moduleName);
        } else {
            return false;
        }
    }
    return true;
}

static lua_State* createLuaState(json_t *requestContext, Arena *arena, bool loadQueryBuilder __attribute__((unused))) {
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
    
    // Load all cached scripts into this state
    if (!loadCachedScripts(L)) {
        lua_close(L);
        return NULL;
    }
    
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

    // Get code from named script if specified
    const char* code = step->code;
    if (step->name) {
        ScriptNode* namedScript = findScript(step->name);
        if (!namedScript) {
            json_t *result = json_object();
            json_object_set_new(result, "error", json_string("Script not found"));
            return result;
        }
        code = namedScript->code;
    }

    if (!code) {
        json_t *result = json_object();
        json_object_set_new(result, "error", json_string("No script code found"));
        return result;
    }

    // Check if script needs querybuilder
    bool needs_querybuilder = strstr(code, "querybuilder") != NULL;
    
    lua_State *L = createLuaState(input, arena, needs_querybuilder);
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
    
    // Find cached bytecode using the actual code
    uint32_t hash = hashString(code) & LUA_HASH_MASK;
    LuaChunkEntry* entry = chunkTable[hash];
    while (entry) {
        if (strcmp(entry->code, code) == 0) {
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
