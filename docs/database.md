# Database Integration Guide

WebDSL provides native PostgreSQL integration with built-in connection pooling and prepared statement caching.

## Configuration

### Database Connection
Configure your database connection in the website block:
```webdsl
website {
    database "postgresql://localhost/mydb"
}
```

## SQL Operations

### Direct SQL Queries
```webdsl
sql {
    SELECT * FROM users
    WHERE id = $1
}
```

### Parameterized Queries
Use numbered parameters ($1, $2, etc.) for safe value interpolation:
```webdsl
pipeline {
    lua {
        return { sqlParams = {userId, status} }
    }
    sql {
        SELECT * FROM orders
        WHERE user_id = $1
        AND status = $2
    }
}
```

## Query Builder

WebDSL provides a fluent query builder interface through Lua:

### Basic Select Query
```webdsl
lua {
    local qb = querybuilder.new()
    local result = qb
        :select("*")
        :from("users")
        :where("status = ?", "active")
        :build()
}
```

### Complex Queries
```webdsl
lua {
    local qb = querybuilder.new()
    local result = qb
        :select("u.id", "u.name", "t.name as team_name")
        :from("users u")
        :join("teams t", "u.team_id = t.id")
        :where_if(query.team_id, "t.id = ?", query.team_id)
        :order_by("u.name")
        :limit(query.limit or 20)
        :offset(query.offset or 0)
        :with_metadata()
        :build()
}
```

### Query Builder Methods

- `:select(...)` - Specify columns to select
- `:from(table)` - Specify the main table
- `:join(table, condition)` - Add JOIN clause
- `:where(condition, ...params)` - Add WHERE clause
- `:where_if(condition, clause, ...params)` - Conditional WHERE
- `:order_by(columns)` - Add ORDER BY clause
- `:limit(n)` - Add LIMIT clause
- `:offset(n)` - Add OFFSET clause
- `:with_metadata()` - Include count metadata
- `:build()` - Generate the final query

## Pagination

### Automatic Pagination
The query builder supports automatic pagination with metadata:

```webdsl
lua {
    local qb = querybuilder.new()
    local result = qb
        :select("*")
        :from("products")
        :limit(query.limit or 10)
        :offset(query.offset or 0)
        :with_metadata()
        :build()
}
```

The result includes:
- Total count
- Current offset
- Current limit
- Has more flag

### Pagination Example
```webdsl
pipeline {
    lua {
        local qb = querybuilder.new()
        return qb
            :select("*")
            :from("employees")
            :limit(query.limit or 20)
            :offset(query.offset or 0)
            :with_metadata()
            :build()
    }
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
