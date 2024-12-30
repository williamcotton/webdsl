#include "db.h"
#include "db_pool.h"
#include "stringbuilder.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <jansson.h>

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
    db->pool = initConnectionPool(arena, conninfo, 5, MAX_POOL_SIZE);
    
    if (!db->pool) {
        fputs("Failed to initialize connection pool\n", stderr);
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
    if (db && db->pool) {
        closeConnectionPool(db->pool);
    }
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

char* resultToJson(Arena *arena, PGresult *result) {
    if (!arena || !result) return NULL;
    
    json_t *root = json_object();
    json_t *rows = json_array();
    json_object_set_new(root, "rows", rows);
    
    int rowCount = PQntuples(result);
    int colCount = PQnfields(result);
    
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
    
    // Convert to string
    char *jsonStr = json_dumps(root, JSON_INDENT(2));
    return jsonStr;
}

PGresult* executeParameterizedQuery(Database *db, const char *sql, 
                                  const char **values, size_t value_count) {
    if (!db || !sql) return NULL;
    
    PGconn *conn = getDbConnection(db);
    if (!conn) {
        fputs("Could not get database connection from pool\n", stderr);
        return NULL;
    }

    PGresult *result = PQexecParams(conn, sql, (int)value_count, 
                                   NULL, values, NULL, NULL, 0);
    
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
