#include "db.h"
#include "db_pool.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <uthash.h>
#include "server/utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
// Generate unique statement names
static __thread unsigned long stmt_counter = 0;

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
    snprintf(buf, sizeof(buf), "stmt_%lu", ++stmt_counter);
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

PGresult* executePreparedStatement(Database *db, const char *sql, 
                                 const char **values, size_t value_count) {
    PGconn *conn = getDbConnection(db);
    if (!conn) return NULL;
    
    // Get or create prepared statement
    PreparedStmt *stmt = prepareSqlStatement(db, conn, sql);
    if (!stmt) {
        releaseDbConnection(db, conn);
        return NULL;
    }
    
    // Execute prepared statement
    PGresult *result = PQexecPrepared(conn, stmt->name, (int)(value_count & INT_MAX), 
                                     values, NULL, NULL, 0);
    
    releaseDbConnection(db, conn);
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
    
    db->pool = initConnectionPool(arena, conninfo, 5, MAX_POOL_SIZE);
    if (!db->pool) {
        pthread_mutex_destroy(&db->stmt_lock);
        return NULL;
    }
    
    return db;
}

PGresult* executeQuery(Database *db, const char *query) {
    if (!db || !query) {
        fputs("Invalid database or query\n", stderr);
        return NULL;
    }

    PGconn *conn = getDbConnection(db);
    if (!conn) {
        fputs("Could not get database connection from pool\n", stderr);
        return NULL;
    }

    PGresult *result = PQexec(conn, query);
    if (PQresultStatus(result) != PGRES_TUPLES_OK && 
        PQresultStatus(result) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Query failed: %s", PQerrorMessage(conn));
        PQclear(result);
        releaseDbConnection(db, conn);
        return NULL;
    }
    
    releaseDbConnection(db, conn);
    return result;
}

void freeResult(PGresult *result) {
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
    closeConnectionPool(db->pool);
}

const char* getDatabaseError(Database *db) {
    if (!db || !db->pool) return "No database connection";
    
    // Get a connection from the pool to check its error message
    PGconn *conn = getDbConnection(db);
    if (!conn) return "No available database connections";
    
    const char *error = PQerrorMessage(conn);
    releaseDbConnection(db, conn);
    
    return error;
}

json_t* resultToJson(PGresult *result) {
    if (!result) return NULL;
    
    // printf("\n=== SQL Result ===\n");
    int rowCount = PQntuples(result);
    int colCount = PQnfields(result);
    
    // // Print column names
    // printf("Columns: ");
    // for (int i = 0; i < colCount; i++) {
    //     printf("%s%s", PQfname(result, i), i < colCount - 1 ? ", " : "\n");
    // }
    
    // // Print first few rows
    // printf("First 3 rows:\n");
    // for (int i = 0; i < rowCount && i < 3; i++) {
    //     printf("Row %d: ", i);
    //     for (int j = 0; j < colCount; j++) {
    //         if (PQgetisnull(result, i, j)) {
    //             printf("NULL%s", j < colCount - 1 ? ", " : "\n");
    //         } else {
    //             printf("%s%s", PQgetvalue(result, i, j), j < colCount - 1 ? ", " : "\n");
    //         }
    //     }
    // }
    // if (rowCount > 3) {
    //     printf("... and %d more rows\n", rowCount - 3);
    // }
    // printf("================\n");
    
    json_t *root = json_object();
    json_t *rows = json_array();
    json_object_set_new(root, "rows", rows);
    
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

PGconn* getDbConnection(Database *db) {
    if (!db || !db->pool) return NULL;
    
    PooledConnection *conn = getConnection(db->pool);
    return conn ? conn->conn : NULL;
}

void releaseDbConnection(Database *db, PGconn *conn) {
    if (!db || !db->pool || !conn) return;
    
    // Find the PooledConnection that contains this PGconn
    PooledConnection *pooled = db->pool->connections;
    while (pooled) {
        if (pooled->conn == conn) {
            returnConnection(db->pool, pooled);
            break;
        }
        pooled = pooled->next;
    }
}
