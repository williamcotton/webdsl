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
        _withMeta = false
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

function QueryBuilder:with_metadata()
    self._withMeta = true
    return self
end

function QueryBuilder:_build_select()
    if self._withMeta then
        return [[ SELECT 'data' as type, NULL::bigint as total_count, t.* FROM (]] .. self:_build_core() .. [[) t
        UNION ALL
        SELECT
            'metadata' as type,
            COUNT(*) OVER()::bigint as total_count,
            NULL::bigint as id,
            NULL::integer as team_id,
            NULL::text as name,
            NULL::text as email
        FROM (]] .. self:_build_core() .. [[) t ]]
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