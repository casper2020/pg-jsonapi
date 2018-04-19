/**
 * @file error_code.h Declaration of default messages used per error code.
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
#ifndef CLD_PG_JSONAPI_ERROR_CODE_H
#define CLD_PG_JSONAPI_ERROR_CODE_H

#include <stdlib.h>
#include <string>
#include <map>

extern "C" {
#include "postgres.h"
} // extern "C"

namespace pg_jsonapi
{

    // related with HttpStatusCode
    typedef enum {
        E_HTTP_BAD_REQUEST            = 400,
        E_HTTP_FORBIDDEN              = 403,
        E_HTTP_NOT_FOUND              = 404,
        E_HTTP_CONFLICT               = 409,
        E_HTTP_INTERNAL_SERVER_ERROR  = 500,
    } HttpStatusErrorCode;

#define JSONAPI_MAKE_SQLSTATE(_errcode) MAKE_SQLSTATE(_errcode[0],_errcode[1],_errcode[2],_errcode[3],_errcode[4])
#define JSONAPI_ERRCODE_CATEGORY        ERRCODE_TO_CATEGORY(JSONAPI_MAKE_SQLSTATE("JA000"))

    typedef struct {
        const char              sqlerrcode_[5+1];
        HttpStatusErrorCode     status_;
        const char*             message_;
    } ErrorCodeMessage;

    /**
     * @brief JSONAPI default error code message object.
     */
    class ErrorCode
    {
    public: // types
        typedef struct {
            HttpStatusErrorCode     status_;
            const char*             message_;
        } ErrorCodeDetail;

    private: // Attributes
        typedef std::map<int, ErrorCodeDetail> ErrorCodeDetailMap;
        static ErrorCodeDetailMap     sql_error_map_;

    public: // Methods

        ErrorCode ();
        virtual ~ErrorCode ();

    public: // Methods

        ErrorCodeDetail     GetDetail(int a_sqlerrcode)  const;
        HttpStatusErrorCode GetStatus(int a_sqlerrcode)  const;
        const char*         GetMessage(int a_sqlerrcode) const;

    };


    inline ErrorCode::ErrorCodeDetail ErrorCode::GetDetail(int a_sqlerrcode) const
    {
        ErrorCodeDetailMap::iterator it = sql_error_map_.find(a_sqlerrcode);
        if ( sql_error_map_.end() == it ) {
            /* if resource does not exist, use default category */
            it = sql_error_map_.find(JSONAPI_ERRCODE_CATEGORY);
        }
        return it->second;
    }

    inline HttpStatusErrorCode ErrorCode::GetStatus(int a_sqlerrcode) const
    {
        ErrorCodeDetailMap::iterator it = sql_error_map_.find(a_sqlerrcode);
        if ( sql_error_map_.end() == it ) {
            /* if resource does not exist, use default category */
            it = sql_error_map_.find(JSONAPI_ERRCODE_CATEGORY);
        }
        return it->second.status_;
    }

    inline const char* ErrorCode::GetMessage(int a_sqlerrcode) const
    {
        ErrorCodeDetailMap::iterator it = sql_error_map_.find(a_sqlerrcode);
        if ( sql_error_map_.end() == it ) {
            /* if resource does not exist, use default category */
            it = sql_error_map_.find(JSONAPI_ERRCODE_CATEGORY);
        }
        return it->second.message_;
    }


} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_ERROR_CODE_H
