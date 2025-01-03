# Validation Rules Guide

## Field Types

### String Validation
```webdsl
fields {
    "username" {
        type "string"
        required true
        length 3..20
        validate {
            match "[a-zA-Z0-9_]+"
        }
    }
}
```

### Number Validation
```webdsl
fields {
    "age" {
        type "number"
        required true
        validate {
            range 0..120
        }
    }
}
```

## Format Types

### Email
```webdsl
"email" {
    type "string"
    format "email"
}
```

### URL
```webdsl
"website" {
    type "string"
    format "url"
}
```

### Date
```webdsl
"birthdate" {
    type "string"
    format "date"  // YYYY-MM-DD
}
```

### Time
```webdsl
"appointment" {
    type "string"
    format "time"  // HH:MM or HH:MM:SS
}
```

### Phone
```webdsl
"phone" {
    type "string"
    format "phone"
}
```

### UUID
```webdsl
"id" {
    type "string"
    format "uuid"
}
```

### IPv4
```webdsl
"ip_address" {
    type "string"
    format "ipv4"
}
```

## Validation Rules

### Length Constraints
```webdsl
"password" {
    type "string"
    length 8..64
}
```

### Pattern Matching
```webdsl
"username" {
    type "string"
    validate {
        match "^[a-z][a-z0-9_]{2,19}$"
    }
}
```

### Numeric Range
```webdsl
"score" {
    type "number"
    validate {
        range 0..100
    }
}
```

### Required Fields
```webdsl
"email" {
    type "string"
    required true
    format "email"
}
```

## Complex Validation Examples

### User Registration
```webdsl
api {
    route "/api/v1/users"
    method "POST"
    fields {
        "username" {
            type "string"
            required true
            length 3..20
            validate {
                match "^[a-zA-Z][a-zA-Z0-9_]*$"
            }
        }
        "email" {
            type "string"
            required true
            format "email"
        }
        "password" {
            type "string"
            required true
            length 8..64
            validate {
                match "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[@$!%*?&])[A-Za-z\\d@$!%*?&]"
            }
        }
        "age" {
            type "number"
            required true
            validate {
                range 13..120
            }
        }
        "phone" {
            type "string"
            format "phone"
        }
    }
}
```

### Product Creation
```webdsl
api {
    route "/api/v1/products"
    method "POST"
    fields {
        "name" {
            type "string"
            required true
            length 2..100
        }
        "sku" {
            type "string"
            required true
            validate {
                match "^[A-Z0-9]{6,10}$"
            }
        }
        "price" {
            type "number"
            required true
            validate {
                range 0..1000000
            }
        }
        "description" {
            type "string"
            length 0..1000
        }
        "website" {
            type "string"
            format "url"
        }
    }
}
```

## Error Handling

Validation errors are returned as JSON:

```json
{
    "errors": {
        "username": "Must be between 3 and 20 characters",
        "email": "Invalid email format",
        "age": "Must be between 13 and 120"
    }
}
```

## Best Practices

1. Always validate user input
2. Use appropriate format types
3. Set reasonable length limits
4. Use specific error messages
5. Combine multiple validation rules
6. Document validation requirements
7. Test edge cases
8. Use consistent validation patterns

## Security Considerations

1. Validate on both client and server
2. Sanitize input after validation
3. Use appropriate data types
4. Implement rate limiting
5. Log validation failures
6. Use secure password rules
7. Validate file uploads
8. Check content types 