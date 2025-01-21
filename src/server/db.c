#include "db.h"
#include "db_pool.h"
#include "server.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <uthash.h>
#include <stdatomic.h>
#include "server/utils.h"
#include "routing.h"

static struct ServerContext *ctx = NULL;

void initDb(struct ServerContext *serverCtx) {
    ctx = serverCtx;
}

// Generate unique statement names
static atomic_ulong stmt_counter = 0;

typedef struct {
    Database *db;
    PGconn *conn;
    PooledConnection *pooled;
} DbConnection;

static DbConnection getDbConnection(Database *db) {
    DbConnection conn = {db, NULL, NULL};
    if (!db || !db->pool) {
        return conn;
    }
    
    conn.pooled = getConnection(db->pool);
    if (conn.pooled) {
        conn.conn = conn.pooled->conn;
    }
    return conn;
}

static void releaseConnection(DbConnection *conn) {
    if (!conn || !conn->db || !conn->pooled) {
        return;
    }
    returnConnection(conn->db->pool, conn->pooled);
    conn->conn = NULL;
    conn->pooled = NULL;
}

static PreparedStmt* findPreparedStmt(Database *db, PGconn *conn, const char *sql) {
    uint32_t hash = hashString(sql) & STMT_HASH_MASK;
    PreparedStmt *stmt = db->stmt_cache[hash];
    
    while (stmt) {
        if (stmt->conn == conn && strcmp(stmt->sql, sql) == 0) {
            return stmt;
        }
        stmt = stmt->next;
    }
    
    return NULL;
}

static PreparedStmt* prepareSqlStatement(Database *db, PGconn *conn, const char *sql) {
    PreparedStmt *stmt = NULL;
    
    pthread_mutex_lock(&db->stmt_lock);
    
    // Check if already prepared on this connection
    stmt = findPreparedStmt(db, conn, sql);
    if (stmt) {
        pthread_mutex_unlock(&db->stmt_lock);
        return stmt;
    }
    
    // Generate unique name for this statement
    char buf[32];
    snprintf(buf, sizeof(buf), "stmt_%d_%lu", PQbackendPID(conn), atomic_fetch_add(&stmt_counter, 1));
    const char *stmt_name = arenaDupString(db->pool->arena, buf);
    
    // Prepare the statement
    PGresult *res = PQprepare(conn, stmt_name, sql, 0, NULL);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", PQerrorMessage(conn));
        PQclear(res);
        pthread_mutex_unlock(&db->stmt_lock);
        return NULL;
    }
    PQclear(res);
    
    // Add to cache
    uint32_t hash = hashString(sql) & STMT_HASH_MASK;
    stmt = arenaAlloc(db->pool->arena, sizeof(PreparedStmt));
    stmt->sql = arenaDupString(db->pool->arena, sql);
    stmt->name = stmt_name;
    stmt->conn = conn;
    stmt->next = db->stmt_cache[hash];
    db->stmt_cache[hash] = stmt;
    
    pthread_mutex_unlock(&db->stmt_lock);
    return stmt;
}

static PGresult* executePreparedStatement(Database *db, const char *sql,
                                 const char **values, size_t value_count) {
    DbConnection conn = getDbConnection(db);
    if (!conn.conn) return NULL;
    
    // Get or create prepared statement
    PreparedStmt *stmt = prepareSqlStatement(db, conn.conn, sql);
    if (!stmt) {
        releaseConnection(&conn);
        return NULL;
    }
    
    // Execute prepared statement
    PGresult *result = PQexecPrepared(conn.conn, stmt->name, (int)(value_count & INT_MAX), 
                                     values, NULL, NULL, 0);

    releaseConnection(&conn);
    return result;
}

// Modify existing executeParameterizedQuery to use prepared statements
PGresult* executeParameterizedQuery(Database *db, const char *sql, 
                                  const char **values, size_t value_count) {
    return executePreparedStatement(db, sql, values, value_count);
}

Database* initDatabase(Arena *arena, const char *conninfo) {
    if (!arena || !conninfo) {
        fputs("Database arena or connection info cannot be NULL\n", stderr);
        return NULL;
    }

    Database *db = arenaAlloc(arena, sizeof(Database));
    if (!db) {
        fputs("Failed to allocate database structure\n", stderr);
        return NULL;
    }
    
    db->conninfo = arenaDupString(arena, conninfo);
    memset(db->stmt_cache, 0, sizeof(db->stmt_cache));  // Initialize hash table to NULL
    
    if (pthread_mutex_init(&db->stmt_lock, NULL) != 0) {
        return NULL;
    }
    
    db->pool = initConnectionPool(arena, conninfo, INITIAL_POOL_SIZE, MAX_POOL_SIZE);
    if (!db->pool) {
        pthread_mutex_destroy(&db->stmt_lock);
        return NULL;
    }
    printf("Database connection pool initialized\n");
    
    return db;
}

static PGresult* executeQuery(Database *db, const char *query) {
    if (!db || !query) {
        fputs("Invalid database or query\n", stderr);
        return NULL;
    }

    DbConnection conn = getDbConnection(db);
    if (!conn.conn) {
        fputs("Could not get database connection from pool\n", stderr);
        return NULL;
    }

    PGresult *result = PQexec(conn.conn, query);
    if (PQresultStatus(result) != PGRES_TUPLES_OK && 
        PQresultStatus(result) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Query failed: %s", PQerrorMessage(conn.conn));
        PQclear(result);
        releaseConnection(&conn);
        return NULL;
    }
    
    releaseConnection(&conn);
    return result;
}

static void freeResult(PGresult *result) {
    if (result) {
        PQclear(result);
    }
}

void closeDatabase(Database *db) {
    if (!db) return;

    pthread_mutex_lock(&db->stmt_lock);
    
    // Clear statement cache (no need to free since using arena)
    memset(db->stmt_cache, 0, sizeof(db->stmt_cache));
    
    pthread_mutex_unlock(&db->stmt_lock);
    pthread_mutex_destroy(&db->stmt_lock);
    if (db->pool) {
        closeConnectionPool(db->pool);
    }
}

json_t* resultToJson(PGresult *result, const char *sql) {
    if (!result) return NULL;
    
    int rowCount = PQntuples(result);
    int colCount = PQnfields(result);
    
    json_t *root = json_object();
    json_t *rows = json_array();
    json_object_set_new(root, "rows", rows);
    json_object_set_new(root, "query", json_string(sql));
    
    // For each row
    for (int i = 0; i < rowCount; i++) {
        json_t *row = json_object();
        
        // For each column
        for (int j = 0; j < colCount; j++) {
            const char *colName = PQfname(result, j);
            
            if (PQgetisnull(result, i, j)) {
                json_object_set_new(row, colName, json_null());
            } else {
                const char *value = PQgetvalue(result, i, j);
                json_object_set_new(row, colName, json_string(value));
            }
        }
        
        json_array_append_new(rows, row);
    }
    
    return root;
}

json_t* executeSqlWithParams(Database *db, const char *sql, const char **values, size_t value_count) {
    PGresult *result;
    if (values && value_count > 0) {
        result = executeParameterizedQuery(db, sql, values, value_count);
    } else {
        result = executeQuery(db, sql);
    }

    if (!result) {
        return NULL;
    }

    json_t *jsonData = resultToJson(result, sql);
    freeResult(result);
    return jsonData;
}

static json_t *executeAndFormatQuery(Arena *arena, const char *sql,
                                     const char **values, size_t value_count) {
    (void)arena;

    if (!sql) {
        return NULL;
    }

    return executeSqlWithParams(ctx->db, sql, values, value_count);
}

static json_t* createErrorResponse(const char* message) {
    json_t *result = json_object();
    json_object_set_new(result, "error", json_string(message));
    return result;
}

static void extractJsonParams(json_t *input, Arena *arena, const char ***values, size_t *value_count) {
    json_t *params = json_object_get(input, "sqlParams");
    *values = NULL;
    *value_count = 0;

    if (!json_is_array(params)) {
        if (json_is_array(input)) {
            params = input;
        } else {
            return;
        }
    }

    *value_count = json_array_size(params);
    if (*value_count > 0) {
        *values = arenaAlloc(arena, sizeof(char *) * (*value_count));
        if (!*values) {
            return;
        }

        for (size_t i = 0; i < *value_count; i++) {
            json_t *param = json_array_get(params, i);
            if (json_is_string(param)) {
                (*values)[i] = json_string_value(param);
            } else if (json_is_number(param)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0f", json_number_value(param));
                (*values)[i] = arenaDupString(arena, buf);
            } else {
                char *str = json_dumps(param, JSON_COMPACT);
                if (str) {
                    (*values)[i] = arenaDupString(arena, str);
                    free(str);
                }
            }
        }
    }
}

json_t *executeSqlStep(PipelineStepNode *step, json_t *input,
                              json_t *requestContext, Arena *arena, ServerContext *serverCtx) {
    (void)requestContext;
    (void)serverCtx;

    const char *sql;
    if (step->is_dynamic) {
        sql = json_string_value(json_object_get(input, "sql"));
        if (!sql) {
            return createErrorResponse("No SQL query provided");
        }
    } else {
        sql = step->code;
        if (step->name) {
            QueryNode *query = findQuery(step->name);
            sql = query->sql;
        }
        if (!sql) {
            return createErrorResponse("No SQL query found");
        }
    }

    const char **values = NULL;
    size_t value_count = 0;
    json_t *params = NULL;
    
    if (input) {
        extractJsonParams(input, arena, &values, &value_count);
        if (!values && value_count > 0) {
            return createErrorResponse("Failed to allocate memory for parameters");
        }
        // Store the params for including in result
        params = json_object_get(input, "sqlParams");
    }

    json_t *jsonData = executeAndFormatQuery(arena, sql, values, value_count);
    if (!jsonData) {
        return createErrorResponse("Failed to execute SQL query");
    }

    // Create new result object
    json_t *result;
    if (input) {
        result = json_deep_copy(input);
        // Clear params after execution
        json_object_set_new(result, "sqlParams", json_array());
    } else {
        result = json_object();
    }

    // Get or create data array
    json_t *data = json_object_get(result, "data");
    if (!data) {
        data = json_array();
        json_object_set_new(result, "data", data);
    }

    // If we had params, include them in the result data
    if (params && json_array_size(params) > 0) {
        json_object_set(jsonData, "sqlParams", params);
    }

    // Add new result to data array
    json_array_append_new(data, jsonData);

    return result;
}
