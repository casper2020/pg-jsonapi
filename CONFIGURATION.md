## Configuration

Table `public.jsonapi_config` may be used to configure the `jsonapi` function behaviour.

Each support URL prefix (HTTP schema and host) may have a JSON configuration with global options and valid `resources`.
If there isn't a specific configuration for the request prefix, it will be checked if there's a fallback configuration for prefix 'default'.
Fields not defined as to-one or to-many relationships are automatically assumed to be attributes.


## Global Options

Options may be optionally specified, if not present default values will be used.

### `version`

Boolean value to define if `version` should be prensent in top level `jsonapi` member.
Default is `true`.

### `compound`

Boolean value to define if related objects should be included.
Default is `true`.

### `show-links`

Boolean value to define if `links` objects should be present, may be specified on resource level.
Default is `true`.


### `show-null`

Boolean value to define if `field` should be displayed when it has a null value, may be specified on resource level.
Default is `true`.

### `page-size`

Integer value to define default `page-size` for requests targetting resource collections, may be specified on resource level.
Default is `1000`.

### `type-restriction`

Boolean value to define if resources are restricted to members defined under `resources`, or if they can be considered with default values.
Default is `false`.

### `attribute-restriction`

Boolean value to define if resources fields are restricted to the ones specified in case of missing member `attributes`.
If not restricted all columns not specified as relationships will be considered attributes.
Default is `false`.

### `empty-is-null`

Boolean value to define if empty values should be considered null for relationships.
Empty values returned for relationships will be considered an error if this option is set to `false`, and will be ignored if set to `true`.
Default is `false`.

### `request-schema`

Boolean value to define if the schema sent as function second argument should be considered by default for each resource.
Default is `true`.
This global flag is used as default for all resources, but may be overridden for each resource.
If true, `request-sharded-schema` and  `request-company-schema` cannot be provided.

### `request-accounting-prefix`

Boolean value to define if the table prefix sent as function third argument should be considered by default for each resource.
Default is `false`.
This global flag is used as default for all resources, but may be overridden for each resource.

### `request-sharded-schema`

Boolean value to define if the schema sent as function fourth argument should be considered by default for each resource.
Default is `false`.
This global flag is used as default for all resources, but may be overridden for each resource.
If true, `request-schema` and `request-company-schema` cannot be provided.

### `request-company-schema`

Boolean value to define if the schema sent as function fifth argument should be considered by default for each resource.
Default is `false`.
This global flag is used as default for all resources, but may be overridden for each resource.
If true, `request-schema` and `request-sharded-schema` cannot be provided.

### `pg-order-by`

String containing sort criteria to be used on resources and relationships.
Default is not defined.
This global flag is used as default for all resources, but may be overridden for each resource.

### `pg-search_path`

String containing search_path 'template' to be used in jsonapi function calls.
This must be provided as it would be used on postgres configuration (https://www.postgresql.org/docs/current/static/config-setting.html).
If string contains 'request-schema' or 'request-sharded-schema' or 'request-compay-schema' then these keywords will be replaced by function arguments to obtain the search_path to be set.
If defined (and not empty), search_path will be changed and reset to previous value before exiting jsonapi function.
This is a global setting, but is only set if needed in current request.

## Resources

Member `resources` contains a list of resource objects specification.
Non specified resources will be considered depending on `restriced-types` mode.

Each specified resource object contains the resource type as key and the resource fields are specified in objects per type: `attributes`, `to-one` and `to-many`.

Postgres configuration for each resource is specified on `pg-*` fields, these fields may be omitted and default values will be used.
Configurations will NOT be obtained automatically from table descriptions.
For each resource object there must be at least a table with one column containing the resource id.

Each resource may also contain an extra object in their configuration: `observed`, which contains a list of related resources (tables) that need to be controlled in case there are changes as side of effect of the main request operation. For each observed resource may be defined the name to be used on the response.
When there are observed resources, the resource object on the response document will contain an `observed` object, inside the `meta` member, containing a boolean field for each observed resource.

### `pg-schema`

Name of the schema to be used.
If not present, and `request-schema` is true than schema argument is used, otherwise if `request-sharded-schema` is true than sharded_schema argument is used, otherwise if `request-company-schema` is true than company_schema argument is used.

### `request-schema`

Defines behavior of schema usage for the resource, overriding global option value.
If true, the `pg-schema`, `request-sharded-schema` and `request-company-schema` cannot be provided.

### `request-sharded-schema`

Defines behavior of schema usage for the resource, overriding global option value.
If true, the `pg-schema`, `request-schema` and `request-company-schema` cannot be provided.

### `request-company-schema`

Defines behavior of schema usage for the resource, overriding global option value.
If true, the `pg-schema`, `request-schema` and `request-sharded-schema` cannot be provided.

### `pg-table`

Name of the table (or other valid relation like a view).
Default is the resource type.

### `request-accounting-prefix`

Defines the behavior of table prefix usage for the resource, overriding global option value.
If true, table name will be concatenation of request table prefix argument and the `pg-table`.

### `pg-id`

Name of the column that contains the resource identification.
Default is `id`.

### `attributes`

### `to-one`

### `to-many`


### `pg-parent-id`

Name of the column that contains the parent resource identification.
Default is `<parent_resource>_id`.

### `pg-child-id`

Name of the column that contains the child resource identification.
Default is `<child_resource>_id`.

### `pg-order-by`

Default sort criteria, will NOT be used when `sort` param is specified on request.

### `pg-attributes-function`

Name of the function to be used to define the list of output attributes (function must be schema qualified)

### `job-tube`

The document must be processed by a job, not inside PostgreSQL

### `job-methods`

The specified methods must be processed using the job specified in `job-tube`, by default all methods will be supported by the job

### `job-ttr`

Job time to run, number of seconds that job may be running, otherwise will be canceled with timeout, there is no default in jsonapi configuration.

### `job-validity`

Job validity in seconds, time to wait until it starts being processed, there is no default in jsonapi configuration.
