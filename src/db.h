#ifndef DB_H
#define DB_H

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#include <libpq-fe.h>
#pragma clang diagnostic pop
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "arena.h"
#include "db_pool.h"

typedef struct Database {
    ConnectionPool *pool;
    const char *conninfo;
} Database;

// Initialize database connection using arena for allocations
// Returns NULL on error
Database* initDatabase(Arena *arena, const char *conninfo);

// Execute a query and return the result
// Returns NULL on error
PGresult* executeQuery(Database *db, const char *sql);

// Convert query result to JSON object
// Returns NULL on error
json_t* resultToJson(PGresult *result);

// Free database resources (but not arena memory)
void freeResult(PGresult *result);
void closeDatabase(Database *db);

// Get last error message from database
const char* getDatabaseError(Database *db);

// Execute a parameterized query and return the result
// Returns NULL on error
PGresult* executeParameterizedQuery(Database *db, const char *sql, const char **values, size_t value_count);

// Get a connection from the pool
PGconn* getDbConnection(Database *db);

// Return a connection to the pool
void releaseDbConnection(Database *db, PGconn *conn);

#endif // DB_H
