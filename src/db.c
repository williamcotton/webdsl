#include "db.h"
#include "db_pool.h"
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
