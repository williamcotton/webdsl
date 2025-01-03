#include "lua.h"
#include "../arena.h"
#include "utils.h"
#include <string.h>

// Add arena wrapper struct
typedef struct {
    Arena *arena;
    size_t total_allocated;
} LuaArenaWrapper;

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

lua_State* createLuaState(json_t *requestContext, Arena *arena) {
    // Create and initialize arena wrapper
    LuaArenaWrapper *wrapper = arenaAlloc(arena, sizeof(LuaArenaWrapper));
    wrapper->arena = arena;
    wrapper->total_allocated = 0;

    // Create Lua state with custom allocator
    lua_State *L = lua_newstate(luaArenaAlloc, wrapper);
    if (!L) {
        return NULL;
    }

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
    
    return L;
}

void pushJsonToLua(lua_State *L, json_t *json) {
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

json_t* luaToJson(lua_State *L, int index) {
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

void extractLuaValues(lua_State *L, Arena *arena, const char ***values, size_t *value_count) {
    if (!lua_istable(L, -1)) {
        *values = NULL;
        *value_count = 0;
        return;
    }
    
    // Count table elements
    size_t count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        count++;
        lua_pop(L, 1);
    }
    
    if (count == 0) {
        *values = NULL;
        *value_count = 0;
        return;
    }

    *value_count = count;
    *values = arenaAlloc(arena, sizeof(char*) * count);
    
    size_t index = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        const char *value = lua_tostring(L, -1);
        if (value) {
            (*values)[index++] = arenaDupString(arena, value);
        }
        lua_pop(L, 1);
    }
}

char* handleLuaPreFilter(Arena *arena, json_t *requestContext, const char *luaScript,
                        const char ***values, size_t *value_count) {
    lua_State *L = createLuaState(requestContext, arena);
    if (!L) {
        return generateErrorJson("Failed to create Lua state");
    }
    
    if (luaL_dostring(L, luaScript) != 0) {
        const char *error = lua_tostring(L, -1);
        char *error_copy = arenaDupString(arena, error);
        lua_close(L);
        return generateErrorJson(error_copy);
    }
    
    if (lua_istable(L, -1)) {
        extractLuaValues(L, arena, values, value_count);
    }
    lua_close(L);
    
    return NULL;
}

char* handleLuaPostFilter(Arena *arena, json_t *jsonData, json_t *requestContext, 
                         const char *luaScript) {
    lua_State *L = createLuaState(requestContext, arena);
    if (!L) {
        return generateErrorJson("Failed to create Lua state");
    }
    
    json_t *rows = json_object_get(jsonData, "rows");
    if (rows) {
        pushJsonToLua(L, rows);
        lua_setglobal(L, "rows");
    }
    
    if (luaL_dostring(L, luaScript) != 0) {
        const char *error = lua_tostring(L, -1);
        char *error_copy = arenaDupString(arena, error);
        lua_close(L);
        return generateErrorJson(error_copy);
    }
    
    json_t *result = luaToJson(L, -1);
    lua_close(L);
    
    if (!result) {
        return generateErrorJson("Failed to convert Lua result to JSON");
    }
    
    char *response = json_dumps(result, JSON_COMPACT);
    json_decref(result);
    return response;
}
