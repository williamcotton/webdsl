#ifndef DB_POOL_H
#define DB_POOL_H

#include <pthread.h>
#include <libpq-fe.h>
#include <stdint.h>
#include "arena.h"

#define MAX_POOL_SIZE 10

typedef struct PooledConnection {
    PGconn *conn;
    int in_use;
    uint32_t : 32;
    struct PooledConnection *next;
} PooledConnection;

typedef struct ConnectionPool {
    PooledConnection *connections;
    pthread_mutex_t lock;
    const char *conninfo;
    int size;
    int max_size;
    Arena *arena;
} ConnectionPool;

// Function declarations
ConnectionPool* initConnectionPool(Arena *arena, const char *conninfo, int initial_size, int max_size);
PooledConnection* getConnection(ConnectionPool *pool);
void returnConnection(ConnectionPool *pool, PooledConnection *conn);
void closeConnectionPool(ConnectionPool *pool);

#endif // DB_POOL_H
