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
#include "db.h"
#include "generated_scripts.h"

#define LUA_HASH_TABLE_SIZE 64  // Should be power of 2
#define LUA_HASH_MASK (LUA_HASH_TABLE_SIZE - 1)
#define INITIAL_REGISTRY_CAPACITY 16
#define SCRIPTS_DIR "scripts"

// Forward declarations
static void pushJsonToLua(lua_State *L, json_t *json);
static json_t* luaToJson(lua_State *L, int index);
static bool loadEmbeddedScripts(lua_State *L);

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

static struct ServerContext *g_ctx = NULL;  // Rename global ctx to g_ctx

// Add typedef for Page
typedef struct PageNode Page;

// Write callback for curl
static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    ResponseBuffer *mem = (ResponseBuffer *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        return 0;  // Out of memory
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

// Lua function to make HTTP requests
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
static int lua_fetch(lua_State *L) {
    // Get URL from first argument
    const char *url = luaL_checkstring(L, 1);
    
    // Get options table from second argument (optional)
    const char *method = "GET";
    const char *body = NULL;
    struct curl_slist *headers = NULL;
    
    if (lua_gettop(L) >= 2) {
        luaL_checktype(L, 2, LUA_TTABLE);
        
        // Get method
        lua_getfield(L, 2, "method");
        if (!lua_isnil(L, -1)) {
            method = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        
        // Get body
        lua_getfield(L, 2, "body");
        if (!lua_isnil(L, -1)) {
            if (lua_istable(L, -1)) {
                // Convert table to JSON string
                json_t *json_body = luaToJson(L, -1);
                body = json_dumps(json_body, 0);
                json_decref(json_body);
            } else {
                body = lua_tostring(L, -1);
            }
        }
        lua_pop(L, 1);
        
        // Get headers
        lua_getfield(L, 2, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                const char *key = lua_tostring(L, -2);
                const char *value = lua_tostring(L, -1);
                char *header = malloc(strlen(key) + strlen(value) + 3);
                snprintf(header, sizeof(header), "%s: %s", key, value);
                headers = curl_slist_append(headers, header);
                free(header);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    
    // Initialize CURL
    CURL *curl = curl_easy_init();
    if (!curl) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to initialize CURL");
        return 2;
    }
    
    // Set up response buffer
    ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;
    
    // Set up request
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
    
    // Set method and body
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    }
    
    // Set headers
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up
    if (headers) {
        curl_slist_free_all(headers);
    }
    if (body) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wcast-qual"
        free((void*)body);
        #pragma clang diagnostic pop
    }
    
    if (res != CURLE_OK) {
        const char *error_msg = curl_easy_strerror(res);
        lua_pushnil(L);
        lua_pushstring(L, error_msg);
        free(response.data);
        curl_easy_cleanup(curl);
        return 2;
    }
    
    // Get status code
    long status_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    
    // Clean up curl
    curl_easy_cleanup(curl);
    
    // Try to parse response as JSON
    json_error_t error;
    json_t *json = json_loads(response.data, 0, &error);
    
    // Create response table
    lua_createtable(L, 0, 3);
    
    // Add status
    lua_pushinteger(L, status_code);
    lua_setfield(L, -2, "status");
    
    // Add ok
    lua_pushboolean(L, status_code >= 200 && status_code < 300);
    lua_setfield(L, -2, "ok");
    
    // Add body
    if (json) {
        // If JSON parsing succeeded, convert to Lua table
        pushJsonToLua(L, json);
        json_decref(json);
    } else {
        // If not JSON, return as string
        lua_pushstring(L, response.data);
    }
    lua_setfield(L, -2, "body");
    
    free(response.data);
    return 1;
}
#pragma clang diagnostic pop

// Register HTTP functions with Lua state
void registerHttpFunctions(lua_State *L) {
    lua_pushcfunction(L, lua_fetch);
    lua_setglobal(L, "fetch");
}

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
            size_t new_capacity = fileRegistry.capacity > 0 ? fileRegistry.capacity * 2 : 1;
            LuaFileEntry* new_entries = realloc(fileRegistry.entries, new_capacity * sizeof(LuaFileEntry));
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
        // Scripts directory doesn't exist - that's fine, continue with embedded scripts
        return true;
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

// Helper function to compile pipeline steps
static bool compilePipelineStepsHelper(PipelineStepNode* step) {
    // Validate pointer alignment
    if (!step || ((uintptr_t)step & 7) != 0) return true;  // Skip if NULL or misaligned
    
    while (step && ((uintptr_t)step & 7) == 0) {  // Check alignment in loop too
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
    return true;
}

static bool compilePipelineSteps(ServerContext *server_ctx) {
    if (!server_ctx || !server_ctx->website) {
        return false;
    }

    // First compile all named scripts
    ScriptNode* script = server_ctx->website->scriptHead;
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

    // Compile API endpoint pipeline steps
    ApiEndpoint* endpoint = server_ctx->website->apiHead;
    while (endpoint) {
        if (endpoint->pipeline && !compilePipelineStepsHelper(endpoint->pipeline)) {
            return false;
        }
        endpoint = endpoint->next;
    }

    // Compile page pipeline steps
    Page* page = server_ctx->website->pageHead;
    while (page) {
        if (page->pipeline && !compilePipelineStepsHelper(page->pipeline)) {
            return false;
        }
        page = page->next;
    }

    return true;
}

// SQL query function implementation
static int lua_sqlQuery(lua_State *L) {
    // Get SQL query from first argument
    const char *sql = luaL_checkstring(L, 1);
    
    // Get params table from second argument (optional)
    size_t param_count = 0;
    const char **params = NULL;
    
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        
        // Count params
        lua_len(L, 2);
        lua_Integer len = lua_tointeger(L, -1);
        // Check that len is positive and not larger than SIZE_MAX
        if (len > 0 && (lua_Unsigned)len <= SIZE_MAX) {
            param_count = (size_t)len;
        }
        lua_pop(L, 1);
        
        if (param_count > 0) {
            // Allocate params array
            params = malloc(sizeof(char *) * param_count);
            if (!params) {
                return luaL_error(L, "Failed to allocate memory for parameters");
            }
            
            // Extract params from table
            for (lua_Integer i = 0; i < (lua_Integer)param_count; i++) {
                lua_rawgeti(L, 2, i + 1);
                if (lua_isnil(L, -1)) {
                    params[i] = NULL;
                } else {
                    params[i] = lua_tostring(L, -1);
                }
                lua_pop(L, 1);
            }
        }
    }
    
    // Execute query
    json_t *result = executeSqlWithParams(g_ctx->db, sql, params, param_count);
    free(params);
    
    if (!result) {
        return luaL_error(L, "Failed to execute SQL query");
    }
    
    // Convert result to Lua table
    pushJsonToLua(L, result);
    json_decref(result);
    
    return 1;
}

// Find SQL query by name function
static int lua_findQuery(lua_State *L) {
    // Get query name from first argument
    const char *name = luaL_checkstring(L, 1);
    
    // Find the query
    QueryNode *query = findQuery(name);
    if (!query || !query->sql) {
        lua_pushnil(L);
        return 1;
    }
    
    // Return the SQL string
    lua_pushstring(L, query->sql);
    return 1;
}

// Register database functions with Lua state
void registerDbFunctions(lua_State *L) {
    lua_pushcfunction(L, lua_sqlQuery);
    lua_setglobal(L, "sqlQuery");
    
    lua_pushcfunction(L, lua_findQuery);
    lua_setglobal(L, "findQuery");
}

// Add global cache for embedded scripts
static struct {
    char **script_buffers;  // Array of script buffers
    size_t script_count;
    const char **script_names;  // Array of script names
} g_embedded_scripts = {0};

static bool loadEmbeddedScripts(lua_State *L) {
    // If scripts are already loaded, just set them as globals
    if (g_embedded_scripts.script_buffers) {
        for (size_t i = 0; i < g_embedded_scripts.script_count; i++) {
            const char *name = g_embedded_scripts.script_names[i];
            const char *buffer = g_embedded_scripts.script_buffers[i];
            
            // Load and execute the script
            if (luaL_loadstring(L, buffer) != 0) {
                fprintf(stderr, "Failed to load embedded script %s: %s\n", 
                        name, lua_tostring(L, -1));
                return false;
            }

            // Execute the chunk to get the module table
            if (lua_pcall(L, 0, 1, 0) != 0) {
                fprintf(stderr, "Failed to execute embedded script %s: %s\n",
                        name, lua_tostring(L, -1));
                return false;
            }

            // Set the module table as a global with the script name
            lua_setglobal(L, name);
        }
        return true;
    }

    // First time loading - count scripts
    size_t script_count = 0;
    for (const EmbeddedScript *script = EMBEDDED_SCRIPTS; script->name != NULL; script++) {
        script_count++;
    }

    // Allocate arrays
    g_embedded_scripts.script_buffers = malloc(script_count * sizeof(char*));
    g_embedded_scripts.script_names = malloc(script_count * sizeof(char*));
    if (!g_embedded_scripts.script_buffers || !g_embedded_scripts.script_names) {
        fprintf(stderr, "Failed to allocate script arrays\n");
        return false;
    }
    g_embedded_scripts.script_count = script_count;

    // Load each script
    size_t script_idx = 0;
    for (const EmbeddedScript *script = EMBEDDED_SCRIPTS; script->name != NULL; script++) {
        // Calculate total length
        size_t total_len = 0;
        for (size_t i = 0; i < script->num_chunks; i++) {
            total_len += strlen(script->chunks[i]);
        }

        // Allocate buffer
        char *buffer = malloc(total_len + 1);
        if (!buffer) {
            fprintf(stderr, "Failed to allocate buffer for embedded script %s\n", script->name);
            return false;
        }

        // Concatenate chunks
        char *ptr = buffer;
        for (size_t i = 0; i < script->num_chunks; i++) {
            size_t len = strlen(script->chunks[i]);
            memcpy(ptr, script->chunks[i], len);
            ptr += len;
        }
        *ptr = '\0';

        // Store in global cache
        g_embedded_scripts.script_buffers[script_idx] = buffer;
        g_embedded_scripts.script_names[script_idx] = script->name;

        // Load and execute the script
        if (luaL_loadstring(L, buffer) != 0) {
            fprintf(stderr, "Failed to load embedded script %s: %s\n", 
                    script->name, lua_tostring(L, -1));
            return false;
        }

        // Execute the chunk to get the module table
        if (lua_pcall(L, 0, 1, 0) != 0) {
            fprintf(stderr, "Failed to execute embedded script %s: %s\n",
                    script->name, lua_tostring(L, -1));
            return false;
        }

        // Set the module table as a global with the script name
        lua_setglobal(L, script->name);
        
        script_idx++;
    }

    return true;
}

// Modify cleanupLua to cleanup embedded scripts:
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

    // Cleanup embedded scripts
    if (g_embedded_scripts.script_buffers) {
        for (size_t i = 0; i < g_embedded_scripts.script_count; i++) {
            free(g_embedded_scripts.script_buffers[i]);
        }
        free(g_embedded_scripts.script_buffers);
        free((void*)g_embedded_scripts.script_names);
        g_embedded_scripts.script_buffers = NULL;
        g_embedded_scripts.script_names = NULL;
        g_embedded_scripts.script_count = 0;
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

static lua_State* createLuaState(json_t *requestContext, Arena *arena) {
    // Create and initialize arena wrapper
    LuaArenaWrapper *wrapper = arenaAlloc(arena, sizeof(LuaArenaWrapper));
    if (!wrapper) {
        fprintf(stderr, "Failed to allocate arena wrapper\n");
        return NULL;
    }
    wrapper->arena = arena;
    wrapper->total_allocated = 0;

    // Create Lua state with custom allocator
    lua_State *L = lua_newstate(luaArenaAlloc, wrapper);
    if (!L) {
        fprintf(stderr, "Failed to create new Lua state\n");
        return NULL;
    }

    // Disable GC since we're using arena allocation and states are short-lived
    lua_gc(L, LUA_GCSTOP, 0);

    luaL_openlibs(L);
    
    // Register our functions
    registerHttpFunctions(L);
    registerDbFunctions(L);
    
    // Load embedded scripts into this state
    if (!loadEmbeddedScripts(L)) {
        fprintf(stderr, "Failed to load embedded scripts\n");
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

    // Add params
    lua_newtable(L);
    json_t *params = json_object_get(requestContext, "params");
    const char *params_key;
    json_t *params_value;
    json_object_foreach(params, params_key, params_value) {
        lua_pushstring(L, params_key);
        lua_pushstring(L, json_string_value(params_value));
        lua_settable(L, -3);
    }
    lua_setglobal(L, "params");
    
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

    lua_State *L = createLuaState(input, arena);
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

// Modify initLua to store context and register DB functions
bool initLua(ServerContext *server_ctx) {
    g_ctx = server_ctx;  // Store in global
    
    if (!initFileRegistry()) {
        return false;
    }

    lua_State *L = luaL_newstate();
    if (!L) return false;
    
    luaL_openlibs(L);
    
    // Register our functions
    registerHttpFunctions(L);
    registerDbFunctions(L);
    
    // First load embedded scripts
    if (!loadEmbeddedScripts(L)) {
        lua_close(L);
        cleanupLua();
        return false;
    }
    
    // Then load filesystem scripts (which can override embedded ones)
    if (!loadAllScripts(L)) {
        lua_close(L);
        cleanupLua();
        return false;
    }

    lua_close(L);

    // Add pipeline step compilation
    if (!compilePipelineSteps(server_ctx)) {
        cleanupLua();
        return false;
    }
    
    return true;
}
