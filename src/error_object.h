/**
 * @file error_object.h Declaration of ErrorObject model node.
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
#pragma once
#ifndef CLD_PG_JSONAPI_ERROR_OBJECT_H
#define CLD_PG_JSONAPI_ERROR_OBJECT_H

#include <stdlib.h>
#include <string>
#include <vector>
#include "error_code.h"

extern "C" {
#include "postgres.h"
#include "utils/json.h"
} // extern "C"

namespace pg_jsonapi
{

    /**
     * @brief JSONAPI error object.
     */
    class ErrorObject
    {

    private: // Attributes
        int                 sqlerrcode_;       // - An application-specific error code, expressed as a string value. We are using PostgreSQL Error Codes.
        unsigned int status_;           // - The HTTP status code applicable to this problem, expressed as a string value.
        std::string         detail_;           // - A human-readable explanation specific to this occurrence of the problem.
        std::string         links_about_;      // - Links object containing a member about, that MAY lead to further details about this particular occurrence of the problem.

                         // source                - An object containing references to the source of the error, optionally including any of the following members:
        std::string         source_pointer_;   // - A JSON Pointer [RFC6901] to the associated entity in the request document [e.g. /data for a primary data object, or /data/attributes/title for a specific attribute].
        char                source_param_[256];// - An optional string indicating which query parameter caused the error.

        bool                operation_;        // - internal control of errors related to operations

    private: // Methods

        const char*         Status     () const;
        unsigned int StatusCode () const;

    public: // Methods

        ErrorObject (int a_sqlerrcode, unsigned int a_status, bool a_operation);
        virtual ~ErrorObject ();

        bool         IsOperation    () const;

        ErrorObject& SetMessage     (const char* a_detail_message, const char* a_internal_msg_fmt, ...) __attribute__((format(printf, 3, 4)));
        ErrorObject& SetSourceParam (const char* a_fmt,...) __attribute__((format(printf, 2, 3)));

        void         Serialize      (StringInfoData& a_response, bool a_open_common_errors = false) const;

    };

    typedef std::vector<ErrorObject>  ErrorVector;

    inline const char* ErrorObject::Status ( ) const
    {
        switch ( status_ ) {
            case E_HTTP_BAD_REQUEST:
                return "400 Bad Request";
            case E_HTTP_FORBIDDEN:
                return "403 Forbidden";
            case E_HTTP_NOT_FOUND:
                return "404 Not Found";
            case E_HTTP_CONFLICT:
                return "409 Conflict";
            case E_HTTP_INTERNAL_SERVER_ERROR:
            default:
                return "500 Internal Server Error";
        }
    }

    inline bool ErrorObject::IsOperation ( ) const
    {
        return operation_;
    }

} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_ERROR_OBJECT_H
