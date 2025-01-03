# API Features Guide

## API Endpoint Definition

Basic API endpoint structure:

```webdsl
api {
    route "/api/v1/resource"
    method "GET"
    executeQuery "queryName"
}
```

## Request Processing

### Pre-filters

#### JQ Pre-filter
```webdsl
api {
    route "/api/v1/users"
    method "GET"
    preFilter jq {
        {
            department: .query.dept,
            status: (.query.status // "active"),
            limit: (.query.limit | tonumber // 20)
        }
    }
    executeQuery "getUsers"
}
```

#### Lua Pre-filter
```webdsl
api {
    route "/api/v1/users"
    method "GET"
    preFilter lua {
        -- Access request context
        local dept = query.dept
        local status = query.status or "active"
        local limit = tonumber(query.limit) or 20
        
        -- Return array of values for SQL query
        return {dept, status, limit}
    }
    executeQuery "getUsers"
}
```

### Post-filters

#### JQ Post-filter
```webdsl
api {
    postFilter jq {
        .rows | map({
            id: .id,
            name: .name,
            email: .email,
            metadata: {
                department: .dept_name,
                status: .status
            }
        })
    }
}
```

#### Lua Post-filter
```webdsl
api {
    postFilter lua {
        local result = {}
        for _, row in ipairs(rows) do
            table.insert(result, {
                id = row.id,
                name = row.name,
                email = row.email,
                metadata = {
                    department = row.dept_name,
                    status = row.status
                }
            })
        end
        return result
    }
}
```

## Request Context

Available in filters:

### Query Parameters
```webdsl
preFilter jq {
    .query.param_name
}

preFilter lua {
    local value = query.param_name
}
```

### Headers
```webdsl
preFilter jq {
    .headers["X-Custom-Header"]
}

preFilter lua {
    local header = headers["X-Custom-Header"]
}
```

### Request Body (POST)
```webdsl
preFilter jq {
    .body.field_name
}

preFilter lua {
    local field = body.field_name
}
```

## Field Validation

```webdsl
api {
    fields {
        "email" {
            type "string"
            required true
            format "email"
        }
        "age" {
            type "number"
            validate {
                range 18..100
            }
        }
        "username" {
            type "string"
            length 3..20
            validate {
                match "[a-zA-Z0-9_]+"
            }
        }
    }
}
```

## Response Formatting

### Basic Response
```webdsl
postFilter jq {
    {
        status: "success",
        data: .rows
    }
}
```

### Pagination
```webdsl
postFilter jq {
    {
        data: .rows[:-1],
        metadata: {
            total: .rows[-1].total_count,
            page: (.query.page | tonumber // 1),
            hasMore: .rows[-1].has_more
        }
    }
}
```

### Error Handling
```webdsl
postFilter lua {
    if #rows == 0 then
        return {
            status = "error",
            message = "No data found"
        }
    end
    return {
        status = "success",
        data = rows
    }
}
```

## Common Patterns

### Authentication Check
```webdsl
api {
    route "/api/v1/protected"
    preFilter lua {
        local auth = headers["Authorization"]
        if not auth then
            error("Authentication required")
        end
        return {auth:sub(8)}  -- Remove "Bearer " prefix
    }
}
```

### Rate Limiting
```webdsl
api {
    route "/api/v1/limited"
    preFilter jq {
        if (.headers["X-Rate-Remaining"] | tonumber) <= 0 then
            error("Rate limit exceeded")
        else
            .
        end
    }
}
```

### Conditional Response
```webdsl
api {
    postFilter jq {
        if .query.format == "summary" then
            .rows | map({id: .id, name: .name})
        else
            .rows
        end
    }
} 