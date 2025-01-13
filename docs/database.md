# Database Integration Guide

## Configuration

### Basic Setup

Configure database connection in the website block:

```webdsl
website {
    database "postgresql://user:pass@localhost/dbname"
}
```

### Connection Pool Settings

The server automatically configures connection pooling:
- Initial pool size: 20 connections
- Maximum pool size: 50 connections
- Connection timeout: 30 seconds

## Queries

### Basic Query Definition

```webdsl
query {
    name "getUsers"
    sql {
        SELECT * FROM users
        WHERE active = true
    }
}
```

### Parameterized Queries

```webdsl
query {
    name "getUserById"
    params [id, status]
    sql {
        SELECT * FROM users
        WHERE id = $1
        AND status = $2
    }
}
```

### Named Parameters

Parameters are referenced by position ($1, $2, etc.) in SQL and named in the params block:

```webdsl
query {
    name "searchUsers"
    params [search_term, department, limit]
    sql {
        SELECT * FROM users
        WHERE (name ILIKE '%' || $1 || '%'
           OR email ILIKE '%' || $1 || '%')
        AND department = $2
        LIMIT $3
    }
}
```

## Advanced Features

### Pagination

```webdsl
query {
    name "paginatedUsers"
    params [offset, limit]
    sql {
        SELECT *, count(*) OVER() as total_count
        FROM users
        ORDER BY created_at DESC
        OFFSET $1
        LIMIT $2
    }
}
```

### Complex Joins

```webdsl
query {
    name "userDetails"
    params [user_id]
    sql {
        SELECT 
            u.*,
            d.name as department_name,
            array_agg(r.name) as roles
        FROM users u
        LEFT JOIN departments d ON u.department_id = d.id
        LEFT JOIN user_roles ur ON u.id = ur.user_id
        LEFT JOIN roles r ON ur.role_id = r.id
        WHERE u.id = $1
        GROUP BY u.id, d.name
    }
}
```

### Transactions

Currently, transactions must be handled within a single query:

```webdsl
query {
    name "createUserWithProfile"
    params [name, email, bio]
    sql {
        WITH new_user AS (
            INSERT INTO users (name, email)
            VALUES ($1, $2)
            RETURNING id
        )
        INSERT INTO profiles (user_id, bio)
        SELECT id, $3
        FROM new_user
        RETURNING user_id
    }
}
```

## Helper Functions

### Optional Parameters

The `filter_by_optional` function handles optional query parameters:

```webdsl
query {
    name "filterUsers"
    params [department, status]
    sql {
        SELECT * FROM users
        WHERE filter_by_optional(department, $1)
        AND filter_by_optional(status, $2)
    }
}
```

### Parameter Parsing

The `parse_optional` function safely parses parameter values:

```webdsl
query {
    name "getUsersByAge"
    params [min_age]
    sql {
        SELECT * FROM users
        WHERE age >= COALESCE(
            parse_optional($1, 0::integer),
            0
        )
    }
}
```

## Best Practices

1. **Use Prepared Statements**
   - All queries are automatically prepared and cached

2. **Parameter Safety**
   - Always use parameterized queries
   - Never concatenate user input

3. **Connection Management**
   - Let the connection pool handle connections
   - Don't manually manage connections

4. **Query Organization**
   - Use meaningful query names
   - Group related queries together
   - Comment complex queries

5. **Error Handling**
   - Use COALESCE for NULL handling
   - Implement proper constraints
   - Return meaningful error messages

6. **Performance**
   - Include necessary indexes
   - Use EXPLAIN ANALYZE for optimization
   - Implement proper pagination
   - Avoid N+1 query problems

## Common Patterns

### Search with Multiple Filters

```webdsl
query {
    name "searchProducts"
    params [category, min_price, max_price, search_term]
    sql {
        SELECT * FROM products
        WHERE filter_by_optional(category, $1)
        AND price >= COALESCE(parse_optional($2, 0::numeric), 0)
        AND price <= COALESCE(parse_optional($3, 'Infinity'::numeric), 'Infinity')
        AND (
            $4 IS NULL
            OR name ILIKE '%' || $4 || '%'
            OR description ILIKE '%' || $4 || '%'
        )
    }
}
```

### Hierarchical Data

```webdsl
query {
    name "getCategory"
    params [category_id]
    sql {
        WITH RECURSIVE category_tree AS (
            SELECT *, 1 as level
            FROM categories
            WHERE id = $1
            
            UNION ALL
            
            SELECT c.*, ct.level + 1
            FROM categories c
            JOIN category_tree ct ON c.parent_id = ct.id
        )
        SELECT * FROM category_tree
        ORDER BY level
    }
} 
```

### Direct SQL Execution

SQL can be executed directly in Lua scripts:

```webdsl
pipeline {
    lua {
        local result = sqlQuery("SELECT * FROM notes")
        return {
            data = result
        }
    }
}
```

### Query Builder

The query builder provides a fluent interface for constructing SQL queries:

```webdsl
lua {
    local qb = querybuilder.new()
    
    local result = qb
        :select("id", "name", "email")
        :from("employees")
        :where_if(query.team_id, "team_id = ?", query.team_id)
        :order_by("id")
        :limit(query.limit or 20)
        :offset(query.offset or 0)
        :with_metadata()
        :build()
        
    return result
}
```

Query Builder Features:
- Conditional WHERE clauses with `where_if`
- Automatic parameter binding
- Pagination support
- Metadata generation
- Order by clauses
- Dynamic column selection

### Pagination Handling

Built-in support for pagination with metadata:

```webdsl
pipeline {
    lua {
        local qb = querybuilder.new()
        return qb
            :select("*")
            :from("users")
            :limit(query.limit or 20)
            :offset(query.offset or 0)
            :with_metadata()
            :build()
    }
    executeQuery dynamic
    jq {
        {
            data: (.rows | map(select(.type == "data"))),
            metadata: {
                total: (.rows | map(select(.type == "metadata")) | .[0].total_count),
                offset: (.rows | map(select(.type == "metadata")) | .[0].offset),
                limit: (.rows | map(select(.type == "metadata")) | .[0].limit),
                has_more: (.rows | map(select(.type == "metadata")) | .[0].has_more)
            }
        }
    }
}
``` 