/**
 * @file utils_adt_json.cc
 * Including code from PostgreSQL 9.4.4 to use some internal functions.
 * Minor changes were made.
 *
 */

/*-------------------------------------------------------------------------
 *
 * json.c
 *		JSON data type support.
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/json.c
 *
 *-------------------------------------------------------------------------
 */

#include "utils_adt_json.h"

extern "C" {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#include "access/htup_details.h"
#include "access/transam.h"
#include "catalog/pg_cast.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "libpq/pqformat.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/datetime.h"
#if PG_MAJORVERSION_NUM >= 15
#include "common/jsonapi.h"
#else
#include "utils/jsonapi.h"
#endif
#include "utils/typcache.h"
#include "utils/syscache.h"
#pragma GCC diagnostic pop
} // extern "C"


/* String to output for infinite dates and timestamps */
#define DT_INFINITY "\"infinity\""

/*
 * Determine how we want to print values of a given type in datum_to_json.
 *
 * Given the datatype OID, return its JsonTypeCategory, as well as the type's
 * output function OID.  If the returned category is JSONTYPE_CAST, we
 * return the OID of the type->JSON cast function instead.
 */
void
pg_jsonapi::json_categorize_type(Oid typoid,
                     JsonTypeCategory *tcategory,
                     Oid *outfuncoid)
{
    bool		typisvarlena;

    /* Look through any domain */
    typoid = getBaseType(typoid);

    /* We'll usually need to return the type output function */
    getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);

    /* Check for known types */
    switch (typoid)
    {
        case BOOLOID:
            *tcategory = JSONTYPE_BOOL;
            break;

        case INT2OID:
        case INT4OID:
        case INT8OID:
        case FLOAT4OID:
        case FLOAT8OID:
        case NUMERICOID:
            *tcategory = JSONTYPE_NUMERIC;
            break;

        case DATEOID:
            *tcategory = JSONTYPE_DATE;
            break;

        case TIMESTAMPOID:
            *tcategory = JSONTYPE_TIMESTAMP;
            break;

        case TIMESTAMPTZOID:
            *tcategory = JSONTYPE_TIMESTAMPTZ;
            break;

        case JSONOID:
        case JSONBOID:
            *tcategory = JSONTYPE_JSON;
            break;

        default:
            /* Check for arrays and composites */
            if (OidIsValid(get_element_type(typoid)))
                *tcategory = JSONTYPE_ARRAY;
            else if (type_is_rowtype(typoid))
                *tcategory = JSONTYPE_COMPOSITE;
            else
            {
                /* It's probably the general case ... */
                *tcategory = JSONTYPE_OTHER;
                /* but let's look for a cast to json, if it's not built-in */
                if (typoid >= FirstNormalObjectId)
                {
                    HeapTuple	tuple;

                    tuple = SearchSysCache2(CASTSOURCETARGET,
                                            ObjectIdGetDatum(typoid),
                                            ObjectIdGetDatum(JSONOID));
                    if (HeapTupleIsValid(tuple))
                    {
                        Form_pg_cast castForm = (Form_pg_cast) GETSTRUCT(tuple);

                        if (castForm->castmethod == COERCION_METHOD_FUNCTION)
                        {
                            *tcategory = JSONTYPE_CAST;
                            *outfuncoid = castForm->castfunc;
                        }

                        ReleaseSysCache(tuple);
                    }
                }
            }
            break;
    }
}


/*
 * Turn a composite / record into JSON.
 */
void
pg_jsonapi::composite_to_json(Datum composite, StringInfo result, bool use_line_feeds)
{
    HeapTupleHeader td;
    Oid			tupType;
    int32		tupTypmod;
    TupleDesc	tupdesc;
    HeapTupleData tmptup,
    *tuple;
    int			i;
    bool		needsep = false;
    const char *sep;

    sep = use_line_feeds ? ",\n " : ",";

    td = DatumGetHeapTupleHeader(composite);

    /* Extract rowtype info and find a tupdesc */
    tupType = HeapTupleHeaderGetTypeId(td);
    tupTypmod = HeapTupleHeaderGetTypMod(td);
    tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);

    /* Build a temporary HeapTuple control structure */
    tmptup.t_len = HeapTupleHeaderGetDatumLength(td);
    tmptup.t_data = td;
    tuple = &tmptup;

    appendStringInfoChar(result, '{');

    for (i = 0; i < tupdesc->natts; i++)
    {
        Datum		val;
        bool		isnull;
        char	   *attname;
        JsonTypeCategory tcategory;
        Oid			outfuncoid;

        if (TupleDescAttr(tupdesc,i)->attisdropped)
            continue;

        if (needsep)
            appendStringInfoString(result, sep);
        needsep = true;

        attname = NameStr(TupleDescAttr(tupdesc,i)->attname);
        escape_json(result, attname);
        appendStringInfoChar(result, ':');

        val = heap_getattr(tuple, i + 1, tupdesc, &isnull);

        if (isnull)
        {
            tcategory = JSONTYPE_NULL;
            outfuncoid = InvalidOid;
        }
        else
            json_categorize_type(TupleDescAttr(tupdesc,i)->atttypid,
                                 &tcategory, &outfuncoid);

        datum_to_json(val, isnull, result, tcategory, outfuncoid, false);
    }

    appendStringInfoChar(result, '}');
    ReleaseTupleDesc(tupdesc);
}

/*
 * Turn a Datum into JSON text, appending the string to "result".
 *
 * tcategory and outfuncoid are from a previous call to json_categorize_type,
 * except that if is_null is true then they can be invalid.
 *
 * If key_scalar is true, the value is being printed as a key, so insist
 * it's of an acceptable type, and force it to be quoted.
 */
void
pg_jsonapi::datum_to_json(Datum val, bool is_null, StringInfo result,
              JsonTypeCategory tcategory, Oid outfuncoid,
              bool key_scalar)
{
    char	   *outputstr;
    const char *outputstr_c;
    text	   *jsontext;

    /* callers are expected to ensure that null keys are not passed in */
    Assert(!(key_scalar && is_null));

    if (is_null)
    {
        appendStringInfoString(result, "null");
        return;
    }

    if (key_scalar &&
        (tcategory == JSONTYPE_ARRAY ||
         tcategory == JSONTYPE_COMPOSITE ||
         tcategory == JSONTYPE_JSON ||
         tcategory == JSONTYPE_CAST))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("key value must be scalar, not array, composite, or json")));

    switch (tcategory)
    {
        case JSONTYPE_ARRAY:
            array_to_json_internal(val, result, false);
            break;
        case JSONTYPE_COMPOSITE:
            composite_to_json(val, result, false);
            break;
        case JSONTYPE_BOOL:
            outputstr_c = DatumGetBool(val) ? "true" : "false";
            if (key_scalar)
                escape_json(result, outputstr_c);
            else
                appendStringInfoString(result, outputstr_c);
            break;
        case JSONTYPE_NUMERIC:
            outputstr = OidOutputFunctionCall(outfuncoid, val);

            /*
             * Don't call escape_json for a non-key if it's a valid JSON
             * number.
             */
            if (!key_scalar && IsValidJsonNumber(outputstr, strlen(outputstr)))
                appendStringInfoString(result, outputstr);
            else
                escape_json(result, outputstr);
            pfree(outputstr);
            break;
        case JSONTYPE_DATE:
        {
            DateADT		date;
            struct pg_tm tm;
            char		buf[MAXDATELEN + 1];

            date = DatumGetDateADT(val);

            if (DATE_NOT_FINITE(date))
            {
                /* we have to format infinity ourselves */
                appendStringInfoString(result,DT_INFINITY);
            }
            else
            {
                j2date(date + POSTGRES_EPOCH_JDATE,
                       &(tm.tm_year), &(tm.tm_mon), &(tm.tm_mday));
                EncodeDateOnly(&tm, USE_XSD_DATES, buf);
                appendStringInfo(result, "\"%s\"", buf);
            }
        }
            break;
        case JSONTYPE_TIMESTAMP:
        {
            Timestamp	timestamp;
            struct pg_tm tm;
            fsec_t		fsec;
            char		buf[MAXDATELEN + 1];

            timestamp = DatumGetTimestamp(val);

            if (TIMESTAMP_NOT_FINITE(timestamp))
            {
                /* we have to format infinity ourselves */
                appendStringInfoString(result,DT_INFINITY);
            }
            else if (timestamp2tm(timestamp, NULL, &tm, &fsec, NULL, NULL) == 0)
            {
                EncodeDateTime(&tm, fsec, false, 0, NULL, USE_XSD_DATES, buf);
                appendStringInfo(result, "\"%s\"", buf);
            }
            else
                ereport(ERROR,
                        (errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
                         errmsg("timestamp out of range")));
        }
            break;
        case JSONTYPE_TIMESTAMPTZ:
        {
            TimestampTz timestamp;
            struct pg_tm tm;
            int			tz;
            fsec_t		fsec;
            const char *tzn = NULL;
            char		buf[MAXDATELEN + 1];

            timestamp = DatumGetTimestamp(val);

            if (TIMESTAMP_NOT_FINITE(timestamp))
            {
                /* we have to format infinity ourselves */
                appendStringInfoString(result,DT_INFINITY);
            }
            else if (timestamp2tm(timestamp, &tz, &tm, &fsec, &tzn, NULL) == 0)
            {
                EncodeDateTime(&tm, fsec, true, tz, tzn, USE_XSD_DATES, buf);
                appendStringInfo(result, "\"%s\"", buf);
            }
            else
                ereport(ERROR,
                        (errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
                         errmsg("timestamp out of range")));
        }
            break;
        case JSONTYPE_JSON:
            /* JSON and JSONB output will already be escaped */
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            appendStringInfoString(result, outputstr);
            pfree(outputstr);
            break;
        case JSONTYPE_CAST:
            /* outfuncoid refers to a cast function, not an output function */
            jsontext = DatumGetTextP(OidFunctionCall1(outfuncoid, val));
            outputstr = text_to_cstring(jsontext);
            appendStringInfoString(result, outputstr);
            pfree(outputstr);
            pfree(jsontext);
            break;
        default:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            escape_json(result, outputstr);
            pfree(outputstr);
            break;
    }
}

/*
 * Process a single dimension of an array.
 * If it's the innermost dimension, output the values, otherwise call
 * ourselves recursively to process the next dimension.
 */
void
pg_jsonapi::array_dim_to_json(StringInfo result, int dim, int ndims, int *dims, Datum *vals,
                  bool *nulls, int *valcount, JsonTypeCategory tcategory,
                  Oid outfuncoid, bool use_line_feeds)
{
    int			i;
    const char *sep;

    Assert(dim < ndims);

    sep = use_line_feeds ? ",\n " : ",";

    appendStringInfoChar(result, '[');

    for (i = 1; i <= dims[dim]; i++)
    {
        if (i > 1)
            appendStringInfoString(result, sep);

        if (dim + 1 == ndims)
        {
            datum_to_json(vals[*valcount], nulls[*valcount], result, tcategory,
                          outfuncoid, false);
            (*valcount)++;
        }
        else
        {
            /*
             * Do we want line feeds on inner dimensions of arrays? For now
             * we'll say no.
             */
            array_dim_to_json(result, dim + 1, ndims, dims, vals, nulls,
                              valcount, tcategory, outfuncoid, false);
        }
    }

    appendStringInfoChar(result, ']');
}

/*
 * Turn an array into JSON.
 */
void
pg_jsonapi::array_to_json_internal(Datum array, StringInfo result, bool use_line_feeds)
{
    ArrayType  *v = DatumGetArrayTypeP(array);
    Oid			element_type = ARR_ELEMTYPE(v);
    int		   *dim;
    int			ndim;
    int			nitems;
    int			count = 0;
    Datum	   *elements;
    bool	   *nulls;
    int16		typlen;
    bool		typbyval;
    char		typalign;
    JsonTypeCategory tcategory;
    Oid			outfuncoid;

    ndim = ARR_NDIM(v);
    dim = ARR_DIMS(v);
    nitems = ArrayGetNItems(ndim, dim);

    if (nitems <= 0)
    {
        appendStringInfoString(result, "[]");
        return;
    }

    get_typlenbyvalalign(element_type,
                         &typlen, &typbyval, &typalign);

    json_categorize_type(element_type,
                         &tcategory, &outfuncoid);

    deconstruct_array(v, element_type, typlen, typbyval,
                      typalign, &elements, &nulls,
                      &nitems);

    array_dim_to_json(result, 0, ndim, dim, elements, nulls, &count, tcategory,
                      outfuncoid, use_line_feeds);

    pfree(elements);
    pfree(nulls);
}

/**
 * @brief URL decode function.
 *
 * @li a_url The url to decode.
 * @li a_url_len The length of the url to decode.
 *
 * @return Decoded string.
 */
std::string pg_jsonapi::Utils::urlDecode(const char* a_url, size_t a_url_len) {

    std::string url = std::string(a_url, 0, a_url_len);
    std::string ret;
    char ch;
    size_t i;
    int j;
    for ( i=0; i < a_url_len; i++ ) {
        if ( 37 == int(a_url[i])) {
            sscanf(url.substr(i+1,2).c_str(), "%x", &j);
            ch = static_cast<char>(j);
            ret += ch;
            i = i+2;
        } else {
            ret += a_url[i];
        }
    }
    ereport(DEBUG4, (errmsg_internal("urlDecode: %s", ret.c_str())));
    return (ret);
}

/**
 * @brief Collapses consecutive spaces on query, except on text between quotes.
 *
 * @li a_query The string with spaces to be collapsed.
 *
 * @return String without duplicate spaces.
 */
std::string pg_jsonapi::Utils::collapseQuerySpaces(const std::string& a_query) {

    size_t original_len = a_query.length();
    std::string ret;
    size_t i = 0;
    while ( i < original_len ) {
        ret += a_query[i];
        i++;
        if ( '\'' == a_query[i-1] ) {
            /* copy everything until next single quote */
            while ( i < original_len && '\'' != a_query[i] )
            {
                ret += a_query[i];
                i++;
            }
            if ( i < original_len && '\'' == a_query[i] ) {
                ret += a_query[i];
                i++;
            }
        } else if ( ' ' == a_query[i-1] && i < original_len && ' ' == a_query[i] ) {
            do {
                i++;
            } while ( i < original_len && ' ' == a_query[i] );
        }
    }
    ereport(DEBUG4, (errmsg_internal("collapseQuerySpaces: %s", ret.c_str())));
    return ret;
}
