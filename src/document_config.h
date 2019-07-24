/**
 * @file document_config.h Declaration of DocumentConfig model node.
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
#ifndef CLD_PG_JSONAPI_DOCUMENT_CONFIG_H
#define CLD_PG_JSONAPI_DOCUMENT_CONFIG_H

#include <stdlib.h>
#include <string>
#include <map>

#include "resource_config.h"

namespace pg_jsonapi
{
    /**
     * @brief Configuration of a JSONAPI document associated to a base URL,
     *        containing general options, resources and their configuration.
     */
    class DocumentConfig
    {
    private:

        typedef std::map<std::string, ResourceConfig> ResourceConfigMap;
        typedef ResourceConfigMap::iterator ResourceConfigMapIterator;

    private: // Attributes
        bool        is_valid_;
        std::string base_url_;
        std::string query_config_;
        std::string query_default_config_;
        bool        version_;
        bool        compound_;
        uint        page_size_;
        bool        show_links_;
        bool        show_null_;
        bool        restrict_type_;
        bool        restrict_attr_;
        bool        empty_is_null_;
        std::string default_order_by_;
        bool        use_request_accounting_schema_;
        bool        use_request_sharded_schema_;
        bool        use_request_company_schema_;
        bool        use_request_accounting_prefix_;
        std::string template_search_path_;
        std::map<std::string, pg_jsonapi::ResourceConfig> resources_;

    private: // Methods
        bool Validate();
        ResourceConfig*       Resource (const std::string& a_type);

    public: // Methods
        DocumentConfig (const std::string a_base_url);
        virtual ~DocumentConfig ();

        bool LoadConfigFromDB(bool& o_config_exists);

        static bool DefaultHasVersion ();
        static bool DefaultIsCompound ();
        static int  DefaultPageSize   ();
        static bool DefaultShowLinks  ();
        static bool DefaultShowNull   ();
        static bool DefaultTypeRestriction();
        static bool DefaultAttrRestriction();
        static bool DefaultEmptyIsNull();
        static bool DefaultRequestAccountingSchema();
        static bool DefaultRequestShardedSchema();
        static bool DefaultRequestCompanySchema();
        static bool DefaultRequestAccountingPrefix();

        bool HasVersion                 () const;
        bool IsCompound                 () const;
        int  PageSize                   () const;
        bool ShowLinks                  () const;
        bool ShowNull                   () const;
        bool HasTypeRestriction         () const;
        bool HasAttrRestriction         () const;
        bool EmptyIsNull                () const;
        bool UseRequestAccountingSchema           () const;
        bool UseRequestShardedSchema    () const;
        bool UseRequestCompanySchema    () const;
        bool UseRequestAccountingPrefix () const;
        const std::string& DefaultOrder () const;
        const std::string& SearchPathTemplate () const;

        bool ValidateRequest    (const std::string& a_type, const std::string& a_related);
        bool IsIdentifier       (const std::string& a_name) const;
        bool IsValidField       (const std::string& a_type, const std::string& a_field) const;

        const ResourceConfig& GetResource (const std::string& a_type) const;

        const std::string&    ConfigQuery ();
    };

    typedef std::map<std::string, DocumentConfig> DocumentConfigMap;


    inline bool DocumentConfig::DefaultHasVersion ()
    {
        return true;
    }

    inline bool DocumentConfig::DefaultIsCompound ()
    {
        return true;
    }

    inline int DocumentConfig::DefaultPageSize ()
    {
        return 1000;
    }

    inline bool DocumentConfig::DefaultShowLinks ()
    {
        return true;
    }

    inline bool DocumentConfig::DefaultShowNull ()
    {
        return true;
    }

    inline bool DocumentConfig::DefaultTypeRestriction ()
    {
        return false;
    }

    inline bool DocumentConfig::DefaultAttrRestriction ()
    {
        return false;
    }

    inline bool DocumentConfig::DefaultEmptyIsNull ()
    {
        return false;
    }

    inline bool DocumentConfig::DefaultRequestAccountingSchema ()
    {
        return false;
    }

    inline bool DocumentConfig::DefaultRequestShardedSchema ()
    {
        return false;
    }

    inline bool DocumentConfig::DefaultRequestCompanySchema ()
    {
        return false;
    }

    inline bool DocumentConfig::DefaultRequestAccountingPrefix ()
    {
        return false;
    }

    inline bool DocumentConfig::HasVersion () const
    {
        return version_;
    }

    inline bool DocumentConfig::IsCompound () const
    {
        return compound_;
    }

    inline int DocumentConfig::PageSize () const
    {
        return page_size_;
    }

    inline bool DocumentConfig::ShowLinks () const
    {
        return show_links_;
    }

    inline bool DocumentConfig::ShowNull () const
    {
        return show_null_;
    }

    inline bool DocumentConfig::HasTypeRestriction () const
    {
        return restrict_type_;
    }

    inline bool DocumentConfig::HasAttrRestriction () const
    {
        return restrict_attr_;
    }

    inline bool DocumentConfig::EmptyIsNull () const
    {
        return empty_is_null_;
    }

    inline bool DocumentConfig::UseRequestAccountingSchema () const
    {
        return use_request_accounting_schema_;
    }

    inline bool DocumentConfig::UseRequestShardedSchema () const
    {
        return use_request_sharded_schema_;
    }

    inline bool DocumentConfig::UseRequestCompanySchema () const
    {
        return use_request_company_schema_;
    }

    inline bool DocumentConfig::UseRequestAccountingPrefix () const
    {
        return use_request_accounting_prefix_;
    }

    inline const std::string& DocumentConfig::DefaultOrder () const
    {
        return default_order_by_;
    }

    inline const std::string& DocumentConfig::SearchPathTemplate () const
    {
        return template_search_path_;
    }

    inline bool DocumentConfig::IsIdentifier(const std::string& a_name) const
    {
        if ( 0 == strcmp(a_name.c_str(), "id") || 0 == strcmp(a_name.c_str(), "type") ) {
            return true;
        }
        return false;
    }

    inline bool DocumentConfig::IsValidField (const std::string& a_type, const std::string& a_field) const
    {
        if ( 0 == resources_.count(a_type) ) {
            return false;
        } else {
            return ( resources_.at(a_type).IsField(a_field) || resources_.at(a_type).IsValidAttribute(a_field) );
        }
    }

    inline const ResourceConfig& DocumentConfig::GetResource (const std::string& a_type) const
    {
        return resources_.at(a_type);
    }

    inline const std::string& DocumentConfig::ConfigQuery ()
    {
        if ( 0 == query_config_.size() ) {
            query_config_ = "SELECT config FROM public.jsonapi_config WHERE prefix = '" + base_url_ + "'";
        }
        return query_config_;
    }

    inline pg_jsonapi::ResourceConfig* pg_jsonapi::DocumentConfig::Resource (const std::string& a_type)
    {
        std::map<std::string, pg_jsonapi::ResourceConfig>::iterator it = resources_.find(a_type);
        if ( resources_.end() == it ) {
            /* if resource does not exist, create it with default values */
            resources_.insert( std::pair<std::string,pg_jsonapi::ResourceConfig>(a_type, ResourceConfig(this, a_type)) );
            it = resources_.find(a_type);
        }
        return &(it->second);
    }

} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_DOCUMENT_CONFIG_H
