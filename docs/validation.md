# Validation Rules Guide

WebDSL provides a comprehensive validation system for form fields and API inputs.

## Basic Validation Structure

```webdsl
fields {
    "fieldName" {
        type "string"
        required true
        // additional rules
    }
}
```

## Data Types

### String
```webdsl
fields {
    "username" {
        type "string"
        required true
        length 3..50
        pattern "^[a-zA-Z0-9_]+$"
    }
}
```

### Number
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

### Boolean
```webdsl
fields {
    "active" {
        type "boolean"
        required true
    }
}
```

### Array
```webdsl
fields {
    "tags" {
        type "array"
        minItems 1
        maxItems 10
    }
}
```

### Object
```webdsl
fields {
    "address" {
        type "object"
        required true
        fields {
            "street" {
                type "string"
                required true
            }
            "city" {
                type "string"
                required true
            }
        }
    }
}
```

## Validation Rules

### Common Rules
- `required` - Field must be present and non-empty
- `type` - Data type validation
- `default` - Default value if not provided

### String Rules
- `length` - String length range (e.g., `length 5..100`)
- `minLength` - Minimum string length
- `maxLength` - Maximum string length
- `pattern` - Regular expression pattern
- `format` - Predefined format validation
- `enum` - Value must be one of specified options

### Number Rules
- `range` - Numeric range (e.g., `range 0..100`)
- `min` - Minimum value
- `max` - Maximum value
- `multipleOf` - Must be multiple of value

### Array Rules
- `minItems` - Minimum array length
- `maxItems` - Maximum array length
- `uniqueItems` - All items must be unique

### Object Rules
- `required` - Required fields
- `additionalProperties` - Allow/disallow additional fields

## Predefined Formats

### Email
```webdsl
fields {
    "email" {
        type "string"
        required true
        format "email"
    }
}
```

### Date
```webdsl
fields {
    "birthdate" {
        type "string"
        required true
        format "date"
    }
}
```

### URL
```webdsl
fields {
    "website" {
        type "string"
        format "url"
    }
}
```

### Other Formats
- `time` - Time string (HH:MM:SS)
- `datetime` - ISO 8601 datetime
- `ipv4` - IPv4 address
- `ipv6` - IPv6 address
- `uuid` - UUID string

## Custom Validation

### Using Lua
```webdsl
fields {
    "customField" {
        type "string"
        validate {
            lua {
                if not isValid(value) then
                    return "Invalid value"
                end
                return nil
            }
        }
    }
}
```

## Error Handling

### Basic Error Template
```webdsl
error {
    mustache {
        <div class="errors">
            {{#errors}}
                <p class="error">{{field}}: {{message}}</p>
            {{/errors}}
        </div>
    }
}
```

### Field-Specific Errors
```webdsl
error {
    mustache {
        <form>
            <div class="field">
                <input name="email" value="{{values.email}}"
                       class="{{#errors.email}}error{{/errors.email}}">
                {{#errors.email}}
                    <span class="error-message">{{errors.email}}</span>
                {{/errors.email}}
            </div>
        </form>
    }
}
```

## Best Practices

1. **Input Sanitization**
   - Always validate and sanitize user input
   - Use appropriate data types
   - Apply length restrictions where applicable

2. **Error Messages**
   - Provide clear, user-friendly error messages
   - Include field names in error messages
   - Use consistent error formatting

3. **Security**
   - Validate on both client and server side
   - Use appropriate formats for sensitive data
   - Implement rate limiting for validation attempts

4. **Performance**
   - Use built-in validators when possible
   - Keep custom validation logic simple
   - Cache validation results when appropriate

5. **Maintainability**
   - Group related validations
   - Use consistent validation patterns
   - Document custom validation rules 