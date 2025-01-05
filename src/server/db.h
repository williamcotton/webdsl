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

#define STMT_HASH_SIZE 64
#define STMT_HASH_MASK (STMT_HASH_SIZE - 1)

// Add prepared statement cache structure
typedef struct PreparedStmt {
    const char *sql;
    const char *name;  // Statement name for Postgres
    PGconn *conn;      // Connection this statement was prepared on
    struct PreparedStmt *next;  // For hash collision chaining
} PreparedStmt;

typedef struct Database {
    ConnectionPool *pool;
    const char *conninfo;
    PreparedStmt *stmt_cache[STMT_HASH_SIZE];  // Array of statement chains
    pthread_mutex_t stmt_lock; // Lock for statement cache
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

// Add new function declarations
PGresult* executePreparedStatement(Database *db, const char *sql, 
                                 const char **values, size_t value_count);

#endif // DB_H
