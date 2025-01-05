# API Features Guide

## API Endpoint Definition

Basic API endpoint structure:

```webdsl
api {
    route "/api/v1/resource"
    method "GET"
    pipeline {
        executeQuery "queryName"
    }
}
```

## Request Processing

### Pipeline Steps

The pipeline block defines a sequence of processing steps that are executed in order. Each step receives the output of the previous step as input.

#### JQ Step
```webdsl
api {
    route "/api/v1/users"
    method "GET"
    pipeline {
        jq {
            {
                department: .query.dept,
                status: (.query.status // "active"),
                limit: (.query.limit | tonumber // 20)
            }
        }
        executeQuery "getUsers"
        jq {
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
}
```

#### Lua Step
```webdsl
api {
    route "/api/v1/users"
    method "GET"
    pipeline {
        lua {
            -- Access request context
            local dept = query.dept
            local status = query.status or "active"
            local limit = tonumber(query.limit) or 20
            
            -- Return array of values for SQL query
            return {dept, status, limit}
        }
        executeQuery "getUsers"
        lua {
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
}
```

## Request Context

Available in pipeline steps:

### Query Parameters
```webdsl
pipeline {
    jq {
        .query.param_name
    }
}

pipeline {
    lua {
        local value = query.param_name
    }
}
```

### Headers
```webdsl
pipeline {
    jq {
        .headers["X-Custom-Header"]
    }
}

pipeline {
    lua {
        local header = headers["X-Custom-Header"]
    }
}
```

### Request Body (POST)
```webdsl
pipeline {
    jq {
        .body.field_name
    }
}

pipeline {
    lua {
        local field = body.field_name
    }
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
pipeline {
    jq {
        {
            status: "success",
            data: .rows
        }
    }
}
```

### Pagination
```webdsl
pipeline {
    lua {
        if not querybuilder then
            error("querybuilder module not loaded")
        end
        local qb = querybuilder.new()
        
        local result = qb
            :select("*")
            :from("users")
            :limit(query.limit or 20)
            :offset(query.offset or 0)
            :with_metadata()
            :build()
            
        return result
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

### Error Handling
```webdsl
pipeline {
    lua {
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
}
```

## Common Patterns

### Authentication Check
```webdsl
api {
    route "/api/v1/protected"
    method "GET"
    pipeline {
        lua {
            local auth = headers["Authorization"]
            if not auth then
                error("Authentication required")
            end
            return {auth:sub(8)}  -- Remove "Bearer " prefix
        }
        executeQuery "protectedResource"
    }
}
```

### Rate Limiting
```webdsl
api {
    route "/api/v1/limited"
    method "GET"
    pipeline {
        jq {
            if (.headers["X-Rate-Remaining"] | tonumber) <= 0 then
                error("Rate limit exceeded")
            else
                .
            end
        }
        executeQuery "limitedResource"
    }
}
```

### Conditional Response
```webdsl
api {
    route "/api/v1/users"
    method "GET"
    pipeline {
        executeQuery "getUsers"
        jq {
            if .query.format == "summary" then
                .rows | map({id: .id, name: .name})
            else
                .rows
            end
        }
    }
} 