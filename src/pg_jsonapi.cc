/**
 * @file pg_jsonapi.cc Implementation of main function of JSONAPI for PostgreSQL
 *
 * This file is part of pg-jsonapi.
 *
 * Copyright (c) 2011-2018 Cloudware S.A. All rights reserved.
 *
 * pg-jsonapi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * pg-jsonapi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with pg-jsonapi.  If not, see <http://www.gnu.org/licenses/>.
 */

extern "C" {
#include "postgres.h"
#include "executor/spi.h"
#include "utils/json.h"
#include "utils/builtins.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
} // extern "C"
#include "query_builder.h"

extern "C" {
PG_MODULE_MAGIC;
Datum   jsonapi(PG_FUNCTION_ARGS);
Datum   inside_jsonapi(PG_FUNCTION_ARGS);
Datum   get_jsonapi_user(PG_FUNCTION_ARGS);
Datum   get_jsonapi_company(PG_FUNCTION_ARGS);
Datum   get_jsonapi_company_schema(PG_FUNCTION_ARGS);
Datum   get_jsonapi_sharded_schema(PG_FUNCTION_ARGS);
Datum   get_jsonapi_accounting_schema(PG_FUNCTION_ARGS);
Datum   get_jsonapi_accounting_prefix(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(jsonapi);
PG_FUNCTION_INFO_V1(inside_jsonapi);
PG_FUNCTION_INFO_V1(get_jsonapi_user);
PG_FUNCTION_INFO_V1(get_jsonapi_company);
PG_FUNCTION_INFO_V1(get_jsonapi_company_schema);
PG_FUNCTION_INFO_V1(get_jsonapi_sharded_schema);
PG_FUNCTION_INFO_V1(get_jsonapi_accounting_schema);
PG_FUNCTION_INFO_V1(get_jsonapi_accounting_prefix);
} // extern "C"

/* Main request */
pg_jsonapi::QueryBuilder* g_qb = NULL;

extern "C" {

/**
 * @brief Init query builder needed to process the request.
 */
static
void jsonapi_initqb()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s g_qb=%p", __FUNCTION__, g_qb)));

    if ( NULL == g_qb ) {
        g_qb = new pg_jsonapi::QueryBuilder();
        if ( NULL == g_qb ) {
            ereport(FATAL, (errcode(ERRCODE_OUT_OF_MEMORY), errmsg("jsonapi: out of memory")));
        }
        ereport(DEBUG3, (errmsg_internal("jsonapi: %s g_qb=%p", __FUNCTION__, g_qb)));
    }
    std::string config_file = "/etc/pg-jsonapi/modsec_includes.conf";
    try {
        g_qb->InitModSecurity(config_file);
    } catch (...) {
        ereport(FATAL, (errcode(ERRCODE_INTERNAL_ERROR), errmsg("jsonapi: Unable to load %s", config_file.c_str())));
    }
}

/**
 * @brief Reset query builder needed to process the request.
 */
static
void jsonapi_resetqb()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s g_qb=%p", __FUNCTION__, g_qb)));

    if ( NULL == g_qb ) {
        jsonapi_initqb();
    } else {
        g_qb->Clear();
    }
}

static void
jsonapi_common(text* a_method,
               text* a_url,
               text* a_body,
               text* a_user_id,
               text* a_company_id,
               text* a_company_schema,
               text* a_sharded_schema,
               text* a_accounting_schema,
               text* a_accounting_prefix)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( ! g_qb->HasErrors() ) {
        /* connect to SPI manager on this level to start the memory context */
        if ( g_qb->SPIConnect() ) {
            try {
                /* parse request arguments */
                if ( g_qb->ParseRequestArguments(
                                                 VARDATA(a_method), VARSIZE_ANY_EXHDR(a_method),
                                                 VARDATA(a_url),    VARSIZE_ANY_EXHDR(a_url),
                                                 (a_body) ? VARDATA(a_body) : NULL, (a_body) ? VARSIZE_ANY_EXHDR(a_body) : 0,
                                                 (a_user_id) ? VARDATA(a_user_id) : NULL, (a_user_id) ? VARSIZE_ANY_EXHDR(a_user_id) : 0,
                                                 (a_company_id) ? VARDATA(a_company_id) : NULL, (a_company_id) ? VARSIZE_ANY_EXHDR(a_company_id) : 0,
                                                 (a_company_schema) ? VARDATA(a_company_schema) : NULL, (a_company_schema) ? VARSIZE_ANY_EXHDR(a_company_schema) : 0,
                                                 (a_sharded_schema) ? VARDATA(a_sharded_schema) : NULL, (a_sharded_schema) ? VARSIZE_ANY_EXHDR(a_sharded_schema) : 0,
                                                 (a_accounting_schema) ? VARDATA(a_accounting_schema) : NULL, (a_accounting_schema) ? VARSIZE_ANY_EXHDR(a_accounting_schema) : 0,
                                                 (a_accounting_prefix) ? VARDATA(a_accounting_prefix) : NULL, (a_accounting_prefix) ? VARSIZE_ANY_EXHDR(a_accounting_prefix) : 0) ) {
                    /* validate request according to configuration */
                    if ( g_qb->ValidateRequest() ) {
                        if ( ! g_qb->IsTopQueryFromJobTube() ) {
                            if( "GET" == g_qb->GetRequestMethod() ) {
                                /* fetch the data and serialize response */
                                g_qb->FetchData();
                            } else {
                                /* execute operations */
                                g_qb->ExecuteOperations();
                            }
                        }
                    }
                }
            } catch (const std::runtime_error& a_rte) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA006"), pg_jsonapi::E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "%s", a_rte.what());
            } catch (...) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA006"), pg_jsonapi::E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "exception...");
            }
        }
    }

    return;
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @param method            The http request method: GET, POST, PATCH or DELETE.
 * @param url               The request url.
 * @param body              The request body.
 * @param user_id           The user identification.
 * @param company_id        The company identification.
 * @param company_schema    The schema to be used, if flag request-company-schema is true (we know it's redundant with schema argument).
 * @param sharded_schema    The schema to be used, if flag request-sharded-schema is true (we know it's redundant with schema argument).
 * @param accounting_schema The schema to be used, if flag request-accounting-schema is true (default).
 * @param accounting_prefix The prefix to be added to define the resource relation.
 *
 * @return A top-level JSON document containing data or errors.
 */
Datum
jsonapi(PG_FUNCTION_ARGS)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s PG_NARGS:%d", __FUNCTION__, PG_NARGS())));

    StringInfoData response;
    TupleDesc      tupdesc;
    Datum          values[2];
    bool           nulls[2];

    /* Initialise attributes information in the tuple descriptor */
    tupdesc = CreateTemplateTupleDesc(2, false);
    TupleDescInitEntry(tupdesc, (AttrNumber) 1, "http_status", INT4OID, -1, 0);
    TupleDescInitEntry(tupdesc, (AttrNumber) 2, "response"   , TEXTOID, -1, 0);
    nulls[0] = nulls[1] = false;
    BlessTupleDesc(tupdesc);
    initStringInfo(&response);
    appendStringInfo(&response, "%*.*s", VARHDRSZ, VARHDRSZ, "~~~~~~~~~~");
    jsonapi_resetqb();

    if ( PG_NARGS() != 9 || PG_ARGISNULL(0) || PG_ARGISNULL(1) ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA010"), pg_jsonapi::E_HTTP_BAD_REQUEST).SetMessage(NULL, "Expected arguments are: ( method, url, body , user_id, company_id, company_schema, sharded_schema, accounting_schema, accounting_prefix )");
    }

    if ( ! g_qb->HasErrors() ) {
        // non-optional parameters
        text*   method          = PG_GETARG_TEXT_PP(0);
        text*   url             = PG_GETARG_TEXT_PP(1);

        // optional parameters
        text*   body                = (PG_NARGS() <= 2 || PG_ARGISNULL(2)) ? NULL : PG_GETARG_TEXT_PP(2);
        text*   user_id             = (PG_NARGS() <= 3 || PG_ARGISNULL(3)) ? NULL : PG_GETARG_TEXT_PP(3);
        text*   company_id          = (PG_NARGS() <= 4 || PG_ARGISNULL(4)) ? NULL : PG_GETARG_TEXT_PP(4);
        text*   company_schema      = (PG_NARGS() <= 5 || PG_ARGISNULL(5)) ? NULL : PG_GETARG_TEXT_PP(5);
        text*   sharded_schema      = (PG_NARGS() <= 6 || PG_ARGISNULL(6)) ? NULL : PG_GETARG_TEXT_PP(6);
        text*   accounting_schema   = (PG_NARGS() <= 7 || PG_ARGISNULL(7)) ? NULL : PG_GETARG_TEXT_PP(7);
        text*   accounting_prefix   = (PG_NARGS() <= 8 || PG_ARGISNULL(8)) ? NULL : PG_GETARG_TEXT_PP(8);

        jsonapi_common(method, url, body, user_id, company_id, company_schema, sharded_schema, accounting_schema, accounting_prefix);
    }

    /* serialize the results */
    g_qb->SerializeResponse(response);

    /* return from jsonapi */
    ereport(g_qb->HasErrors() ? LOG : DEBUG1, (errmsg_internal("jsonapi: http_status:%d response: %.*s", g_qb->GetHttpStatus(), response.len-VARHDRSZ,  response.data+VARHDRSZ)));
    SET_VARSIZE(response.data, response.len + VARHDRSZ);
    values[0] = Int32GetDatum(g_qb->GetHttpStatus());
    values[1] = PointerGetDatum(response.data);

    /* disconnect from SPI manager only after serialization because of memory context and HTTP status */
    g_qb->SPIDisconnect();
    g_qb->Clear();

    PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @return TRUE if we are in function inside a jsonapi() or jsonapi_with_status().
 */
Datum
inside_jsonapi(PG_FUNCTION_ARGS)
{
    jsonapi_initqb();
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, g_qb->SPIIsConnected() ? "true" : "false")));
    if ( g_qb->SPIIsConnected() ){
        PG_RETURN_BOOL(true);
    } else {
        PG_RETURN_BOOL(false);
    }
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @return schema if we are in function inside a jsonapi() or jsonapi_with_status().
 */
Datum
get_jsonapi_accounting_schema(PG_FUNCTION_ARGS)
{
    jsonapi_initqb();
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, g_qb->GetRequestAccountingSchema().c_str())));
    if ( g_qb->SPIIsConnected() ){
        PG_RETURN_TEXT_P(cstring_to_text(g_qb->GetRequestAccountingSchema().c_str()));
    } else {
        PG_RETURN_NULL();
    }
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @return accounting_prefix if we are in function inside a jsonapi() or jsonapi_with_status().
 */
Datum
get_jsonapi_accounting_prefix(PG_FUNCTION_ARGS)
{
    jsonapi_initqb();
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, g_qb->GetRequestAccountingPrefix().c_str())));
    if ( g_qb->SPIIsConnected() ){
        PG_RETURN_TEXT_P(cstring_to_text(g_qb->GetRequestAccountingPrefix().c_str()));
    } else {
        PG_RETURN_NULL();
    }
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @return company_schema if we are in function inside a jsonapi() or jsonapi_with_status().
 */
Datum
get_jsonapi_company_schema(PG_FUNCTION_ARGS)
{
    jsonapi_initqb();
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, g_qb->GetRequestCompanySchema().c_str())));
    if ( g_qb->SPIIsConnected() ){
        PG_RETURN_TEXT_P(cstring_to_text(g_qb->GetRequestCompanySchema().c_str()));
    } else {
        PG_RETURN_NULL();
    }
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @return sharded_schema if we are in function inside a jsonapi() or jsonapi_with_status().
 */
Datum
get_jsonapi_sharded_schema(PG_FUNCTION_ARGS)
{
    jsonapi_initqb();
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, g_qb->GetRequestShardedSchema().c_str())));
    if ( g_qb->SPIIsConnected() ){
        PG_RETURN_TEXT_P(cstring_to_text(g_qb->GetRequestShardedSchema().c_str()));
    } else {
        PG_RETURN_NULL();
    }
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @return user_id if we are in function inside a jsonapi() or jsonapi_with_status().
 */
Datum
get_jsonapi_user(PG_FUNCTION_ARGS)
{
    jsonapi_initqb();
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, g_qb->GetRequestUser().c_str())));
    if ( g_qb->SPIIsConnected() ){
        PG_RETURN_TEXT_P(cstring_to_text(g_qb->GetRequestUser().c_str()));
    } else {
        PG_RETURN_NULL();
    }
}

/**
 * @brief JSONAPI interface to PostreSQL
 *
 * @return company_id if we are in function inside a jsonapi() or jsonapi_with_status().
 */
Datum
get_jsonapi_company(PG_FUNCTION_ARGS)
{
    jsonapi_initqb();
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, g_qb->GetRequestCompany().c_str())));
    if ( g_qb->SPIIsConnected() ){
        PG_RETURN_TEXT_P(cstring_to_text(g_qb->GetRequestCompany().c_str()));
    } else {
        PG_RETURN_NULL();
    }
}

} // extern "C" functions
