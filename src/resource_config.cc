/**
 * @file resource_config.cc Implementation of ResourceConfig
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

#include "resource_config.h"
#include "query_builder.h"

extern pg_jsonapi::QueryBuilder* g_qb;

/**
 * @brief Create a resource configuration associated to a jsonapi document.
 */
pg_jsonapi::ResourceConfig::ResourceConfig (const DocumentConfig* a_parent_doc, std::string a_type) : parent_doc_(a_parent_doc), type_(a_type)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, type_.c_str())));

    q_main_.schema_.clear();
    q_main_.use_rq_accounting_schema_ = parent_doc_->UseRequestAccountingSchema();
    q_main_.use_rq_sharded_schema_    = parent_doc_->UseRequestShardedSchema();
    q_main_.use_rq_company_schema_    = parent_doc_->UseRequestCompanySchema();
    q_main_.use_rq_accounting_prefix_ = parent_doc_->UseRequestAccountingPrefix();
    q_main_.table_                    = type_;
    q_main_.returns_json_             = false;
    q_main_.needs_search_path_        = false;
    q_main_.col_id_                   = "id";
    q_main_.condition_.clear();
    q_main_.order_by_                 = parent_doc_->DefaultOrder();
    if ( parent_doc_->HasAttrRestriction() ) {
        q_main_.select_columns_ = "id";
    } else {
        q_main_.select_columns_ = "*";
    }
}

/**
 * @brief Destructor.
 */
pg_jsonapi::ResourceConfig::~ResourceConfig ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, type_.c_str())));

}

bool pg_jsonapi::ResourceConfig::IsIdentifier(const std::string& a_name) const
{
    return parent_doc_->IsIdentifier(a_name);
}

const std::string& pg_jsonapi::ResourceConfig::GetPGQuerySchema () const
{
    if ( q_main_.use_rq_accounting_schema_ ) {
        return g_qb->GetRequestAccountingSchema();
    } else if ( q_main_.use_rq_sharded_schema_ ) {
        return g_qb->GetRequestShardedSchema();
    } else if ( q_main_.use_rq_company_schema_ ) {
        return g_qb->GetRequestCompanySchema();
    } else {
        return q_main_.schema_;
    }
}

void pg_jsonapi::ResourceConfig::AddPGQueryItem (std::string& a_buffer) const
{
    if ( q_main_.use_rq_accounting_prefix_ ) {
        a_buffer += g_qb->GetRequestAccountingPrefix();
    }
    if ( IsQueryFromFunction() ) {
        a_buffer += q_main_.function_;
    } else {
        a_buffer += q_main_.table_;
    }

    return;
}

void pg_jsonapi::ResourceConfig::AddPGQueryFromItem (std::string& a_buffer) const
{
    if ( GetPGQuerySchema().length() ) {
        a_buffer += "\"" + GetPGQuerySchema() + "\".\"";
        AddPGQueryItem(a_buffer);
        a_buffer += "\"";
    } else {
        a_buffer += "\"";
        AddPGQueryItem(a_buffer);
        a_buffer += "\"";
    }
    return;
}

const std::string& pg_jsonapi::ResourceConfig::GetPGRelationQuerySchema (const std::string& a_field) const
{
    if ( q_relations_.at(a_field).use_rq_accounting_schema_ ) {
        return g_qb->GetRequestAccountingSchema();
    } else if ( q_relations_.at(a_field).use_rq_sharded_schema_ ) {
        return g_qb->GetRequestShardedSchema();
    } else if ( q_relations_.at(a_field).use_rq_company_schema_ ) {
        return g_qb->GetRequestCompanySchema();
    } else {
        return q_relations_.at(a_field).schema_;
    }
}

void pg_jsonapi::ResourceConfig::AddPGRelationQueryTable (std::string& a_buffer, const std::string& a_field) const
{
    if ( q_relations_.at(a_field).use_rq_accounting_prefix_ ) {
        a_buffer += g_qb->GetRequestAccountingPrefix() + q_relations_.at(a_field).table_;
    } else {
        a_buffer += q_relations_.at(a_field).table_;
    }
    return;
}

void pg_jsonapi::ResourceConfig::AddPGRelationQueryFromItem (std::string& a_buffer, const std::string& a_field) const
{
    if ( GetPGRelationQuerySchema(a_field).length() ) {
        a_buffer += "\"" + GetPGRelationQuerySchema(a_field) + "\".\"";
        AddPGRelationQueryTable(a_buffer, a_field);
        a_buffer += "\"";
    } else {
        a_buffer += "\"";
        AddPGRelationQueryTable(a_buffer, a_field);
        a_buffer += "\"";
    }
    return;
}

/**
 * @brief Validate an attribute against the resource configuration.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::ResourceConfig::IsValidAttribute (const std::string& a_field) const
{
    if ( attributes_.size() || parent_doc_->HasAttrRestriction() ) {
        return attributes_.count(a_field);
    } else {
        return !( IsIdentifier(a_field) || IsRelationship(a_field) );
    }
}

/**
 * @brief Set the configuration of one attribute from parsed json object.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::ResourceConfig::SetAttribute (const JsonapiJson::Value& a_attr_config)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, type_.c_str())));

    std::string key;
    std::string col;
    std::string cast;

    if ( a_attr_config.isString() ) {
        key = a_attr_config.asString();
    } else {
        if ( ! a_attr_config.isObject() || a_attr_config.size() > 1 ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"attributes\"]', member must be string or object",
                       type_.c_str());
            return false;
        }
        key = a_attr_config.getMemberNames()[0];
        if ( a_attr_config[key].isMember("pg-column") ) {
            col = a_attr_config[key]["pg-column"].asString();
        }
        if ( a_attr_config[key].isMember("pg-cast") ) {
            cast =  a_attr_config[key]["pg-cast"].asString();
        }
    }

    if ( ! IsNewFieldNameValid(key) ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid field '%s' on 'resources[\"%s\"][\"attributes\"]', name is reserved or duplicated",
                   key.c_str(), type_.c_str());
        return false;
    }
    attributes_.insert(key);

    q_main_.select_columns_ += ",";
    if ( cast.size() ) {
        q_main_.casted_columns_[key] = "\"" + ( col.empty() ? key : col) + "\"::" + cast;
        q_main_.select_columns_ += q_main_.casted_columns_[key] + " AS ";
    }
    if ( col.size() ) {
        q_main_.columns_[key] = "\"" + col + "\"";
        if ( cast.empty() ) {
            q_main_.select_columns_ += q_main_.columns_[key] + " AS ";
        }
    } else if ( IsQueryFromAttributesFunction() ) {
        q_main_.select_columns_ +=  "pgf.";
    }
    q_main_.select_columns_ += "\"" + key + "\"";
    return true;
}

/**
 * @brief Set the configuration of a relationship from parsed json object.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::ResourceConfig::SetRelationship(FieldType a_type, const JsonapiJson::Value& a_relation_config, unsigned int index)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, type_.c_str())));

    std::string key;
    std::string res;
    std::string col_child;
    bool relation_on_parent_table = false;

    if ( a_relation_config[index].isString() ) {
        key = a_relation_config[index].asString();
        res = key;
        col_child = key + "_id";
        relation_on_parent_table = true;
    } else if ( ! a_relation_config[index].isObject() || a_relation_config[index].size() > 1 ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"%s\"][%d]', member must be string or object", type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", index);
        return false;
    } else {
        key = a_relation_config[index].getMemberNames()[0];
        if ( ! a_relation_config[index][key].isMember("resource") ) {
            res = key;
        } else if ( ! a_relation_config[index][key]["resource"].isString() ||  a_relation_config[index][key]["resource"].empty() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"%s\"][%d('%s')][\"resource\"]'",
                       type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", index, key.c_str());
            return false;
        } else {
            res = a_relation_config[index][key]["resource"].asString();
        }
        relation_on_parent_table = ( ! a_relation_config[index][key].isMember("pg-table") );

        if ( a_relation_config[index][key].isMember("pg-child-id") ) {
            if ( ! a_relation_config[index][key]["pg-child-id"].isString() || a_relation_config[index][key]["pg-child-id"].empty() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid type for 'resources[\"%s\"][\"%s\"][\"%s\"][\"pg-child-id\"]', string is expected",
                           type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", key.c_str());
                return false;
            }
            col_child = a_relation_config[index][key]["pg-child-id"].asString();
        } else {
            col_child = key + "_id";
        }
    }

    if ( ! IsNewFieldNameValid(key) ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid field '%s' on 'resources[\"%s\"][\"%s\"][%d]', name is reserved or duplicated",
                   key.c_str(), type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", index);
        return false;
    }
    relationships_[key].field_type_ = a_type;
    relationships_[key].resource_type_ = res;
    if ( relation_on_parent_table ) {
        if ( a_relation_config[index].isObject() ) {
            const std::string members[] = {"pg-schema", "pg-parent-id", "pg-condition"};
            for ( size_t i = 0; i < sizeof(members)/sizeof(members[0]); ++i )
            {
                if ( a_relation_config[index][key].isMember(members[i]) ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "configuration of 'resources[\"%s\"][\"%s\"][\"%s\"]' has \"%s\" specified but \"pg-table\" is missing",
                               type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", key.c_str(), members[i].c_str());
                    return false;
                }
            }
        }

        if ( 0 == q_main_.select_columns_.size() ) {
            q_main_.select_columns_ = GetPGQueryColId() + " AS id";
        }
        q_main_.select_columns_ += ",\"" + col_child + "\" AS \"" + key + "\"";
        q_main_.columns_[key] = "\"" + col_child + "\"";
        return true;
    }

    q_relations_[key].schema_.clear();
    q_relations_[key].use_rq_accounting_schema_ = parent_doc_->UseRequestAccountingSchema();
    q_relations_[key].use_rq_sharded_schema_ = parent_doc_->UseRequestShardedSchema();
    q_relations_[key].use_rq_company_schema_ = parent_doc_->UseRequestCompanySchema();
    q_relations_[key].table_.clear();
    q_relations_[key].order_by_ = parent_doc_->DefaultOrder();
    q_relations_[key].use_rq_accounting_prefix_ = parent_doc_->UseRequestAccountingPrefix();
    q_relations_[key].show_links_ = q_main_.show_links_;
    q_relations_[key].show_null_ = q_main_.show_null_;
    q_relations_[key].col_parent_id_ = type_ + "_id";
    q_relations_[key].col_child_id_ = col_child;
    q_relations_[key].condition_.clear();
    q_relations_[key].select_columns_.clear();

    BoolOption bool_options[] = {
        {"request-accounting-schema", &q_relations_[key].use_rq_accounting_schema_},
        {"request-sharded-schema",    &q_relations_[key].use_rq_sharded_schema_},
        {"request-company-schema",    &q_relations_[key].use_rq_company_schema_},
        {"request-accounting-prefix", &q_relations_[key].use_rq_accounting_prefix_},
        {"show-links",                &q_relations_[key].show_links_},
        {"show-null",                 &q_relations_[key].show_null_}
    };
    StringOption str_options[] = {
        {"pg-schema",    &q_relations_[key].schema_},
        {"pg-table",     &q_relations_[key].table_},
        {"pg-order-by",  &q_relations_[key].order_by_},
        {"pg-parent-id", &q_relations_[key].col_parent_id_},
        {"pg-condition", &q_relations_[key].condition_}
    };

    for ( size_t i = 0; i < sizeof(bool_options)/sizeof(bool_options[0]); ++i ) {
        const JsonapiJson::Value& option = a_relation_config[index][key][bool_options[i].name];
        if ( ! option.isNull() ) {
            if ( false == option.isBool() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"%s\"][\"%s\"]', boolean is expected",
                           type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", bool_options[i].name);
                return false;
            } else {
                *(bool_options[i].ptr) = option.asBool();
            }
        }
    }

    for ( size_t i = 0; i < sizeof(str_options)/sizeof(str_options[0]); ++i ) {
        const JsonapiJson::Value& option = a_relation_config[index][key][str_options[i].name];
        if ( ! option.isNull() ) {
            if ( false == option.isString() || option.asString().empty() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"%s\"][\"%s\"]', string is expected",
                           type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", str_options[i].name);
                return false;
            } else {
                *(str_options[i].ptr) = option.asString();
            }
        }
    }

    if ( q_relations_[key].use_rq_accounting_schema_ || q_relations_[key].use_rq_sharded_schema_ || q_relations_[key].use_rq_company_schema_ ) {
        int count = 0;
        if ( q_relations_[key].schema_.length() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "incompatible configuration of 'resources[\"%s\"][\"%s\"][\"%s\"][\"pg-schema\"]',"
                                                                                                    " \"pg-schema\" may only be defined if \"request-accounting-schema\", \"request-sharded-schema\" and \"request-company-schema\" are false",
                                                                                                    type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", key.c_str());
        }
        count  = q_relations_[key].use_rq_accounting_schema_ ? 1 : 0;
        count += q_relations_[key].use_rq_sharded_schema_ ? 1 : 0;
        count += q_relations_[key].use_rq_company_schema_ ? 1 : 0;
        if ( count > 1 ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "incompatible configuration of 'resources[\"%s\"][\"%s\"][\"%s\"]',"
                                                                                                    " \"request-accounting-schema\", \"request-sharded-schema\" and \"request-company-schema\" cannot be true simultaneously",
                                                                                                    type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", key.c_str());
        }
    } else if ( 0 == q_relations_[key].schema_.length() ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid configuration of 'resources[\"%s\"][\"%s\"][\"%s\"]',"
                                                                                                " relationship schema must be configured with: \"pg-schema\", \"request-accounting-schema\", \"request-sharded-schema\" or \"request-company-schema\"",
                                                                                                type_.c_str(), EIsToOne == a_type ? "to-one" : "to-many", key.c_str());
    }
    q_relations_[key].select_columns_ += "\""+ q_relations_[key].col_parent_id_ + "\" AS id";
    q_relations_[key].select_columns_ += ",\"" + col_child + "\" AS \"" + key + "\"";

    return true;
}

/**
 * @brief Set the resource configuration from parsed json object.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::ResourceConfig::SetValues(const JsonapiJson::Value& a_config)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, type_.c_str())));

    bool rv = true;

    q_main_.schema_.clear();
    q_main_.use_rq_accounting_schema_ = parent_doc_->UseRequestAccountingSchema();
    q_main_.use_rq_sharded_schema_    = parent_doc_->UseRequestShardedSchema();
    q_main_.use_rq_company_schema_    = parent_doc_->UseRequestCompanySchema();
    q_main_.use_rq_accounting_prefix_ = parent_doc_->UseRequestAccountingPrefix();
    q_main_.table_ = type_;
    q_main_.order_by_ = parent_doc_->DefaultOrder();
    q_main_.needs_search_path_ = false;
    q_main_.page_size_ = parent_doc_->PageSize();
    q_main_.show_links_ = parent_doc_->ShowLinks();
    q_main_.show_null_ = parent_doc_->ShowNull();
    q_main_.col_id_ = "id";
    q_main_.condition_.clear();
    q_main_.select_columns_.clear();

    BoolOption bool_options[] = {

        {"request-accounting-schema",   &q_main_.use_rq_accounting_schema_},
        {"request-sharded-schema",      &q_main_.use_rq_sharded_schema_},
        {"request-company-schema",      &q_main_.use_rq_company_schema_},
        {"request-accounting-prefix",   &q_main_.use_rq_accounting_prefix_},
        {"returns-json",                &q_main_.returns_json_},
        {"pg-set-search_path",          &q_main_.needs_search_path_},
        {"show-links",                  &q_main_.show_links_},
        {"show-null",                   &q_main_.show_null_}
    };
    UIntOption uint_options[] = {
        {"page-size",                   &q_main_.page_size_}
    };
    StringOption str_options[] = {
        {"pg-schema",                               &q_main_.schema_},
        {"pg-table",                                &q_main_.table_},
        {"pg-function",                             &q_main_.function_},
        {"pg-attributes-function",                  &q_main_.attributes_function_},
        {"pg-order-by",                             &q_main_.order_by_},
        {"pg-id",                                   &q_main_.col_id_},
        {"pg-condition",                            &q_main_.condition_},
        {"job-tube",                                &q_main_.job_tube_},
        {"request-accounting-schema-function-arg",  &q_main_.function_arg_rq_accounting_schema_},
        {"request-sharded-schema-function-arg",     &q_main_.function_arg_rq_sharded_schema_},
        {"request-company-schema-function-arg",     &q_main_.function_arg_rq_company_schema_},
        {"request-accounting-prefix-function-arg",  &q_main_.function_arg_rq_accounting_prefix_},
        {"request-user-function-arg",               &q_main_.function_arg_rq_user_},
        {"request-company-function-arg",            &q_main_.function_arg_rq_company_},
        {"request-id-function-arg",                 &q_main_.function_arg_rq_col_id_},
        {"request-count-function-arg",              &q_main_.function_arg_rq_count_},
        {"request-filter-function-arg",             &q_main_.function_arg_rq_filter_},
        {"request-offset-function-arg",             &q_main_.function_arg_rq_page_offset_},
        {"request-limit-function-arg",              &q_main_.function_arg_rq_page_limit_}
    };

    for ( size_t i = 0; i < sizeof(bool_options)/sizeof(bool_options[0]); ++i ) {
        const JsonapiJson::Value& option = a_config[bool_options[i].name];
        if ( ! option.isNull() ) {
            if ( false == option.isBool() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"%s\"]', boolean is expected",
                           type_.c_str(), bool_options[i].name);
                rv = false;
            } else {
                *(bool_options[i].ptr) = option.asBool();
            }
        }
    }
    for ( size_t i = 0; i < sizeof(uint_options)/sizeof(uint_options[0]); ++i ) {
        const JsonapiJson::Value& option = a_config[uint_options[i].name];
        if ( ! option.isNull() ) {
            if ( false == option.isUInt() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"%s\"]', uint is expected",
                            type_.c_str(), uint_options[i].name);
                rv = false;
            } else {
                *(uint_options[i].ptr) = option.asUInt();
            }
        }
    }
    for ( size_t i = 0; i < sizeof(str_options)/sizeof(str_options[0]); ++i ) {
        const JsonapiJson::Value& option = a_config[str_options[i].name];
        if ( ! option.isNull() ) {
            if ( false == option.isString() || option.empty() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"%s\"]', string is expected",
                           type_.c_str(), str_options[i].name);
                rv = false;
            } else {
                *(str_options[i].ptr) = option.asString();
            }
        }
    }

    if ( a_config.isMember("job-methods") ) {
        if ( q_main_.job_tube_.empty() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid key 'resources[\"%s\"][\"job-methods\"]', job requires specification of 'resources[\"%s\"][\"job-tube\"]' ", type_.c_str(), type_.c_str());
            rv = false;
        } else if ( false == a_config["job-methods"].isArray() || a_config["job-methods"].empty() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"job-methods\"]', array is expected", type_.c_str());
            rv = false;
        } else {
            for (unsigned int index = 0; index < a_config["job-methods"].size(); index++) {
                if ( false == a_config["job-methods"][index].isString() || a_config["job-methods"][index].empty() ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value in 'resources[\"%s\"][\"job-methods\"]', string is expected", type_.c_str());
                    rv = false;
                } else {
                    if ( ! QueryBuilder::IsValidHttpMethod(a_config["job-methods"][index].asString()) ) {
                        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA012"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value '%s' in 'resources[\"%s\"][\"job-methods\"]', supported HTTP method is expected in uppercase", a_config["job-methods"][index].asString().c_str(), type_.c_str());
                        rv = false;
                    } else {
                        q_main_.job_methods_.insert(a_config["job-methods"][index].asString());
                    }
                }
            }
        }
    }

    if ( IsQueryFromFunction() ) {
        const char* incompatible_options[] = {"pg-table", "pg-attributes-function", "pg-order-by"};
        for ( size_t i = 0; i < sizeof(incompatible_options)/sizeof(incompatible_options[0]); ++i ) {
            if ( !a_config[incompatible_options[i]].isNull() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "conflicting key 'resources[\"%s\"][\"%s\"]' may only be used with 'resources[\"%s\"][\"pg-function\"]'",
                            type_.c_str(), incompatible_options[i], type_.c_str());
            }
        }
        if (   (!a_config["request-offset-function-arg"].isNull() &&  a_config["request-limit-function-arg"].isNull())
            || ( a_config["request-offset-function-arg"].isNull() && !a_config["request-limit-function-arg"].isNull())
        ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "inconsistent keys 'resources[\"%s\"][\"offset-function-arg\"]' and 'resources[\"%s\"][\"limit-function-arg\"]', both keys must be provided if function supports pagination",
                                                                                                    type_.c_str(), type_.c_str());
        }
        q_main_.table_.clear();
    } else {
        const char* incompatible_options[] = {"returns-json", "request-accounting-schema-function-arg", "request-sharded-schema-function-arg", "request-company-schema-function-arg", "request-accounting-prefix-function-arg", "request-user-function-arg", "request-company-function-arg", "request-id-function-arg", "request-count-function-arg", "request-filter-function-arg", "request-offset-function-arg", "request-limit-function-arg"};
        for ( size_t i = 0; i < sizeof(incompatible_options)/sizeof(incompatible_options[0]); ++i ) {
            if ( !a_config[incompatible_options[i]].isNull() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "conflicting keys 'resources[\"%s\"][\"pg-function\"]' and 'resources[\"%s\"][\"%s\"]'",
                            type_.c_str(), type_.c_str(), incompatible_options[i]);
            }
        }
    }

    if ( q_main_.use_rq_accounting_schema_ || q_main_.use_rq_sharded_schema_ || q_main_.use_rq_sharded_schema_ ) {
        int count = 0;
        if ( q_main_.schema_.length() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "incompatible configuration of 'resources[\"%s\"][\"pg-schema\"]',"
                                                                                                    " \"pg-schema\" may only be defined if \"request-accounting-schema\", \"request-sharded-schema\" and \"request-company-schema\" are false",
                                                                                                    type_.c_str());
        }
        count  = q_main_.use_rq_accounting_schema_ ? 1 : 0;
        count += q_main_.use_rq_sharded_schema_ ? 1 : 0;
        count += q_main_.use_rq_company_schema_ ? 1 : 0;
        if ( count > 1 ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "incompatible configuration of 'resources[\"%s\"]',"
                                                                                                    " \"request-accounting-schema\", \"request-sharded-schema\" and \"request-company-schema\" cannot be true simultaneously",
                                                                                                    type_.c_str());
        }
    }

    if ( a_config.isMember("attributes") ) {
        if ( ! a_config["attributes"].isArray() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"attributes\"]', array is expected", type_.c_str());
            rv = false;
        } else {
            q_main_.select_columns_ = GetPGQueryColId() + " AS id";
            for (unsigned int index = 0; index < a_config["attributes"].size(); index++) {
                rv &= SetAttribute(a_config["attributes"][index]);
            }
        }
    } else if ( IsQueryFromAttributesFunction() ) {
        q_main_.select_columns_ = GetPGQueryColId() + " AS id, pgf.*";
    } else if ( parent_doc_->HasAttrRestriction() ) {
        q_main_.select_columns_ = "" + GetPGQueryColId() + " AS id";
    } else {
        q_main_.select_columns_ = "*";
    }

    if ( a_config.isMember("to-one") ) {
        if ( ! a_config["to-one"].isArray() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"to-one\"]', array is expected", type_.c_str());
            rv = false;
        } else {
            for (unsigned int index = 0; index < a_config["to-one"].size(); index++) {
                rv &= SetRelationship(EIsToOne, a_config["to-one"], index);
            }
        }
    }

    if ( a_config.isMember("to-many") ) {
        if ( ! a_config["to-many"].isArray() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"to-many\"]', array is expected", type_.c_str());
            rv = false;
        } else {
            for (unsigned int index = 0; index < a_config["to-many"].size(); index++) {
                rv &= SetRelationship(EIsToMany, a_config["to-many"], index);
            }
        }
    }

    if ( a_config.isMember("observed") ) {
        if ( ! a_config["observed"].isArray() ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"observed\"]', array is expected", type_.c_str());
            rv = false;
        } else {
            for (unsigned int index = 0; index < a_config["observed"].size(); index++) {
                rv &= SetObserved(a_config["observed"][index]);
            }
        }
    }

    return rv;
}

Oid pg_jsonapi::ResourceConfig::GetRelid(std::string a_type, std::string a_relnamespace, std::string a_relname)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s - %s.%s", __FUNCTION__, a_type.c_str(), a_relnamespace.c_str(), a_relname.c_str())));

    Oid relid = InvalidOid;

    if ( a_relnamespace.size() ) {
        Oid s_oid = get_namespace_oid(a_relnamespace.c_str(), true);
        if ( ! OidIsValid(s_oid) ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s': schema '%s' does not exist", a_type.c_str(), a_relnamespace.c_str() );
            return InvalidOid;
        } else {
            relid = get_relname_relid( a_relname.c_str(), s_oid);
            if ( ! OidIsValid(relid) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s': relation %s.%s does not exist", a_type.c_str(), a_relnamespace.c_str(), a_relname.c_str() );
                return InvalidOid;
            }
        }
    } else {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s': schema was not provided", a_type.c_str() );
        /*
        relid = RelnameGetRelid( a_relname.c_str() );
        if ( ! OidIsValid(relid) ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s': relation %s does not exist", a_type.c_str(), a_relname.c_str() );
            return InvalidOid;
        }
        */
    }

    return relid;
}

/**
 * @brief Validate the resource configuration against the database if possible.
 *        If resource depends on schema or table preffix, return with success.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::ResourceConfig::ValidatePG(bool a_specific_request)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s - %s", __FUNCTION__, type_.c_str(), a_specific_request ? "true" : "false")));

    if ( a_specific_request && q_main_.needs_search_path_ ) {
        g_qb->RequireSearchPath();
    }

    if ( q_main_.use_rq_accounting_schema_ || q_main_.use_rq_sharded_schema_ || q_main_.use_rq_company_schema_ || q_main_.use_rq_accounting_prefix_ || IsQueryFromFunction() || GetJobTube().length() ) {
        if ( a_specific_request ) {
            if ( 0 == g_qb->GetRequestAccountingSchema().length() && (q_main_.use_rq_accounting_schema_ || q_main_.function_arg_rq_accounting_schema_.length() ) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'accounting_schema'",
                                                                                              type_.c_str());
                return false;
            }
            if ( 0 == g_qb->GetRequestAccountingPrefix().length() && (q_main_.use_rq_accounting_prefix_ || q_main_.function_arg_rq_accounting_prefix_.length() ) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'accounting_prefix'",
                                                                                              type_.c_str());
                return false;
            }
            if ( 0 == g_qb->GetRequestShardedSchema().length() && (q_main_.use_rq_sharded_schema_ || q_main_.function_arg_rq_sharded_schema_.length() ) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'sharded_schema'",
                                                                                              type_.c_str());
                return false;
            }
            if ( 0 == g_qb->GetRequestCompanySchema().length() && (q_main_.use_rq_company_schema_ || q_main_.function_arg_rq_company_schema_.length() ) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'company_schema'",
                                                                                              type_.c_str());
                return false;
            }
            if ( 0 == g_qb->GetRequestUser().length() && q_main_.function_arg_rq_user_.length() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'user_id'",
                                                                                              type_.c_str());
                return false;
            }
            if ( 0 == g_qb->GetRequestCompany().length() && q_main_.function_arg_rq_company_.length() ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'company_id'",
                                                                                              type_.c_str());
                return false;
            }
            if ( !IsQueryFromFunction() && !g_qb->IsTopQueryFromJobTube() ) {
                std::string table_name;
                if ( q_main_.use_rq_accounting_prefix_ ) {
                    table_name = g_qb->GetRequestAccountingPrefix();
                }
                table_name += q_main_.table_;
                if ( ! OidIsValid( GetRelid(GetType(), GetPGQuerySchema(), table_name) ) ) {
                    return false;
                }
            }
        }
    } else {
        if ( ! OidIsValid( GetRelid(GetType(), q_main_.schema_, q_main_.table_) ) ) {
            return false;
        }
    }

    if ( a_specific_request ) {
        for ( ResourceConfig::RelationshipMap::const_iterator rel = GetRelationships().begin(); rel != GetRelationships().end(); ++rel ) {
            ereport(DEBUG3, (errmsg_internal("jsonapi: %s res=%s rel=%s", __FUNCTION__, type_.c_str(), rel->first.c_str())));

            const std::string& rel_type = GetFieldResourceType(rel->first);

            if ( IsPGChildRelation(rel_type) ) {
                std::string rel_table;

                if ( q_relations_.at(rel_type).use_rq_accounting_schema_ && 0 == g_qb->GetRequestAccountingSchema().length() ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'accounting_schema'",
                                                                                                  type_.c_str());
                    return false;
                }
                if ( q_relations_.at(rel_type).use_rq_accounting_prefix_ && 0 == g_qb->GetRequestAccountingPrefix().length() ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'accounting_prefix'",
                                                                                                  type_.c_str());
                    return false;
                }
                if ( q_relations_.at(rel_type).use_rq_sharded_schema_ && 0 == g_qb->GetRequestShardedSchema().length() ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'sharded_schema'",
                                                                                                  type_.c_str());
                    return false;
                }
                if ( q_relations_.at(rel_type).use_rq_company_schema_ && 0 == g_qb->GetRequestCompanySchema().length() ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "requests for resource '%s' require parameter 'company_schema'",
                                                                                                  type_.c_str());
                    return false;
                }

                AddPGRelationQueryTable(rel_table, rel_type);
                if ( ! OidIsValid( GetRelid(GetType(), GetPGRelationQuerySchema(rel_type), rel_table) ) ) {
                    return false;
                }
            }
        }
    }

    return true;
}

Oid pg_jsonapi::ResourceConfig::GetOid() const
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, type_.c_str())));
    Oid relid = InvalidOid;

    if ( !IsQueryFromFunction() ) {
        if ( q_main_.use_rq_company_schema_ || q_main_.use_rq_sharded_schema_ || q_main_.use_rq_accounting_schema_ || q_main_.use_rq_accounting_prefix_ ) {
            std::string table_name;
            if ( q_main_.use_rq_accounting_prefix_ ) {
                table_name += g_qb->GetRequestAccountingPrefix();
            }
            table_name += q_main_.table_;
            relid = GetRelid(GetType(), GetPGQuerySchema(), table_name);
        } else {
            relid = GetRelid(GetType(), q_main_.schema_, q_main_.table_);
        }
    }

    return relid;
}

/**
 * @brief Set the configuration of an observed resource parsed json object.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::ResourceConfig::SetObserved(const JsonapiJson::Value& a_observed_config)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s %s", __FUNCTION__, type_.c_str())));

    std::string key;
    std::string name;

    if ( a_observed_config.isString() ) {
        key = a_observed_config.asString();
        name = key;
    } else {
        if ( ! a_observed_config.isObject() || a_observed_config.size() > 1 ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources[\"%s\"][\"observed\"]', member must be string or object",
                                                                                                    type_.c_str());
            return false;
        }
        key = a_observed_config.getMemberNames()[0];
        if ( a_observed_config[key].isMember("meta-name") ) {
            name =  a_observed_config[key]["meta-name"].asString();
        } else {
            name = key;
        }
    }
    if ( IsObserved(key) ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid field '%s' on 'resources[\"%s\"][\"observed\"]', name is reserved or duplicated",
                                                                                                key.c_str(), type_.c_str());
        return false;
    }
    observed_[key] = name;

    return true;
}
