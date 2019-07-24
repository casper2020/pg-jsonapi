/**
 * @file query_builder.h declaration of QueryBuilder model node
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
#pragma once
#ifndef CLD_PG_JSONAPI_QUERY_BUILDER_H
#define CLD_PG_JSONAPI_QUERY_BUILDER_H

#include <stdlib.h>
#include <string>
#include "document_config.h"
#include "operation_request.h"
#include "resource_data.h"
#include "utils_adt_json.h"

namespace pg_jsonapi
{
    typedef enum {
        E_EXT_NONE,
        E_EXT_BULK,
        E_EXT_JSON_PATCH
    } Extension;

    // related with HttpStatusErrorCode
    typedef enum {
        E_HTTP_OK                     = 200,
        E_HTTP_CREATED                = 201,
        E_HTTP_ACCEPTED               = 202,
        E_HTTP_NO_CONTENT             = 204,

        E_HTTP_ERROR_BAD_REQUEST            = E_HTTP_BAD_REQUEST,
        E_HTTP_ERROR_FORBIDDEN              = E_HTTP_FORBIDDEN,
        E_HTTP_ERROR_NOT_FOUND              = E_HTTP_NOT_FOUND,
        E_HTTP_ERROR_CONFLICT               = E_HTTP_CONFLICT,
        E_HTTP_ERROR_INTERNAL_SERVER_ERROR  = E_HTTP_INTERNAL_SERVER_ERROR,
    } HttpStatusCode;

    /**
     * @brief Node used to interact with postgres, dealing with SPI interface,
     *        executing queries and keeping results in optimized way.
     */
    class QueryBuilder
    {
    private: // ErrorCode will be created only once
        ErrorCode       errcodes_;

    private: // Resource Specification - initialized only once by base_url
        DocumentConfigMap   config_map_;
        DocumentConfig*     config_;      // specification for current request
        StringSet           requested_urls_;

    private: // Attributes - request variables filled while parsing request

        std::string         rq_method_;
        Extension           rq_extension_;
        std::string         rq_url_encoded_;
        std::string         rq_base_url_;
        JsonapiJson::Value  rq_body_root_;
        std::string         rq_sharded_schema_;
        std::string         rq_company_schema_;
        std::string         rq_accounting_schema_;
        std::string         rq_accounting_prefix_;
        std::string         rq_user_id_;
        std::string         rq_company_id_;
        std::string         rq_resource_type_;
        std::string         rq_resource_id_;
        std::string         rq_related_;
        bool                rq_relationship_;
        StringSet           rq_include_param_;
        StringPairVector    rq_sort_param_;
        StringSetMap        rq_fields_param_;
        StringMap           rq_filter_field_param_;
        std::string         rq_filter_param_;
        ssize_t             rq_page_size_param_;
        ssize_t             rq_page_number_param_;
        short               rq_links_param_;
        short               rq_totals_param_;
        short               rq_null_param_;

        OperationRequestVector rq_operations_;

    private: // Attributes - used to query postgres and keep results

        bool            spi_connected_;
        bool            spi_read_only_;
        std::string     q_buffer_;
        size_t          q_required_count_;
        ResourceDataMap q_data_;
        StringSetMap    q_to_be_included_;
        bool            q_top_must_be_included_;
        size_t          q_top_total_rows_;
        size_t          q_top_grand_total_rows_;
        size_t          q_page_size_;
        size_t          q_page_number_;
        ErrorVector     q_errors_;
        HttpStatusCode  q_http_status_;
        const char*     q_json_function_data_;
        const char*     q_json_function_included_;
        bool            q_needs_search_path_;
        std::string     q_old_search_path_;

    private: // Methods

        bool               ParseUrl                    ();
        bool               ParseRequestBody            (const char* a_body, size_t a_body_len);

        void               AddInClause                 (const std::string& a_column, StringSet a_values);
        const std::string& GetTopQuery                 (bool a_count_rows = false, bool a_apply_filters = true);
        const std::string& GetRelationshipQuery        (const std::string& a_type, const std::string& a_rel, const StringSet& a_parent_ids);
        const std::string& GetInclusionQuery           (const std::string& a_type);
        const std::string& GetFilterTableByFieldCondition (const std::string& a_type, const std::string& a_field, const std::string& a_value);

        bool               ProcessCounter              (size_t& a_count);
        bool               ProcessFunctionJsonResult   (const std::string& a_type);
        bool               ProcessQueryResult          (const std::string& a_type, size_t a_depth);
        bool               ProcessAttributes           (const std::string& a_type, size_t a_depth, StringSet* a_processed_ids);
        bool               ProcessRelationships        (const std::string& a_type, size_t a_depth);
        void               RequestResourceInclusion    (const std::string& a_type, size_t a_depth, const std::string& a_id, const std::string& a_field, const std::string& a_rel_id);
        void               CleanRelationshipInclusion  ();

        bool               IsRequestedField            (const std::string& a_type, const std::string& a_field) const;

        void               SerializeRelationshipData   (StringInfoData& a_response, const std::string& a_type, const std::string& a_field, const ResourceData& a_rd, uint32 a_row) const;
        void               SerializeResource           (StringInfoData& a_response, const std::string& a_type, ResourceData& a_rd, uint32 a_row) const;
        void               SerializeFetchData          (StringInfoData& a_response);
        void               SerializeIncluded           (StringInfoData& a_response);
        void               SerializeErrors             (StringInfoData& a_response);

    public: // Methods

        QueryBuilder ();
        virtual ~QueryBuilder ();

        void         Clear ();
        ErrorObject& AddError(int a_sqlerrcode, HttpStatusErrorCode a_status, bool a_operation = false);
        void         SerializeCommonErrorItems    (StringInfoData& a_response);


        bool         SPIConnect                   ();
        bool         SPIDisconnect                ();
        bool         SPIExecuteCommand            (const std::string& a_command, const int a_expected_ret);

        bool         ParseRequestArguments        (const char* a_method, size_t a_method_len,
                                                   const char* a_url, size_t a_url_len,
                                                   const char* a_body, size_t a_body_len,
                                                   const char* a_user_id, size_t a_user_id_len,
                                                   const char* a_company_id, size_t a_company_id_len,
                                                   const char* a_company_schema, size_t a_company_schema_len,
                                                   const char* a_sharded_schema, size_t a_sharded_schema_len,
                                                   const char* a_accounting_schema, size_t a_accounting_schema_len,
                                                   const char* a_accounting_prefix, size_t a_accounting_prefix_len);

        void         RequireSearchPath();
        bool         ValidateRequest              ();
        bool         FetchData                    ();
        bool         ExecuteOperations            ();
        void         RequestOperationResponseData (const std::string& a_type, const std::string& a_id);
        void         SerializeResponse            (StringInfoData& a_response);

        static bool  IsValidHttpMethod            (const std::string& a_method);

        /*
         * Attribute accessors
         */
        bool                  SPIIsConnected()              const;
        bool                  HasErrors()                   const;
        size_t                ErrorsSize()                  const;
        const ErrorObject&    GetError(size_t a_index)      const;
        HttpStatusCode        GetHttpStatus ()              const;
        const DocumentConfig* GetDocumentConfig()           const;
        bool                  NeedsSearchPath()             const;

        const std::string&    GetRequestUrl()                  const;
        const std::string&    GetRequestBaseUrl()              const;
        const std::string&    GetRequestMethod()               const;
        const std::string&    GetRequestAccountingSchema()     const;
        const std::string&    GetRequestShardedSchema()        const;
        const std::string&    GetRequestCompanySchema()        const;
        const std::string&    GetRequestAccountingPrefix()     const;
        const std::string&    GetRequestUser()                 const;
        const std::string&    GetRequestCompany()              const;
        const std::string&    GetResourceType()                const;
        const std::string&    GetResourceId()                  const;
        const std::string&    GetRelated()                     const;
        const std::string&    GetRelatedType()                 const;
        bool                  IsIndividual()                   const;
        bool                  IsCollection()                   const;
        bool                  IsRelationship()                 const;
        bool                  HasRelated()                     const;
        bool                  IsTopQueryFromFunction()         const;
        bool                  TopFunctionReturnsJson()         const;
        bool                  TopFunctionSupportsCounts()      const;
        bool                  TopFunctionSupportsFilter()      const;
        const std::string&    GetFunctionArgAccountingSchema() const;
        const std::string&    GetFunctionArgShardedSchema()    const;
        const std::string&    GetFunctionArgCompanySchema()    const;
        const std::string&    GetFunctionArgAccountingPrefix() const;
        const std::string&    GetFunctionArgUser()             const;
        const std::string&    GetFunctionArgCompany()          const;
        bool                  IsTopQueryFromJobTube()          const;
    };

    inline bool QueryBuilder::IsRequestedField (const std::string& a_type, const std::string& a_field) const
    {
        return ( 0 == rq_fields_param_.count(a_type) || rq_fields_param_.at(a_type).count(a_field) );
    }

    inline void QueryBuilder::RequireSearchPath ()
    {
        q_needs_search_path_ = true;
        return;
    }

    inline bool QueryBuilder::SPIIsConnected () const
    {
        return spi_connected_;
    }
    inline bool QueryBuilder::HasErrors () const
    {
        return ( q_errors_.size() );
    }

    inline size_t QueryBuilder::ErrorsSize () const
    {
        return ( q_errors_.size() );
    }

    inline const ErrorObject& QueryBuilder::GetError (size_t a_index) const
    {
        return q_errors_[a_index];
    }

    inline HttpStatusCode QueryBuilder::GetHttpStatus () const
    {
        return q_http_status_;
    }

    inline bool QueryBuilder::NeedsSearchPath () const
    {
        return q_needs_search_path_;
    }

    inline const pg_jsonapi::DocumentConfig* QueryBuilder::GetDocumentConfig () const
    {
        return config_;
    }

    inline const std::string& QueryBuilder::GetRequestUrl () const
    {
        return rq_url_encoded_;
    }

    inline const std::string& QueryBuilder::GetRequestBaseUrl () const
    {
        return rq_base_url_;
    }

    inline const std::string& QueryBuilder::GetRequestMethod () const
    {
        return rq_method_;
    }

    inline const std::string& QueryBuilder::GetRequestAccountingSchema () const
    {
        return rq_accounting_schema_;
    }

    inline const std::string& QueryBuilder::GetRequestShardedSchema () const
    {
        return rq_sharded_schema_;
    }

    inline const std::string& QueryBuilder::GetRequestCompanySchema () const
    {
        return rq_company_schema_;
    }

    inline const std::string& QueryBuilder::GetRequestAccountingPrefix () const
    {
        return rq_accounting_prefix_;
    }

    inline const std::string& QueryBuilder::GetRequestUser () const
    {
        return rq_user_id_;
    }

    inline const std::string& QueryBuilder::GetRequestCompany () const
    {
        return rq_company_id_;
    }

    inline const std::string& QueryBuilder::GetResourceType () const
    {
        return rq_resource_type_;
    }

    inline const std::string& QueryBuilder::GetResourceId () const
    {
        return rq_resource_id_;
    }

    inline bool QueryBuilder::IsIndividual () const
    {
        return rq_resource_id_.length();
    }

    inline bool QueryBuilder::IsCollection () const
    {
        return ! IsIndividual();
    }

    inline bool QueryBuilder::IsRelationship () const
    {
        return rq_relationship_;
    }

    inline bool QueryBuilder::HasRelated () const
    {
        return rq_related_.length();
    }

    inline const std::string& QueryBuilder::GetRelated () const
    {
        return rq_related_;
    }

    inline const std::string& QueryBuilder::GetRelatedType () const
    {
        return config_->GetResource(rq_resource_type_).GetFieldResourceType(rq_related_);
    }

    inline bool QueryBuilder::IsTopQueryFromFunction () const
    {
        return rq_resource_type_.length() && config_->GetResource(rq_resource_type_).IsQueryFromFunction();
    }

    inline bool QueryBuilder::TopFunctionReturnsJson () const
    {
        return config_->GetResource(rq_resource_type_).FunctionReturnsJson();
    }

    inline bool QueryBuilder::TopFunctionSupportsCounts () const
    {
        return config_->GetResource(rq_resource_type_).FunctionSupportsCounts();
    }

    inline bool QueryBuilder::TopFunctionSupportsFilter () const
    {
        return config_->GetResource(rq_resource_type_).FunctionSupportsFilter();
    }

    inline const std::string& QueryBuilder::GetFunctionArgAccountingSchema () const
    {
        return config_->GetResource(rq_resource_type_).GetPGFunctionArgAccountingSchema();
    }

    inline const std::string& QueryBuilder::GetFunctionArgShardedSchema () const
    {
        return config_->GetResource(rq_resource_type_).GetPGFunctionArgShardedSchema();
    }

    inline const std::string& QueryBuilder::GetFunctionArgCompanySchema () const
    {
        return config_->GetResource(rq_resource_type_).GetPGFunctionArgCompanySchema();
    }

    inline const std::string& QueryBuilder::GetFunctionArgAccountingPrefix () const
    {
        return config_->GetResource(rq_resource_type_).GetPGFunctionArgAccountingPrefix();
    }

    inline const std::string& QueryBuilder::GetFunctionArgUser () const
    {
        return config_->GetResource(rq_resource_type_).GetPGFunctionArgUser();
    }

    inline const std::string& QueryBuilder::GetFunctionArgCompany () const
    {
        return config_->GetResource(rq_resource_type_).GetPGFunctionArgCompany();
    }

    inline bool QueryBuilder::IsTopQueryFromJobTube () const
    {
        return rq_resource_type_.length() && config_->GetResource(rq_resource_type_).HasJobTube(rq_method_);
    }

} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_QUERY_BUILDER_H
