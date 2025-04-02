#include "db_pool.h"
#include <stdio.h>
#include <string.h>

static PooledConnection* createConnection(ConnectionPool *pool) {
    if (!pool || !pool->conninfo) return NULL;
    
    PooledConnection *conn = arenaAlloc(pool->arena, sizeof(PooledConnection));
    if (!conn) return NULL;
    
    conn->conn = PQconnectdb(pool->conninfo);
    if (PQstatus(conn->conn) != CONNECTION_OK) {
        fprintf(stderr, "Failed to create database connection: %s\n", 
                PQerrorMessage(conn->conn));
        PQfinish(conn->conn);
        return NULL;
    }
    
    conn->in_use = 0;
    conn->next = NULL;
    return conn;
}

ConnectionPool* initConnectionPool(Arena *arena, const char *conninfo, int initial_size, int max_size) {
    if (!arena || !conninfo || initial_size < 1 || max_size < initial_size) return NULL;
    
    ConnectionPool *pool = arenaAlloc(arena, sizeof(ConnectionPool));
    if (!pool) return NULL;
    
    pool->arena = arena;
    pool->conninfo = arenaDupString(arena, conninfo);
    if (!pool->conninfo) {
        fprintf(stderr, "Failed to copy connection info string\n");
        return NULL;
    }
    
    pool->size = 0;
    pool->max_size = max_size > MAX_POOL_SIZE ? MAX_POOL_SIZE : max_size;
    pool->connections = NULL;
    
    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize mutex\n");
        return NULL;
    }
    
    // Create initial connections
    for (int i = 0; i < initial_size; i++) {
        PooledConnection *conn = createConnection(pool);
        if (!conn) {
            fprintf(stderr, "Warning: Could only create %d initial connections\n", i);
            break;
        }
        
        conn->next = pool->connections;
        pool->connections = conn;
        pool->size++;
    }
    
    if (pool->size == 0) {
        fprintf(stderr, "Failed to create any initial connections\n");
        pthread_mutex_destroy(&pool->lock);
        return NULL;
    }
    
    return pool;
}

PooledConnection* getConnection(ConnectionPool *pool) {
    if (!pool) return NULL;
    
    pthread_mutex_lock(&pool->lock);
    
    // Look for an available connection
    PooledConnection *conn = pool->connections;
    while (conn) {
        if (!conn->in_use) {
            conn->in_use = 1;
            pthread_mutex_unlock(&pool->lock);
            return conn;
        }
        conn = conn->next;
    }
    
    // If we have room, create a new connection
    if (pool->size < pool->max_size) {
        conn = createConnection(pool);
        if (conn) {
            conn->in_use = 1;
            conn->next = pool->connections;
            pool->connections = conn;
            pool->size++;
            pthread_mutex_unlock(&pool->lock);
            return conn;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);
    return NULL;  // No connections available
}

void returnConnection(ConnectionPool *pool, PooledConnection *conn) {
    if (!pool || !conn) return;
    
    pthread_mutex_lock(&pool->lock);
    
    // Check connection status and reset if needed
    if (PQstatus(conn->conn) != CONNECTION_OK) {
        PQreset(conn->conn);
    }
    
    conn->in_use = 0;
    
    pthread_mutex_unlock(&pool->lock);
}

void closeConnectionPool(ConnectionPool *pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&pool->lock);
    
    PooledConnection *conn = pool->connections;
    while (conn) {
        PQfinish(conn->conn);
        conn = conn->next;
    }
    
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
}
