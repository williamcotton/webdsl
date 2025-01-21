# API Features Guide

WebDSL provides a powerful API system with built-in support for REST endpoints, data validation, and transformation pipelines.

## Basic API Structure

### Simple Endpoint
```webdsl
api {
    route "/api/v1/resource"
    method "GET"
    pipeline {
        sql {
            SELECT * FROM resources
        }
        jq {
            { data: .rows }
        }
    }
}
```

### Complete Endpoint Example
```webdsl
api {
    route "/api/v1/employees"
    method "POST"
    fields {
        "name" {
            type "string"
            required true
            length 10..100
        }
        "email" {
            type "string"
            required true
            format "email"
        }
    }
    pipeline {
        lua {
            return { sqlParams = {body.name, body.email} }
        }
        sql {
            INSERT INTO employees (name, email)
            VALUES ($1, $2)
            RETURNING id, name, email
        }
        jq {
            {
                success: true,
                employee: .rows[0]
            }
        }
    }
}
```

## Request Pipeline

The pipeline is a sequence of transformations applied to the request data:

### Pipeline Components

1. **Lua Scripts**
   ```webdsl
   lua {
       local data = processData(request.body)
       return { processed = data }
   }
   ```

2. **SQL Queries**
   ```webdsl
   sql {
       SELECT * FROM table WHERE id = $1
   }
   ```

3. **JQ Transformations**
   ```webdsl
   jq {
       {
           data: .rows,
           metadata: {
               count: length
           }
       }
   }
   ```

### Pipeline Context

Each pipeline step has access to:
- `request` - The full request object
- `query` - URL query parameters
- `params` - Route parameters
- `body` - Request body
- `headers` - Request headers

## Field Validation

### Basic Validation
```webdsl
fields {
    "username" {
        type "string"
        required true
        length 3..50
    }
    "age" {
        type "number"
        validate {
            range 0..120
        }
    }
}
```

### Validation Types
- `string`
- `number`
- `boolean`
- `array`
- `object`

### Validation Rules
- `required` - Field must be present
- `length` - String length range
- `format` - Predefined formats (email, date, etc.)
- `range` - Numeric range
- `pattern` - Regex pattern match

## External API Integration

### Fetch External APIs
```webdsl
api {
    route "/api/v1/weather"
    method "GET"
    pipeline {
        lua {
            local url = "https://api.weather.com/forecast"
            local response = fetch(url)
            return { data = response }
        }
    }
}
```

## Response Handling

### Success Response
```webdsl
pipeline {
    // ... processing steps
    jq {
        {
            success: true,
            data: .rows,
            message: "Operation successful"
        }
    }
}
```

### Error Handling
```webdsl
api {
    route "/api/v1/resource"
    method "POST"
    pipeline {
        // ... processing steps
    }
    error {
        jq {
            {
                success: false,
                error: {
                    code: .statusCode,
                    message: .error
                }
            }
        }
    }
}
```

## Reusable Components

### Script Blocks
```webdsl
script {
    name "processUserData"
    lua {
        return {
            processed = processData(body)
        }
    }
}

api {
    route "/api/v1/users"
    pipeline {
        executeScript "processUserData"
        // ... more steps
    }
}
```

### Transform Blocks
```webdsl
transform {
    name "formatUserResponse"
    jq {
        {
            data: (.rows | map({
                id: .id,
                name: .name,
                email: .email
            }))
        }
    }
}

api {
    route "/api/v1/users"
    pipeline {
        sql { SELECT * FROM users }
        executeTransform "formatUserResponse"
    }
}
```

## Best Practices

1. Use semantic versioning in API routes
2. Implement proper validation for all inputs
3. Structure responses consistently
4. Handle errors gracefully
5. Use reusable components for common operations
6. Document API endpoints thoroughly
7. Use appropriate HTTP methods
8. Implement rate limiting for public APIs 