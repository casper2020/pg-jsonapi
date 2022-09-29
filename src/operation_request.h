/**
 * @file operation_request.h Declaration of OperationRequest model node.
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
#ifndef CLD_PG_JSONAPI_OPERATION_REQUEST_H
#define CLD_PG_JSONAPI_OPERATION_REQUEST_H

#include <stdlib.h>
#include <string>

#include "json/json.h"

#include "resource_data.h"
#include "error_object.h"
#include "observed_stat.h"
#include "utils_adt_json.h"

namespace pg_jsonapi
{
    typedef enum {
        E_OP_UNDEFINED,
        E_OP_CREATE,
        E_OP_UPDATE,
        E_OP_DELETE
    } OperationType;

    typedef std::vector<size_t> SizeVector;

    /**
     * @brief Generic model to keep basic, BULK extension or JSON Patch requests.
     */
    class OperationRequest
    {

    private: // Attributes - request variables filled while parsing request
        int                         rq_index_;
        OperationType               rq_type_;
        std::string                 rq_resource_type_;
        std::string                 rq_resource_id_;
        std::string                 rq_related_;
        bool                        rq_relationship_;
        std::string                 rq_attribute_;
        const JsonapiJson::Value*   rq_body_data_;

    private: // Attributes - used to query postgres and keep results
        std::string        q_buffer_;
        size_t             q_required_count_;
        SizeVector         error_index_;
        ObservedStatMap    q_observed_stat_;

    private: // Methods

        bool ParsePath (std::string a_patch_path);

        bool BodyHasValidResourceData();
        bool BodyHasValidAttributes(const JsonapiJson::Value& a_value);
        bool BodyHasValidRelationships(const JsonapiJson::Value& a_value);
        bool BodyHasValidRelationshipData(const JsonapiJson::Value& a_value);

        void GetArrayAsSQLValue(const JsonapiJson::Value& a_value, std::string& a_sql_value);

        const std::string& GetResourceInsertCmd();
        const std::string& GetResourceUpdateCmd();
        const std::string& GetResourceDeleteCmd();
        const std::string& GetFieldUpdateCmd               (const std::string& a_field, const char* a_value);
        const std::string& GetPGChildRelationshipInsertCmd ();
        const std::string& GetPGChildRelationshipDeleteCmd ();

    public: // Methods
        OperationRequest ();
        virtual ~OperationRequest ();

        ErrorObject&       AddError(int a_sqlerrcode, unsigned int a_status);
        void               SerializeErrors(StringInfoData& a_response);

        static void        AddQuotedStringToBuffer(std::string& a_buffer, const char* a_value, bool a_quote_value);

        void               SetRequestType(int a_index, OperationType a_type);
        bool               SetRequest(const JsonapiJson::Value* a_data, std::string a_path = std::string());
        const std::string& GetInsertCmd();
        const std::string& GetUpdateCmd();
        const std::string& GetDeleteCmd();
        const std::string& GetUpdateRelationshipCmd();

        void               InitObservedStat();
        bool               ProcessOperationResult();
        bool               SerializeObservedInMeta(StringInfoData& a_response);
        bool               SerializeMeta (StringInfoData& a_response, bool a_write_empty);

        /*
         * Attribute accessors
         */
        bool                  HasError()        const;
        const SizeVector&     GetErrorIndex ()  const;

        OperationType         GetType()         const;
        const std::string&    GetResourceType() const;
        const std::string&    GetResourceId()   const;
        const std::string&    GetRelated()      const;
        const std::string&    GetAttribute()    const;
        bool                  IsIndividual()    const;
        bool                  IsCollection()    const;
        bool                  IsRelationship()  const;
        bool                  HasRelated()      const;

    };

    typedef std::vector<OperationRequest> OperationRequestVector;


    inline void OperationRequest::SetRequestType (int a_index, OperationType a_type)
    {
        rq_index_         = a_index;
        rq_type_          = a_type;
        return;
    }

    inline bool OperationRequest::HasError () const
    {
        return error_index_.size();
    }

    inline const SizeVector& OperationRequest::GetErrorIndex () const
    {
        return error_index_;
    }

    inline OperationType OperationRequest::GetType () const
    {
        return rq_type_;
    }

    inline const std::string& OperationRequest::GetResourceType () const
    {
        return rq_resource_type_;
    }

    inline const std::string& OperationRequest::GetResourceId () const
    {
        return rq_resource_id_;
    }

    inline bool OperationRequest::IsIndividual () const
    {
        return rq_resource_id_.length();
    }

    inline bool OperationRequest::IsCollection () const
    {
        return ! IsIndividual();
    }

    inline bool OperationRequest::IsRelationship () const
    {
        return rq_relationship_;
    }

    inline bool OperationRequest::HasRelated () const
    {
        return rq_related_.length();
    }

    inline const std::string& OperationRequest::GetRelated () const
    {
        return rq_related_;
    }

    inline const std::string& OperationRequest::GetAttribute () const
    {
        return rq_attribute_;
    }

} // namespace pg_jsonapi


#endif // CLD_PG_JSONAPI_OPERATION_REQUEST_H

