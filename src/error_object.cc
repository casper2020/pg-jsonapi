/**
 * @file error_object.cc Implementation of Error
 *
 * Copyright (c) 2011-2018 Cloudware S.A. All rights reserved.
 *
 * This file is part of pg-jsonapi.
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

#include "error_object.h"

/**
 * @brief Create a new error and initialize it with a status code.
 *
 * @param a_status The HTTP status code
 */
pg_jsonapi::ErrorObject::ErrorObject ( int a_sqlerrcode, HttpStatusErrorCode a_status, bool a_operation ) : sqlerrcode_(a_sqlerrcode), status_(a_status), operation_(a_operation)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %d", __FUNCTION__, status_)));

    /*
     * Attribute defaults
     */
    source_param_[0] = '\0';
}

/**
 * @brief Destructor.
 */
pg_jsonapi::ErrorObject::~ErrorObject ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %d", __FUNCTION__, status_)));

}

/**
 * @brief Set the error message detail.
 *
 * @param a_detail_message  A string to be used as final error message.
 */
pg_jsonapi::ErrorObject& pg_jsonapi::ErrorObject::SetMessage (const char* a_detail_message, const char* a_internal_msg_fmt, ...)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %d", __FUNCTION__, status_)));
    va_list args;
    char    internal_[1024];   // - extra information to be logged, we will not serialize because of SQL read injection

    if ( a_detail_message != NULL && strlen(a_detail_message) ) {
        detail_ = a_detail_message;
    } else {
        static pg_jsonapi::ErrorCode g_errcodes;    // static to only initialize once
        detail_ = g_errcodes.GetMessage(sqlerrcode_);
    }

    if ( a_internal_msg_fmt ) {
        va_start(args, a_internal_msg_fmt);
        vsnprintf(internal_, sizeof(internal_)-1, a_internal_msg_fmt, args);
        va_end(args);
        ereport(LOG, (errmsg_internal("pg_jsonapi ERROR: %s - internal: %s - detail: %s", unpack_sql_state(sqlerrcode_), internal_, detail_.c_str())));
    } else {
        ereport(DEBUG2, (errmsg_internal("pg_jsonapi ERROR: %s - detail: %s", unpack_sql_state(sqlerrcode_), detail_.c_str())));
    }

    return *this;
}

/**
 * @brief Set the source parameter that caused the error.
 *
 * @param a_fmt A format string to indicate which query parameter caused the error.
 */
pg_jsonapi::ErrorObject& pg_jsonapi::ErrorObject::SetSourceParam (const char* a_fmt,...)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %d", __FUNCTION__, status_)));

    va_list args;
    size_t len = sizeof(source_param_)-1;

    va_start(args, a_fmt);
    vsnprintf(source_param_, len, a_fmt, args);
    va_end(args);

    ereport(DEBUG2, (errmsg_internal("pg_jsonapi ERROR: %s - parameter: %s", unpack_sql_state(sqlerrcode_), source_param_)));
    return *this;
}

/**
 * @brief Serialize the error message.
 */
void pg_jsonapi::ErrorObject::Serialize (StringInfoData& a_response, bool a_open_common_errors) const
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %d", __FUNCTION__, status_)));

    appendStringInfo(&a_response, "{\"status\":\"%s\"", Status());
    if ( sqlerrcode_ ) {
        appendStringInfo(&a_response, ",\"code\":\"%s\"", unpack_sql_state(sqlerrcode_));
    }
    if ( detail_.size() ) {
        appendStringInfoString(&a_response, ",\"detail\":");
        escape_json(&a_response, detail_.c_str());
    }
    if ( links_about_.size() ) {
        appendStringInfo(&a_response, ",\"links\":{\"about\":\"%s\"}", links_about_.c_str());
    }
    if ( source_pointer_.size() || '\0' != source_param_[0] ) {
        const char* start = ",\"source\":{";
        if ( source_pointer_.size() ) {
            appendStringInfo(&a_response, "%s\"pointer\":\"%s\"", start, source_pointer_.c_str());
            start = ",";
        }
        if ( '\0' != source_param_[0] ) {
            appendStringInfo(&a_response, "%s\"parameter\":\"%s\"", start, source_param_);
        }
        appendStringInfoString(&a_response, "}");
    }
    if ( a_open_common_errors ) {
        appendStringInfoString(&a_response, ",\"meta\":{");
        // leave common-errors array open, meta and error need to be closed later
        appendStringInfo(&a_response, "\"common-errors\": [");
    } else {
        // close error
        appendStringInfoChar(&a_response, '}');
    }

    return;
}
