#ifndef DB_H
#define DB_H

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#include <libpq-fe.h>
#pragma clang diagnostic pop
#include "arena.h"

typedef struct Database {
    PGconn *conn;
    const char *conninfo;
} Database;

// Initialize database connection using arena for allocations
// Returns NULL on error
Database* initDatabase(Arena *arena, const char *conninfo);

// Execute a query and return the result
// Returns NULL on error
PGresult* executeQuery(Database *db, const char *query);

// Convert query result to JSON string using arena allocator
// Returns NULL on error
char* resultToJson(Arena *arena, PGresult *result);

// Free database resources (but not arena memory)
void freeResult(PGresult *result);
void closeDatabase(Database *db);

// Get last error message from database
const char* getDatabaseError(Database *db);

#endif // DB_H
