CREATE OR REPLACE FUNCTION parse_optional(raw_value text, template anyelement) 
RETURNS anyelement AS $$
BEGIN
    IF raw_value = '{}' OR raw_value IS NULL THEN
        RETURN NULL;
    END IF;
    EXECUTE format('SELECT %L::%s', raw_value, pg_typeof(template)) INTO template;
    RETURN template;
EXCEPTION WHEN OTHERS THEN
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION filter_by_optional(
    field anyelement,
    param text
) RETURNS boolean AS $$
BEGIN
    RETURN field = parse_optional(param, field) 
        OR parse_optional(param, field) IS NULL;
END;
$$ LANGUAGE plpgsql;
