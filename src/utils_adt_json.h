/**
 * @file utils_adt_json.h
 * Including code from PostgreSQL 9.4.4 to use some internal functions.
 * Minor changes were made.
 * Declaring funtions and types on this header file.
 *
 */

/*-------------------------------------------------------------------------
 *
 * json.c
 *      JSON data type support.
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *    src/backend/utils/adt/json.c
 *
 *-------------------------------------------------------------------------
 */

#pragma once
#ifndef CLD_PG_JSONAPI_UTILS_ADT_JSON_H
#define CLD_PG_JSONAPI_UTILS_ADT_JSON_H

#include <string>
extern "C" {
#include "postgres.h"
#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/json.h"
#include "utils/lsyscache.h"
} // extern "C"

namespace pg_jsonapi
{

    typedef enum                    /* type categories for datum_to_json */
    {
        JSONTYPE_NULL,              /* null, so we didn't bother to identify */
        JSONTYPE_BOOL,              /* boolean (built-in types only) */
        JSONTYPE_NUMERIC,           /* numeric (ditto) */
        JSONTYPE_DATE,              /* we use special formatting for datetimes */
        JSONTYPE_TIMESTAMP,
        JSONTYPE_TIMESTAMPTZ,
        JSONTYPE_JSON,              /* JSON itself (and JSONB) */
        JSONTYPE_ARRAY,             /* array */
        JSONTYPE_COMPOSITE,         /* composite */
        JSONTYPE_CAST,              /* something with an explicit cast to JSON */
        JSONTYPE_OTHER              /* all else */
    } JsonTypeCategory;

    void json_categorize_type   (Oid typoid, JsonTypeCategory *tcategory,
                                 Oid *outfuncoid);

    void composite_to_json      (Datum composite, StringInfo result, bool use_line_feeds);

    void datum_to_json          (Datum val, bool is_null, StringInfo result,
                                 JsonTypeCategory tcategory, Oid outfuncoid,
                                 bool key_scalar);

    void array_dim_to_json      (StringInfo result, int dim, int ndims, int *dims, Datum *vals,
                                 bool *nulls, int *valcount, JsonTypeCategory tcategory,
                                 Oid outfuncoid, bool use_line_feeds);

    void array_to_json_internal (Datum array, StringInfo result, bool use_line_feeds);

    class Utils
    {

    public:

        static std::string urlDecode (const char* a_url, size_t a_url_len);

    };

} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_UTILS_ADT_JSON_H
