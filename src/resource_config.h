/**
 * @file resource_config.h declaration of ResourceConfig model node
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
#ifndef CLD_PG_JSONAPI_RESOURCE_CONFIG_H
#define CLD_PG_JSONAPI_RESOURCE_CONFIG_H

extern "C" {
#include "postgres.h"
#include "catalog/namespace.h"

#include "utils/lsyscache.h"
} // extern "C"

#include <stdlib.h>
#include <string>
#include <set>
#include <map>
#include <list>
#include <vector>

#include "json/json.h"

namespace pg_jsonapi
{
    typedef std::set<std::string>              StringSet;
    typedef StringSet::iterator                StringSetIterator;
    typedef std::vector<std::string>           StringVector;
    typedef std::map<std::string,std::string>  StringMap;
    typedef StringMap::iterator                StringMapIterator;
    typedef std::map<std::string,StringSet>    StringSetMap;
    typedef StringSetMap::iterator             StringSetMapIterator;
    typedef std::map<std::string,StringVector> StringVectorMap;
    typedef std::vector< std::pair<std::string,std::string> > StringPairVector;

    typedef struct {
        const char* name;
        bool*       ptr;
    } BoolOption;

    typedef struct {
        const char* name;
        uint*       ptr;
    } UIntOption;

    typedef struct {
        const char*  name;
        std::string* ptr;
    } StringOption;

    class DocumentConfig;

    /**
     * @brief Configuration of a JSONAPI resource,
     *        containing its attributes and relationships
     *        and how they are represented on postgresql database.
     */
    class ResourceConfig
    {
    public:
        typedef enum {
            EIsNotField,
            EIsAttribute,
            EIsToOne,
            EIsToMany
        } FieldType;

        typedef std::map<const std::string, std::string> PGColumnsSpecMap;

    private:

        typedef struct {
            std::string      schema_;
            std::string      table_; // TABLE or VIEW
            std::string      function_;
            std::string      attributes_function_;
            bool             returns_json_;
            std::string      order_by_;
            bool             use_rq_accounting_schema_; // redundant schema argument
            bool             use_rq_sharded_schema_;    // redundant schema argument
            bool             use_rq_company_schema_;    // redundant schema argument
            bool             use_rq_accounting_prefix_;
            std::string      function_arg_rq_accounting_schema_;
            std::string      function_arg_rq_sharded_schema_;
            std::string      function_arg_rq_company_schema_;
            std::string      function_arg_rq_accounting_prefix_;
            std::string      function_arg_rq_user_;
            std::string      function_arg_rq_company_;
            std::string      function_arg_rq_col_id_;
            std::string      function_arg_rq_page_offset_;
            std::string      function_arg_rq_page_limit_;
            std::string      function_arg_rq_count_;
            std::string      function_arg_rq_count_column_;
            std::string      function_arg_rq_filter_;
            std::string      function_arg_rq_order_;
            bool             id_from_rowset_;
            bool             needs_search_path_;
            uint             page_size_;
            uint             page_limit_;
            bool             show_links_;
            bool             show_null_;
            std::string      col_id_;
            std::string      company_column_;
            std::string      condition_;
            std::string      job_tube_;
            StringSet        job_methods_;
            uint             job_ttr_;        // use zero as undefined
            uint             job_validity_;   // use zero as undefined
            std::string      select_columns_;
            PGColumnsSpecMap columns_; // columns for fields on same table
            PGColumnsSpecMap casted_columns_; // columns for fields on same table applying specified cast
        } PGResourceSpec;

        typedef struct {
            std::string      schema_;
            bool             use_rq_accounting_schema_; // redundant schema argument
            bool             use_rq_sharded_schema_;    // redundant schema argument
            bool             use_rq_company_schema_;    // redundant schema argument
            std::string      table_; // TABLE or VIEW
            // JOANA - TODO: support function on relationships
            // std::string      function_; // function with arguments sent on filter
            std::string      order_by_;
            bool             use_rq_accounting_prefix_;
            bool             show_links_;
            bool             show_null_;
            std::string      col_parent_id_;
            std::string      col_child_id_;
            std::string      condition_;
            std::string      select_columns_;
        } PGRelationSpec;

        typedef struct {
            FieldType   field_type_;
            std::string resource_type_;
        } Relationship;


    public:
        typedef std::map<const std::string, PGRelationSpec>    PGRelationSpecMap;

        typedef std::map<std::string, Relationship> RelationshipMap;

    private: // Attributes

        const DocumentConfig* parent_doc_;

        std::string       type_;
        StringSet         attributes_;
        RelationshipMap   relationships_;
        StringMap         observed_;

        PGResourceSpec    q_main_;        // main resource spec
        PGRelationSpecMap q_relations_;   // relationships on distinct table

    private: // Methods

        bool    IsNewFieldNameValid  (const std::string& a_field) const;

        bool    SetAttribute         (const JsonapiJson::Value& a_attr_config);
        bool    SetRelationship      (FieldType a_type, const JsonapiJson::Value& a_relation_config, unsigned int index);
        bool    SetObserved          (const JsonapiJson::Value& a_observed_config);

        static Oid GetRelid(std::string a_type, std::string a_relnamespace, std::string a_relname);

    public: // Methods
        ResourceConfig (const DocumentConfig* a_parent_doc, std::string a_type);
        virtual ~ResourceConfig ();

        bool SetValues  (const JsonapiJson::Value& a_res_config);
        bool ValidatePG (bool a_specific_request);

        Oid                      GetOid                           () const;
        const std::string&       GetType                          () const;
        const std::string&       GetPGQuerySchema                 () const;
        void                     AddPGQueryItem                   (std::string& a_buffer) const;
        void                     AddPGQueryFromItem               (std::string& a_buffer) const;
        const std::string&       GetPGQueryFunction               () const;
        const std::string&       GetPGQueryAttributesFunction     () const;
        const std::string&       GetPGQueryColId                  () const;
        const std::string&       GetPGQueryColumns                () const;
        const std::string&       GetPGQueryColumn                 (const std::string& a_field) const;
        const std::string&       GetPGQueryCastedColumn           (const std::string& a_field) const;
        const std::string&       GetPGQueryCompanyColumn          () const;
        const std::string&       GetPGQueryCondition              () const;
        const std::string&       GetPGQueryOrder                  () const;
        bool                     IdFromRowset                     () const;
        uint                     PageSize                         () const;
        uint                     PageLimit                        () const;
        bool                     ShowLinks                        () const;
        bool                     ShowNull                         () const;
        bool                     IsQueryFromFunction              () const;
        bool                     IsQueryFromAttributesFunction    () const;
        bool                     FunctionReturnsJson              () const;
        const std::string&       GetPGFunctionArgAccountingSchema () const;
        const std::string&       GetPGFunctionArgShardedSchema    () const;
        const std::string&       GetPGFunctionArgCompanySchema    () const;
        const std::string&       GetPGFunctionArgAccountingPrefix () const;
        const std::string&       GetPGFunctionArgUser             () const;
        const std::string&       GetPGFunctionArgCompany          () const;
        const std::string&       GetPGFunctionArgColId            () const;
        const std::string&       GetPGFunctionArgPageOffset       () const;
        const std::string&       GetPGFunctionArgPageLimit        () const;
        bool                     FunctionSupportsPagination       () const;
        const std::string&       GetPGFunctionArgCount            () const;
        bool                     FunctionSupportsCounts           () const;
        bool                     FunctionSupportsCountColumn      () const;
        const std::string&       GetPGFunctionCountColumn         () const;
        const std::string&       GetPGFunctionArgOrder            () const;
        bool                     FunctionSupportsOrder            () const;
        const std::string&       GetPGFunctionArgFilter           () const;
        bool                     FunctionSupportsFilter           () const;

        bool                     HasJobTube                       (const std::string& a_method) const;
        const std::string&       GetJobTube                       () const;
        size_t                   JobTtr                           () const;
        size_t                   JobValidity                      () const;

        const PGRelationSpecMap& GetPGRelations                () const;
        const std::string&       GetPGRelationQuerySchema      (const std::string& a_field) const;
        void                     AddPGRelationQueryTable       (std::string& a_buffer, const std::string& a_field) const;
        void                     AddPGRelationQueryFromItem    (std::string& a_buffer, const std::string& a_field) const;
        const std::string&       GetPGRelationQueryColParentId (const std::string& a_field) const;
        const std::string&       GetPGRelationQueryColChildId  (const std::string& a_field) const;
        const std::string&       GetPGRelationQueryColumns     (const std::string& a_field) const;
        const std::string&       GetPGRelationQueryCondition   (const std::string& a_field) const;
        const std::string&       GetPGRelationQueryOrder       (const std::string& a_field) const;
        bool                     ShowLinks                     (const std::string& a_field) const;
        bool                     ShowNull                      (const std::string& a_field) const;
        const RelationshipMap&   GetRelationships              () const;
        const StringMap&         GetObserved                   () const;

        bool               IsIdentifier         (const std::string& a_name)  const;
        bool               IsAttribute          (const std::string& a_field) const;
        bool               IsValidAttribute     (const std::string& a_field) const;
        bool               IsRelationship       (const std::string& a_field) const;
        bool               IsField              (const std::string& a_field) const;
        bool               IsToOneRelationship  (const std::string& a_field) const;
        bool               IsToManyRelationship (const std::string& a_field) const;
        bool               IsPGChildRelation    (const std::string& a_field) const;
        bool               IsObserved           (const std::string& a_field) const;
        const std::string& GetObservedMetaName  (const std::string& a_field) const;
        const std::string& GetFieldResourceType (const std::string& a_field) const;

    };

    inline const std::string& ResourceConfig::GetType () const
    {
        return type_;
    }

    inline const std::string& ResourceConfig::GetPGQueryFunction () const
    {
        return q_main_.function_;
    }

    inline const std::string& ResourceConfig::GetPGQueryAttributesFunction () const
    {
        return q_main_.attributes_function_;
    }

    inline const std::string& ResourceConfig::GetPGQueryColId () const
    {
        return q_main_.col_id_;
    }

    inline const std::string& ResourceConfig::GetPGQueryColumns () const
    {
        return q_main_.select_columns_;
    }

    inline const std::string& ResourceConfig::GetPGQueryColumn (const std::string& a_field) const
    {
        if ( q_main_.columns_.count(a_field) ) {
            return q_main_.columns_.at(a_field);
        } else {
            return a_field;
        }
    }

    inline const std::string& ResourceConfig::GetPGQueryCastedColumn (const std::string& a_field) const
    {
        if ( q_main_.casted_columns_.count(a_field) ) {
            return q_main_.casted_columns_.at(a_field);
        } else {
            return GetPGQueryColumn(a_field);
        }
    }

    inline const std::string& ResourceConfig::GetPGQueryCompanyColumn () const
    {
        return q_main_.company_column_;
    }

    inline const std::string& ResourceConfig::GetPGQueryCondition () const
    {
        return q_main_.condition_;
    }

    inline const std::string& ResourceConfig::GetPGQueryOrder () const
    {
        return q_main_.order_by_;
    }

    inline bool ResourceConfig::IdFromRowset () const
    {
        return q_main_.id_from_rowset_;
    }

    inline uint ResourceConfig::PageSize () const
    {
        return q_main_.page_size_;
    }

    inline uint ResourceConfig::PageLimit () const
    {
        return q_main_.page_limit_;
    }

    inline bool ResourceConfig::ShowLinks () const
    {
        return q_main_.show_links_;
    }

    inline bool ResourceConfig::ShowNull () const
    {
        return q_main_.show_null_;
    }

    inline bool ResourceConfig::IsQueryFromFunction () const
    {
        return ( ! q_main_.function_.empty() );
    }

    inline bool ResourceConfig::IsQueryFromAttributesFunction () const
    {
        return ( ! q_main_.attributes_function_.empty() );
    }

    inline bool ResourceConfig::FunctionReturnsJson () const
    {
        return ( IsQueryFromFunction() && q_main_.returns_json_ );
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgAccountingSchema () const
    {
        return q_main_.function_arg_rq_accounting_schema_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgShardedSchema () const
    {
        return q_main_.function_arg_rq_sharded_schema_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgCompanySchema () const
    {
        return q_main_.function_arg_rq_company_schema_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgAccountingPrefix () const
    {
        return q_main_.function_arg_rq_accounting_prefix_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgUser () const
    {
        return q_main_.function_arg_rq_user_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgCompany () const
    {
        return q_main_.function_arg_rq_company_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgColId () const
    {
        return q_main_.function_arg_rq_col_id_.length()? q_main_.function_arg_rq_col_id_ : GetPGQueryColId() ;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgPageOffset () const
    {
        return q_main_.function_arg_rq_page_offset_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgPageLimit () const
    {
        return q_main_.function_arg_rq_page_limit_;
    }

    inline bool ResourceConfig::FunctionSupportsPagination () const
    {
        return ( q_main_.function_arg_rq_page_offset_.length() > 0 && q_main_.function_arg_rq_page_limit_.length() > 0 );
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgCount () const
    {
        return q_main_.function_arg_rq_count_;
    }

    inline bool ResourceConfig::FunctionSupportsCounts () const
    {
        return ( q_main_.function_arg_rq_count_.length() > 0 );
    }

    inline bool ResourceConfig::FunctionSupportsCountColumn () const
    {
        return ( q_main_.function_arg_rq_count_column_.length() > 0 );
    }

    inline const std::string& ResourceConfig::GetPGFunctionCountColumn () const
    {
        return q_main_.function_arg_rq_count_column_;
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgOrder () const
    {
        return q_main_.function_arg_rq_order_;
    }

    inline bool ResourceConfig::FunctionSupportsOrder () const
    {
        return ( q_main_.function_arg_rq_order_.length() > 0 );
    }

    inline const std::string& ResourceConfig::GetPGFunctionArgFilter () const
    {
        return q_main_.function_arg_rq_filter_;
    }

    inline bool ResourceConfig::FunctionSupportsFilter () const
    {
        return ( q_main_.function_arg_rq_filter_.length() > 0 );
    }

    inline const ResourceConfig::PGRelationSpecMap& ResourceConfig::GetPGRelations () const
    {
        return q_relations_;
    }

    inline const std::string& ResourceConfig::GetPGRelationQueryColParentId (const std::string& a_field) const
    {
        return q_relations_.at(a_field).col_parent_id_;
    }

    inline const std::string& ResourceConfig::GetPGRelationQueryColChildId (const std::string& a_field) const
    {
        return q_relations_.at(a_field).col_child_id_;
    }

    inline const std::string& ResourceConfig::GetPGRelationQueryColumns (const std::string& a_field) const
    {
        return q_relations_.at(a_field).select_columns_;
    }

    inline const std::string& ResourceConfig::GetPGRelationQueryCondition (const std::string& a_field) const
    {
        return q_relations_.at(a_field).condition_;
    }

    inline const std::string& ResourceConfig::GetPGRelationQueryOrder (const std::string& a_field) const
    {
        return q_relations_.at(a_field).order_by_;
    }

    inline bool ResourceConfig::ShowLinks (const std::string& a_field) const
    {
        return ( q_relations_.empty() || 0 == q_relations_.count(a_field) ) ? q_main_.show_links_ : q_relations_.at(a_field).show_links_;
    }

    inline bool ResourceConfig::ShowNull (const std::string& a_field) const
    {
        return ( q_relations_.empty() || 0 == q_relations_.count(a_field) ) ? q_main_.show_null_ : q_relations_.at(a_field).show_null_;
    }

    inline bool ResourceConfig::IsNewFieldNameValid(const std::string& a_field) const
    {
        if ( IsField(a_field) || IsIdentifier(a_field) ) {
            return false;
        }
        return true;
    }

    inline bool ResourceConfig::IsAttribute (const std::string& a_field) const
    {
        return attributes_.count(a_field);
    }

    inline bool ResourceConfig::IsRelationship (const std::string& a_field) const
    {
        return relationships_.count(a_field);
    }

    inline bool ResourceConfig::IsField (const std::string& a_field) const
    {
        return ( IsRelationship(a_field) || IsAttribute(a_field) );
    }

    inline bool ResourceConfig::IsToOneRelationship (const std::string& a_field) const
    {
        return ( IsRelationship(a_field) && EIsToOne == relationships_.at(a_field).field_type_ );
    }

    inline bool ResourceConfig::IsToManyRelationship (const std::string& a_field) const
    {
        return ( IsRelationship(a_field) && EIsToMany == relationships_.at(a_field).field_type_ );
    }

    inline bool ResourceConfig::IsObserved (const std::string& a_field) const
    {
        return observed_.count(a_field);
    }

    inline const std::string& ResourceConfig::GetObservedMetaName (const std::string& a_field) const
    {
        if ( IsObserved(a_field) ) {
            return observed_.at(a_field);
        }
        /* this should never happen... */
        return a_field;
    }

    inline bool ResourceConfig::IsPGChildRelation (const std::string& a_field) const
    {
        return q_relations_.count(a_field);
    }

    inline const std::string& ResourceConfig::GetFieldResourceType (const std::string& a_field) const
    {
        if ( IsRelationship(a_field) ) {
            return relationships_.at(a_field).resource_type_;
        }
        /* this should never happen... */
        return a_field;
    }

    inline const ResourceConfig::RelationshipMap& ResourceConfig::GetRelationships () const
    {
        return relationships_;
    }

    inline const StringMap& ResourceConfig::GetObserved() const
    {
        return observed_;
    }

    inline bool ResourceConfig::HasJobTube (const std::string& a_method) const
    {
        if ( q_main_.job_tube_.empty() ) {
            return false;
        } else if ( q_main_.job_methods_.empty() ) {
            return true;
        } else {
            return q_main_.job_methods_.count(a_method);
        }
    }

    inline const std::string& ResourceConfig::GetJobTube () const
    {
        return q_main_.job_tube_;
    }


    inline size_t ResourceConfig::JobTtr () const
    {
        return q_main_.job_ttr_;
    }

    inline size_t ResourceConfig::JobValidity () const
    {
        return q_main_.job_validity_;
    }

} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_RESOURCE_CONFIG_H
