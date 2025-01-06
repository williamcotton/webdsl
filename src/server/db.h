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
#include "../ast.h"

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

void closeDatabase(Database *db);

json_t *executeSqlStep(PipelineStepNode *step, json_t *input,
                       json_t *requestContext, Arena *arena);

#endif // DB_H
