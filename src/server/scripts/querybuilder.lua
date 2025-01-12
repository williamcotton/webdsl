local QueryBuilder = {}
QueryBuilder.__index = QueryBuilder

function QueryBuilder.new()
    local self = {
        _selects = {},
        _from = nil,
        _wheres = {},
        _orderBy = nil,
        _limit = nil,
        _offset = nil,
        _params = {},
        _withMeta = false,
        _metaFields = {}
    }
    return setmetatable(self, QueryBuilder)
end

function QueryBuilder:select(...)
    self._selects = {...}
    return self
end

function QueryBuilder:from(table)
    self._from = table
    return self
end

function QueryBuilder:where(condition, ...)
    local params = {...}
    -- Replace ? with $N based on total param count
    local param_count = #self._params
    local modified_condition = condition:gsub("?", function()
        param_count = param_count + 1
        return "$" .. param_count
    end)
    table.insert(self._wheres, {cond = modified_condition, params = params})
    for _, param in ipairs(params) do
        if param ~= nil then
            table.insert(self._params, param)
        end
    end
    return self
end

function QueryBuilder:where_if(value, condition, ...)
    if value ~= nil and value ~= "" then
        return self:where(condition, ...)
    end
    return self
end

function QueryBuilder:order_by(expr)
    self._orderBy = expr
    return self
end

function QueryBuilder:limit(n)
    if n then
        self._limit = tonumber(n)
    end
    return self
end

function QueryBuilder:offset(n)
    if n then
        self._offset = tonumber(n)
    end
    return self
end

function QueryBuilder:with_metadata(fields)
    self._withMeta = true
    self._metaFields = fields or {}
    return self
end

function QueryBuilder:_build_select()
    if self._withMeta then
        -- Build NULL fields for metadata based on selected fields
        local null_fields = {}
        for _, field in ipairs(self._selects) do
            local field_name = field:match("([^%s]+)%s+[aA][sS]%s+") or field
            field_name = field_name:match("%.([^%.]+)$") or field_name
            
            local field_type = self._metaFields[field_name]
            if not field_type then
                if field_name == "id" then
                    field_type = "bigint"
                elseif field_name:match("_id$") then
                    field_type = "integer"
                elseif field_name:match("count$") then
                    field_type = "bigint"
                else
                    field_type = "text"
                end
            end
            
            table.insert(null_fields, string.format("NULL::%s as %s", field_type, field_name))
        end
        local null_fields_str = table.concat(null_fields, ", ")

        -- Build core query without LIMIT/OFFSET
        local core_parts = {}
        local select_clause = "SELECT " .. (#self._selects > 0 and table.concat(self._selects, ", ") or "*")
        table.insert(core_parts, select_clause)
        if self._from then table.insert(core_parts, "FROM " .. self._from) end
        if #self._wheres > 0 then
            local conditions = {}
            for _, where in ipairs(self._wheres) do
                table.insert(conditions, where.cond)
            end
            table.insert(core_parts, "WHERE " .. table.concat(conditions, " AND "))
        end
        if self._orderBy then table.insert(core_parts, "ORDER BY " .. self._orderBy) end
        local core_query = table.concat(core_parts, " ")

        -- Build the complete query with pagination and metadata
        return string.format([[
            WITH base_query AS (
                %s
            ),
            total_count AS (
                SELECT COUNT(*) as count FROM base_query
            ),
            paginated_data AS (
                SELECT base_query.*, total_count.count as total_count
                FROM base_query, total_count
                %s
                %s
            )
            (
                SELECT 
                    'data'::text as type,
                    NULL::bigint as total_count,
                    %s,
                    NULL::integer as offset,
                    NULL::integer as limit,
                    NULL::boolean as has_more
                FROM paginated_data
            )
            UNION ALL
            (
                SELECT
                    'metadata'::text as type,
                    count::bigint as total_count,
                    %s,
                    %s as offset,
                    %s as limit,
                    EXISTS (
                        SELECT 1 FROM base_query
                        OFFSET %s + %s
                        LIMIT 1
                    ) as has_more
                FROM total_count
            )
            ORDER BY type DESC
        ]], 
        core_query,
        self._limit and string.format("LIMIT %d", self._limit) or "",
        self._offset and string.format("OFFSET %d", self._offset) or "",
        table.concat(self._selects, ", "),
        null_fields_str,
        self._offset or "0",
        self._limit or "20",
        self._offset or "0",
        self._limit or "20")
    else
        return self:_build_core()
    end
end

function QueryBuilder:_build_core()
    local parts = {}
    local select_clause = "SELECT " .. (#self._selects > 0 and table.concat(self._selects, ", ") or "*")
    table.insert(parts, select_clause)
    if self._from then table.insert(parts, "FROM " .. self._from) end
    if #self._wheres > 0 then
        local conditions = {}
        for _, where in ipairs(self._wheres) do
            table.insert(conditions, where.cond)
        end
        table.insert(parts, "WHERE " .. table.concat(conditions, " AND "))
    end
    if self._orderBy then table.insert(parts, "ORDER BY " .. self._orderBy) end
    if self._limit then table.insert(parts, "LIMIT " .. tostring(self._limit)) end
    if self._offset then table.insert(parts, "OFFSET " .. tostring(self._offset)) end
    return table.concat(parts, " ")
end

function QueryBuilder:build()
    return {
        sql = self:_build_select(),
        params = self._params
    }
end

return {
    new = QueryBuilder.new
}
