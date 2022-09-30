/**
 * @file query_builder.rl Implementation of QueryBuilder
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
extern "C" {
#include "postgres.h"
#include "executor/spi.h"
#include "access/xact.h"
#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/json.h"
#include "parser/parse_coerce.h"
#include "pgstat.h"
} // extern "C"
#include "query_builder.h"
#include "utils_adt_json.h"
#include <tuple>
#include <regex>

/**
 * @brief Constructor.
 */
pg_jsonapi::QueryBuilder::QueryBuilder ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    config_ = NULL;

    rq_method_ = "GET";
    rq_extension_ = E_EXT_NONE;
    rq_relationship_ = false;
    rq_links_param_ = -1; // undefined
    rq_totals_param_ = -1; // undefined
    rq_null_param_ = -1; // undefined
    rq_page_size_param_ = -1; // undefined
    rq_page_number_param_ = -1; // undefined

    spi_connected_ = false;
    spi_read_only_ = true;
    q_required_count_ = 0;
    q_top_must_be_included_ = false;
    q_top_total_rows_ = 0;
    q_top_grand_total_rows_ = 0;
    q_page_size_ = 0;
    q_page_number_ = 0;
    q_http_status_ = E_HTTP_OK;
    q_json_function_data_ = NULL;
    q_json_function_included_ = NULL;
    q_needs_search_path_ = false;
}

/**
 * @brief Destructor.
 */
pg_jsonapi::QueryBuilder::~QueryBuilder ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
    /*
     * delete contained objects
     */
}

/**
 * @brief Reset this node, keeping previously prepared or loaded documents configuration per base url.
 */
void pg_jsonapi::QueryBuilder::Clear ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
    /*
     * clear contained objects
     */

    // Resource Specification - initialized only once by base_url
    config_ = NULL;

    // Attributes - request variables filled while parsing request
    rq_method_ = "GET";
    rq_extension_ = E_EXT_NONE;
    rq_url_encoded_.clear();
    rq_base_url_.clear();
    if ( rq_body_root_.isArray() || rq_body_root_.isObject() ) {
        rq_body_root_.clear();
    }
    rq_accounting_schema_.clear();
    rq_sharded_schema_.clear();
    rq_company_schema_.clear();
    rq_accounting_prefix_.clear();
    rq_user_id_.clear();
    rq_company_id_.clear();
    rq_resource_type_.clear();
    rq_resource_id_.clear();
    rq_related_.clear();
    rq_relationship_ = false;
    rq_include_param_.clear();
    rq_sort_param_.clear();
    rq_fields_param_.clear();
    rq_filter_field_param_.clear();
    rq_filter_param_.clear();
    rq_links_param_ = -1; // undefined
    rq_totals_param_ = -1; // undefined
    rq_null_param_ = -1; // undefined
    rq_page_size_param_ = -1; // undefined
    rq_page_number_param_ = -1; // undefined
    rq_operations_.clear();

    // Attributes - used to query postgres and keep results
    spi_connected_ = false;
    spi_read_only_ = true;
    q_buffer_.clear();
    q_required_count_ = 0;
    q_data_.clear();
    q_to_be_included_.clear();
    q_top_must_be_included_ = false;
    q_top_total_rows_ = 0;
    q_top_grand_total_rows_ = 0;
    q_page_size_ = 0;
    q_page_number_ = 0;
    q_errors_.clear();
    q_http_status_ = E_HTTP_OK;
    q_json_function_data_ = NULL;
    q_json_function_included_ = NULL;
    q_needs_search_path_ = false;
    q_old_search_path_.clear();
}

/**
 * @brief Create a new error object.
 *
 * @li a_status The HTTP status code for the error.
 *
 * @return Reference for the new error.
 */
pg_jsonapi::ErrorObject& pg_jsonapi::QueryBuilder::AddError(int a_sqlerrcode, HttpStatusErrorCode a_status, bool a_operation)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( q_http_status_ < E_HTTP_ERROR_BAD_REQUEST ) {
        q_http_status_ = (HttpStatusCode)a_status;
    }

    ErrorObject error(a_sqlerrcode, a_status, a_operation);
    q_errors_.push_back(error);
    return q_errors_.back();
}

bool pg_jsonapi::QueryBuilder::IsValidHttpMethod (const std::string& a_method)
{
    // ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
    return ( "GET" == a_method || "POST" == a_method || "PATCH" == a_method || "DELETE" == a_method );
}

bool pg_jsonapi::QueryBuilder::IsValidSQLCondition (const std::string& a_condition)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s checking: %s", __FUNCTION__, a_condition.c_str())));

    std::smatch m;
    std::string s = a_condition;
    std::regex e ("(?:'[^']+'|;|\\b(SELECT|INSERT|UPDATE|DELETE|CREATE|ALTER|DROP|TRUNCATE|EXECUTE|PG_\\w+|current_database|current_setting|query_to_xml|version|lo_from_bytea|lo_put|lo_export|getpgusername)\\b)", std::regex_constants::ECMAScript|std::regex_constants::icase);
    while (std::regex_search (s,m,e)) {
        if ('\'' != m[0].str()[0] ) {
            ereport(DEBUG1, (errmsg_internal("invalid condition: %s", m[0].str().c_str())));
            return false;
        }
        s = m.suffix().str();
    }
    return true;
}

/**
 * @brief Parse the request components and check if they are a valid JSONAPI request.
 *
 * @param a_method The http request method: GET, POST, PATCH or DELETE.
 * @param a_url The request url.
 * @param a_body The request body.
 * @param a_accounting_schema The schema to be used.
 * @param a_accounting_prefix The preffix to be added to define the resource relation.
 *
 * @return @li true if parsing succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ParseRequestArguments (const char* a_method, size_t a_method_len,
                                                      const char* a_url, size_t a_url_len,
                                                      const char* a_body, size_t a_body_len,
                                                      const char* a_user_id, size_t a_user_id_len,
                                                      const char* a_company_id, size_t a_company_id_len,
                                                      const char* a_company_schema, size_t a_company_schema_len,
                                                      const char* a_sharded_schema, size_t a_sharded_schema_len,
                                                      const char* a_accounting_schema, size_t a_accounting_schema_len,
                                                      const char* a_accounting_prefix, size_t a_accounting_prefix_len)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s a_method=%.*s a_url=%.*s a_body len=%zu a_accounting_schema=%.*s a_sharded_schema=%.*s a_company_schema=%.*s a_accounting_prefix=%.*s",
                                     __FUNCTION__,
                                     (int)a_method_len, a_method,
                                     (int)a_url_len, a_url,
                                     a_body_len,
                                     (int)a_accounting_schema_len, a_accounting_schema,
                                     (int)a_sharded_schema_len, a_sharded_schema,
                                     (int)a_company_schema_len, a_company_schema,
                                     (int)a_accounting_prefix_len, a_accounting_prefix)));

    JsonapiJson::Reader reader(JsonapiJson::Features::strictMode());

    if ( 0 == a_method_len )
    {
        AddError(JSONAPI_MAKE_SQLSTATE("JA012"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "empty or null request");
        return false;
    }

    rq_method_ = std::string(a_method, 0, a_method_len);
    if ( ! IsValidHttpMethod(rq_method_) ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA012"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "method '%s' is not valid", rq_method_.c_str());
        return false;
    }

    rq_url_encoded_ = std::string(a_url, 0, a_url_len);
    if ( ! ParseUrl() ) {
        return false;
    }
    if ( "PATCH" != rq_method_  && GetResourceType().empty() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA010"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "root url is only allowed on PATCH operations");
        return false;
    }

    if ( ! ParseRequestBody(a_body, a_body_len) ) {
        return false;
    }

    if ( a_accounting_schema_len ) {
        rq_accounting_schema_ = std::string(a_accounting_schema, 0, a_accounting_schema_len);
    }

    if ( a_sharded_schema_len ) {
        rq_sharded_schema_ = std::string(a_sharded_schema, 0, a_sharded_schema_len);
    }

    if ( a_company_schema_len ) {
        rq_company_schema_ = std::string(a_company_schema, 0, a_company_schema_len);
    }

    if ( a_accounting_prefix_len ) {
        rq_accounting_prefix_ = std::string(a_accounting_prefix, 0, a_accounting_prefix_len);
    }

    if ( a_user_id_len ) {
        rq_user_id_ = std::string(a_user_id, 0, a_user_id_len);
    }

    if ( a_company_id_len ) {
        rq_company_id_ = std::string(a_company_id, 0, a_company_id_len);
    }
    return true;
}

/**
 * @brief Parse the request URL.
 *
 * @return @li true if parsing succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ParseUrl ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    int   cs;
    const char* p     = rq_url_encoded_.c_str();
    const char* pe    = p + rq_url_encoded_.length();
    const char* eof   = pe;
    const char* start = NULL;
    const char* key_s = NULL;
    const char* key_e = NULL;

    bool        has_include = false;
    std::string type;
    std::string field;

    %%{
        machine JSONAPIUrl;
        include RequestBasicElements "basic_elements.rlh";

        # complete uri
        main := base_url ( '/'? | ('/' resource ('/' id ( ( relationship )? '/' related )? )? ( '?' param ('&' param)* )? )? );
        write data;
        write init;
        write exec;
    }%%

    if ( HasErrors() ) {
        return false;
    }

    if ( cs < JSONAPIUrl_first_final)
    {
        AddError(JSONAPI_MAKE_SQLSTATE("JA010"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "parse_error near col %d (%*s).", int(p - rq_url_encoded_.c_str()), int(pe-p), p);
        return false;
    }
    (void) JSONAPIUrl_en_main;
    (void) JSONAPIUrl_error;
    return true;
}

/**
 * @brief Parse the request body.
 *
 * @return @li true if parsing succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ParseRequestBody (const char* a_body, size_t a_body_len)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    bool rv = true;
    JsonapiJson::Reader reader(JsonapiJson::Features::strictMode());
    OperationType op_type = E_OP_UPDATE;

    if ( 0 == a_body_len ) {
        if ( "POST" == rq_method_ || "PATCH" == rq_method_ ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA014"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "method '%s' requires body", rq_method_.c_str());
            return false;
        }
        if ( "DELETE" == rq_method_ ) {
            rq_operations_.resize( 1 );
            rq_operations_[0].SetRequestType(0, E_OP_DELETE);
            if ( ! rq_operations_[0].SetRequest(NULL) ) {
                return false;
            }
        }
        return true;
    }

    if ( "GET" == rq_method_ ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "body should not be given for method '%s'", rq_method_.c_str());
        return false;
    }

    if (   ! reader.parse(a_body, a_body + a_body_len, rq_body_root_, false)
        || ! ( rq_body_root_.isArray() || rq_body_root_.isObject() ) ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid json on request body: %s",
                    reader.getFormatedErrorMessages().c_str());
        return false;
    }

    if ( rq_body_root_.isArray() ) {
        rq_extension_ = E_EXT_JSON_PATCH;

        if ( "PATCH" != rq_method_ ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "array request is invalid for method '%s', array is only allowed as JSON PATCH extension", rq_method_.c_str());
            return false;
        }
        rq_operations_.resize( rq_body_root_.size() );
        for ( int i = 0; i < (int)rq_operations_.size(); i++ ) {
            if ( ! (   rq_body_root_[i].isObject()
                    && 3 == rq_body_root_[i].size()
                    && rq_body_root_[i].isMember("op") && rq_body_root_[i]["op"].isString()
                    && rq_body_root_[i].isMember("path") && rq_body_root_[i]["path"].isString()
                    ) ) {
                AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "request body MUST contain an array of operations in JSON Patch format");
                rv = false;
            } else {
                if ( "add" == rq_body_root_[i]["op"].asString() ) {
                    op_type = E_OP_CREATE;
                } else if ( "replace" == rq_body_root_[i]["op"].asString() ) {
                    op_type = E_OP_UPDATE;
                } else if ( "remove" == rq_body_root_[i]["op"].asString() ) {
                    op_type = E_OP_DELETE;
                } else {
                    op_type = E_OP_UNDEFINED;
                    AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "operation '%s' is not valid or not implemented", rq_body_root_[i]["op"].asString().c_str() );
                    rv = false;
                }
                rq_operations_[i].SetRequestType(i, op_type);

                if (E_OP_UNDEFINED != op_type ) {
                    if ( ! (   rq_body_root_[i].isObject()
                            && 3 == rq_body_root_[i].size()
                            && rq_body_root_[i].isMember("op")   && rq_body_root_[i]["op"].isString()
                            && rq_body_root_[i].isMember("path") && rq_body_root_[i]["path"].isString()
                            ) ) {
                        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "request body MUST contain an array of operations in JSON Patch format");
                        rv = false;
                    } else if ( ! rq_operations_[i].SetRequest(&rq_body_root_[i]["value"], rq_body_root_[i]["path"].asString()) ) {
                        rv= false;
                    }
                }
            }
        }
    } else {
        if ( 1 != rq_body_root_.size() || ! rq_body_root_.isMember("data") ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "request body MUST contain a data member whose value is %s",
                       ( "DELETE" != rq_method_ ) ?
                       "an array of resource identifier objects" : "an array of resource objects");
            return false;
        }

        if ( "POST" == rq_method_ ) {
            op_type = E_OP_CREATE;
        } else if ( "PATCH" == rq_method_ ) {
            op_type = E_OP_UPDATE;
        } else if ( "DELETE" == rq_method_ ) {
            op_type = E_OP_DELETE;
        } // else never hapens

        if ( rq_body_root_["data"].isObject() ) {
            rq_operations_.resize( 1 );
            rq_operations_[0].SetRequestType(0, op_type);
            if ( ! rq_operations_[0].SetRequest(&rq_body_root_["data"]) ) {
                return false;
            }
        } else {
            rq_extension_ = E_EXT_BULK;
            rq_operations_.resize( rq_body_root_["data"].size() );
            for ( int i = 0; i < (int)rq_operations_.size(); i++ ) {
                rq_operations_[i].SetRequestType(i, op_type);
                if ( ! rq_operations_[i].SetRequest(&rq_body_root_["data"][i]) ) {
                    return false;
                }
            }
        }
    }

    return rv;
}

/**
 * @brief Validate the request agains the document configuration.
 *
 * @return @li true if parsing succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ValidateRequest()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    std::map<std::string, pg_jsonapi::DocumentConfig>::iterator it = config_map_.find(rq_base_url_);
    if ( config_map_.end() == it  ) {
        bool config_exists = false;
        /* get configuration from DB and keep it for later use */
        if ( requested_urls_.end() == requested_urls_.find(rq_base_url_) ) {
            requested_urls_.insert(rq_base_url_);
            config_map_.insert(std::pair<std::string, pg_jsonapi::DocumentConfig>(rq_base_url_, DocumentConfig(rq_base_url_)));
            it = config_map_.find(rq_base_url_);
            it->second.LoadConfigFromDB(config_exists);
            if ( ! config_exists ) {
                config_map_.erase(it);
                it = config_map_.end();
            }
        }
        if ( config_map_.end() == it && ! config_exists ) {
            it = config_map_.find("default");
            if ( config_map_.end() == it  ) {
                config_map_.insert(std::pair<std::string, pg_jsonapi::DocumentConfig>("default", DocumentConfig("default")));
                it = config_map_.find("default");
                if ( false == it->second.LoadConfigFromDB(config_exists) ) {
                    config_map_.erase(it);
                    it = config_map_.end();
                    return false;
                }
            }
        }
    }
    config_ = &(it->second);

    if ( GetResourceType().length() ) {
        if ( ! config_->ValidateRequest(GetResourceType(), GetRelated()) ) {
            return false;
        }
    }

    for ( int i = 0; i < (int)rq_operations_.size(); i++ ) {
        if ( ( GetResourceType() != rq_operations_[i].GetResourceType() || GetRelated() != rq_operations_[i].GetRelated() )
            && ! config_->ValidateRequest(rq_operations_[i].GetResourceType(), rq_operations_[i].GetRelated()) ) {
            return false;
        }
        if ( E_EXT_NONE != rq_extension_ ) {
            if ( config_->GetResource(rq_operations_[i].GetResourceType()).FunctionReturnsJson() ) {
                rq_operations_[i].AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' cannot be used with bulk or jsonpatch extensions because its a function that returns json", rq_operations_[i].GetResourceType().c_str());
            }
            if ( config_->GetResource(rq_operations_[i].GetResourceType()).HasJobTube(rq_method_) ) {
                rq_operations_[i].AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' cannot be used with bulk or jsonpatch extensions because its configured with job-tube for method %s", rq_operations_[i].GetResourceType().c_str(), rq_method_.c_str());
            }
        }
    }

    for ( StringSetMap::iterator res = rq_fields_param_.begin(); res != rq_fields_param_.end(); ++res ) {
        for ( StringSet::iterator field = res->second.begin(); field != res->second.end(); ++field ) {
            if ( ! config_->IsValidField(res->first, *field) ) {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' does not have a '%s' field for '%s'",
                             res->first.c_str(), field->c_str(), rq_base_url_.c_str());
                e.SetSourceParam("fields[%s]=%s", res->first.c_str(), field->c_str());
            }
        }
    }

    if ( ! IsTopQueryFromJobTube() ) {

        if ( IsTopQueryFromFunction() ) {
            if ( !rq_filter_param_.empty() && !TopFunctionSupportsFilter() ) {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' is configured as a call to function '%s' which does not support filter",
                                                                                                         GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str());
                e.SetSourceParam("filter=%s", rq_filter_param_.c_str());
            }

            if ( 0 == rq_filter_field_param_.count(GetFunctionArgAccountingSchema()) ) {
                if ( !GetFunctionArgAccountingSchema().empty() ) {
                    rq_filter_field_param_[GetFunctionArgAccountingSchema()] = GetRequestAccountingSchema();
                }
            } else {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "cannot use filter[%s], resource '%s' is configured as a call to function '%s' with request-accounting-schema-function-arg",
                                                                                                         GetFunctionArgAccountingSchema().c_str(), GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str());
                e.SetSourceParam("filter[%s]=%s", GetFunctionArgAccountingSchema().c_str(), rq_filter_field_param_[GetFunctionArgAccountingSchema()].c_str());
            }
            if ( 0 == rq_filter_field_param_.count(GetFunctionArgShardedSchema()) ) {
                if ( !GetFunctionArgShardedSchema().empty() ) {
                    rq_filter_field_param_[GetFunctionArgShardedSchema()] = GetRequestShardedSchema();
                }
            } else {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "cannot use filter[%s], resource '%s' is configured as a call to function '%s' with request-sharded-schema-function-arg",
                                                                                                         GetFunctionArgShardedSchema().c_str(), GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str());
                e.SetSourceParam("filter[%s]=%s", GetFunctionArgShardedSchema().c_str(), rq_filter_field_param_[GetFunctionArgShardedSchema()].c_str());
            }

            if ( 0 == rq_filter_field_param_.count(GetFunctionArgCompanySchema()) ) {
                if ( !GetFunctionArgCompanySchema().empty() ) {
                    rq_filter_field_param_[GetFunctionArgCompanySchema()] = GetRequestCompanySchema();
                }
            } else {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "cannot use filter[%s], resource '%s' is configured as a call to function '%s' with request-company-schema-function-arg",
                                                                                                         GetFunctionArgCompanySchema().c_str(), GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str());
                e.SetSourceParam("filter[%s]=%s", GetFunctionArgCompanySchema().c_str(), rq_filter_field_param_[GetFunctionArgCompanySchema()].c_str());
            }
            if ( 0 == rq_filter_field_param_.count(GetFunctionArgAccountingPrefix()) ) {
                if ( !GetFunctionArgAccountingPrefix().empty() ) {
                    rq_filter_field_param_[GetFunctionArgAccountingPrefix()] = GetRequestAccountingPrefix();
                }
            } else {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "cannot use filter[%s], resource '%s' is configured as a call to function '%s' with request-accounting-prefix-function-arg",
                                                                                                         GetFunctionArgAccountingPrefix().c_str(), GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str());
                e.SetSourceParam("filter[%s]=%s", GetFunctionArgAccountingPrefix().c_str(), rq_filter_field_param_[GetFunctionArgAccountingPrefix()].c_str());
            }
            if ( 0 == rq_filter_field_param_.count(GetFunctionArgUser()) ) {
                if ( !GetFunctionArgUser().empty() ) {
                    rq_filter_field_param_[GetFunctionArgUser()] = GetRequestUser();
                }
            } else {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "cannot use filter[%s], resource '%s' is configured as a call to function '%s' with request-user-function-arg",
                                                                                                         GetFunctionArgUser().c_str(), GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str());
                e.SetSourceParam("filter[%s]=%s", GetFunctionArgUser().c_str(), rq_filter_field_param_[GetFunctionArgUser()].c_str());
            }
            if ( 0 == rq_filter_field_param_.count(GetFunctionArgCompany()) ) {
                if ( !GetFunctionArgCompany().empty() ) {
                    rq_filter_field_param_[GetFunctionArgCompany()] = GetRequestCompany();
                }
            } else {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "cannot use filter[%s], resource '%s' is configured as a call to function '%s' with request-company-function-arg",
                                                                                                         GetFunctionArgCompany().c_str(), GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str());
                e.SetSourceParam("filter[%s]=%s", GetFunctionArgCompany().c_str(), rq_filter_field_param_[GetFunctionArgCompany()].c_str());
            }
            if ( 1 == rq_totals_param_ && !TopFunctionSupportsCounts() ) {
                rq_totals_param_ = 0;
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' is configured as a call to function '%s' which does not support totals",
                                                                                                         GetResourceType().c_str(), config_->GetResource(GetResourceType()).GetPGQueryFunction().c_str() );
                e.SetSourceParam("totals=1");
            }
        } else {
            for ( StringMap::iterator res = rq_filter_field_param_.begin(); res != rq_filter_field_param_.end(); ++res ) {
                if ( ! config_->IsValidField(GetResourceType(), res->first) ) {
                    ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' does not have a '%s' field for '%s'",
                                                                                                             GetResourceType().c_str(), res->first.c_str(), rq_base_url_.c_str());
                    e.SetSourceParam("filter[%s]=%s", res->first.c_str(), res->second.c_str());
                }
            }
        }

        for ( std::vector< std::pair <std::string,std::string> >::iterator res = rq_sort_param_.begin(); res != rq_sort_param_.end(); ++res ) {
            if ( ! ( config_->IsValidField(GetResourceType(), std::get<0>(*res)) || config_->IsIdentifier(std::get<0>(*res)) ) ) {
                ErrorObject& e = AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' does not have a '%s' field for '%s'",
                                                                                                         GetResourceType().c_str(), res->first.c_str(), rq_base_url_.c_str());
                e.SetSourceParam("sort=%s", std::get<0>(*res).c_str());
            }
        }

        /*
         TODO: rq_include_param_
         */

        if ( GetResourceType().length() && ( IsCollection() || ( HasRelated () && ! IsRelationship() ) ) ) {
            std::string type_pag = ( HasRelated() && !IsRelationship() ) ? config_->GetResource(GetResourceType()).GetFieldResourceType(GetRelated()) : GetResourceType();
            if ( -1 == rq_page_size_param_ ) {
                if ( !HasRelated() ) {
                    q_page_size_ = config_->GetResource(type_pag).PageSize();
                }
            } else {
                if ( rq_page_size_param_ > config_->GetResource(type_pag).PageLimit() ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "page[size] for resource '%s' cannot exceed %u", 
                        config_->GetResource(type_pag).GetType().c_str(), config_->GetResource(type_pag).PageLimit())
                    .SetSourceParam("page[size]=%zd", rq_page_size_param_);
                }
                q_page_size_ = rq_page_size_param_;
            }
            ereport(DEBUG4, (errmsg_internal("jsonapi: %s type_pag:%s q_page_size_:%u rq_page_size_param_:%zd", __FUNCTION__, type_pag.c_str(), q_page_size_, rq_page_size_param_)));

            if ( -1 == rq_page_number_param_ ) {
                if ( q_page_size_ ) {
                    q_page_number_ = 1;
                }
            } else if ( 0 == q_page_size_ ) {
                AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "page[size] is not specified, cannot apply pagination")
                .SetSourceParam("page[number]=%zd", rq_page_number_param_);
            } else {
                q_page_number_ = rq_page_number_param_;
            }
        } else {
            if ( -1 != rq_page_size_param_ ) {
                AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "pagination can only be applied when fetching collections")
                .SetSourceParam("page[size]=%zd", rq_page_size_param_);
            }
            if ( -1 != rq_page_number_param_ ) {
                AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "pagination can only be applied when fetching collections")
                .SetSourceParam("page[number]=%zd", rq_page_number_param_);
            }
        }

        if ( NeedsSearchPath() ) {
            ereport(DEBUG3, (errmsg_internal("jsonapi: old.search_path=%s<< template_search_path=%s new.search_path=%s<<", q_old_search_path_.c_str(), config_->SearchPathTemplate().c_str(), namespace_search_path)));
            std::string set_cmd;

            set_cmd = "SET LOCAL search_path to " + config_->SearchPathTemplate();
            size_t pos = set_cmd.find("request-accounting-schema");
            if ( std::string::npos != pos ) {
                if ( GetRequestAccountingSchema().length() ) {
                    set_cmd.replace(pos, strlen("request-accounting-schema"),GetRequestAccountingSchema());
                } else {
                    set_cmd.replace(pos, strlen("request-accounting-schema"),"public");
                }
            }
            pos = set_cmd.find("request-sharded-schema");
            if ( std::string::npos != pos ) {
                if ( GetRequestShardedSchema().length() ) {
                    set_cmd.replace(pos, strlen("request-sharded-schema"),GetRequestShardedSchema());
                } else {
                    set_cmd.replace(pos, strlen("request-sharded-schema"),"public");
                }
            }
            pos = set_cmd.find("request-company-schema");
            if ( std::string::npos != pos ) {
                if ( GetRequestCompanySchema().length() ) {
                    set_cmd.replace(pos, strlen("request-company-schema"),GetRequestCompanySchema());
                } else {
                    set_cmd.replace(pos, strlen("request-company-schema"),"public");
                }
            }

            if ( ! SPIExecuteCommand(set_cmd, SPI_OK_UTILITY) ) {
                return false;
            }
            q_old_search_path_ = namespace_search_path;
            ereport(DEBUG3, (errmsg_internal("jsonapi: search_path=%s",  namespace_search_path)));
        }
    }

    if ( q_errors_.size() ) {
        return false;
    }

    return true;
}

/**
 * @brief Create an inclusion request to be queried later if the related resource should be included in response.
 */
void pg_jsonapi::QueryBuilder::RequestResourceInclusion(const std::string& a_type, size_t a_depth, const std::string& a_id, const std::string& a_field, const std::string& a_rel_id)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s a_type:%s", __FUNCTION__, a_type.c_str())));

    if (  ( config_->IsCompound() && 0 == rq_include_param_.size() )
        || ( rq_include_param_.count(a_field)
            && (   ( 0 == a_depth && ( ! HasRelated() || IsRelationship() ) )
                || ( 1 == a_depth && HasRelated () && ! IsRelationship()    ) ) )
        || q_data_[a_type].inclusion_path_.count(a_id)
        ) {
        bool needs_inclusion = false;
        const std::string& atttype = config_->GetResource(a_type).GetFieldResourceType(a_field);

        if ( 0 == rq_include_param_.size() ) {
            needs_inclusion = true;
        } else {
            if ( rq_include_param_.count(a_field) ) {
                needs_inclusion = true;
                q_data_[atttype].inclusion_path_[a_rel_id].insert(a_field);
            }
            if ( q_data_[a_type].inclusion_path_.count(a_id)  ) {
                for ( StringSet::iterator tree_parent = q_data_[a_type].inclusion_path_.at(a_id).begin(); tree_parent != q_data_[a_type].inclusion_path_.at(a_id).end(); ++tree_parent ) {
                    std::string path_name = *tree_parent + "." + a_field;
                    if ( rq_include_param_.count(path_name) ) {
                        needs_inclusion = true;
                        q_data_[atttype].inclusion_path_[a_rel_id].insert(path_name);
                    }
                }
            }
        }
        if ( needs_inclusion ) {
            RequestOperationResponseData(atttype,a_rel_id);
        }
    }
    return;
}

/**
 * @brief Clean inclusion requests, removing resources which are already available.
 */
void pg_jsonapi::QueryBuilder::CleanRelationshipInclusion()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    StringSetMapIterator type_it = q_to_be_included_.begin();
    while ( type_it != q_to_be_included_.end() ) {
        const std::string& atttype = type_it->first;
        StringSetIterator id_it = q_to_be_included_[atttype].begin();
        while ( id_it != q_to_be_included_[atttype].end() ) {
            if (   ( q_data_.count(atttype) )
                && ( q_data_[atttype].requested_ids_.count(*id_it) || q_data_[atttype].processed_ids_.count(*id_it) )
                ) {
                StringSetIterator remove_it = id_it;
                id_it++;
                q_to_be_included_[atttype].erase(remove_it);
            } else {
                id_it++;
            }
        }

        if ( 0 == q_to_be_included_[atttype].size() ) {
            StringSetMapIterator remove_it = type_it;
            type_it++;
            q_to_be_included_.erase(remove_it);
        } else {
            type_it++;
        }
    }

    return;
}

/**
 * @brief Process postgresql result for query returning counter.
 *
 * @return @li true if counter is valid
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ProcessCounter (size_t& a_count)
{
    Datum  datum;
    Oid    result_oid;
    bool   is_null;

    result_oid = TupleDescAttr(SPI_tuptable->tupdesc,0)->atttypid;
    if (   1 != SPI_processed
        || 1 != SPI_tuptable->tupdesc->natts
        || (INT8OID != result_oid && INT4OID != result_oid)
        ) {
        return false;
    }
    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if ( is_null ) {
        return false;
    }
    a_count = DatumGetInt64(datum);
    ereport(DEBUG3, (errmsg_internal("a_count() = %zd", a_count)));

    return true;
}

/**
 * @brief Process postgresql result for query returning resource attributes.
 *
 * @return @li true if data is valid
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ProcessAttributes(const std::string& a_type, size_t a_depth, StringSet* a_processed_ids)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s a_type:%s", __FUNCTION__, a_type.c_str())));

    uint32_t              offset = 0;
    bool                  is_null;
    const ResourceConfig& rc = config_->GetResource(a_type);

    if ( q_data_[a_type].processed_ ) {
        offset = q_data_[a_type].processed_;
        q_data_[a_type].processed_ += SPI_processed;
        ereport(DEBUG3, (errmsg_internal("query for resource '%s' had already %u processed rows, total resized to %u", a_type.c_str(), offset, q_data_[a_type].processed_)));

        if ( q_data_[a_type].tupdesc_->natts != SPI_tuptable->tupdesc->natts ) {
            /* sanity check: we are trusting that queries always return same columns per resource */
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "different number of columns was returned for resource '%s'", a_type.c_str());
            return false;
        }
    } else {
        q_data_[a_type].processed_ = SPI_processed;
    }
    q_data_[a_type].tupdesc_  = SPI_tuptable->tupdesc;
    q_data_[a_type].items_.resize(q_data_[a_type].processed_);
    for (uint32 row = offset; row < q_data_[a_type].processed_; row++) {
        q_data_[a_type].items_[row].res_tuple_ = SPI_tuptable->vals[row-offset];
        q_data_[a_type].items_[row].serialized_ = false;
        /* 'id' column should always be the first field returned from query, and we could avoid strcmp loop... */
        for (int col = 1; NULL == q_data_[a_type].items_[row].id_ && col <= q_data_[a_type].tupdesc_->natts; col++) {
            const char* attname = NameStr(TupleDescAttr(q_data_[a_type].tupdesc_,col-1)->attname);
            if ( 0 == strcmp(attname, "id") ) {
                SPI_getbinval(q_data_[a_type].items_[row].res_tuple_, q_data_[a_type].tupdesc_, col, &is_null);
                if ( is_null ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "empty id for '%s'", a_type.c_str());
                    return false;
                }
                q_data_[a_type].items_[row].id_ = SPI_getvalue(q_data_[a_type].items_[row].res_tuple_, q_data_[a_type].tupdesc_, col);
                if ( q_data_[a_type].processed_ids_.count(q_data_[a_type].items_[row].id_) ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "possible duplicate, id '%s' for '%s' was already returned",
                               q_data_[a_type].items_[row].id_, a_type.c_str());
                    return false;
                }
                q_data_[a_type].id_index_[q_data_[a_type].items_[row].id_] = row;
                a_processed_ids->insert( q_data_[a_type].items_[row].id_ );
                q_data_[a_type].processed_ids_.insert( q_data_[a_type].items_[row].id_ );

            }
        }
        if ( NULL == q_data_[a_type].items_[row].id_ || 0 ==strlen(q_data_[a_type].items_[row].id_) ) {
            if ( rc.IdFromRowset() ) {
                q_data_[a_type].items_[row].internal_id_ = std::to_string(row);
                q_data_[a_type].items_[row].id_ = q_data_[a_type].items_[row].internal_id_.c_str();
                if ( q_data_[a_type].processed_ids_.count(q_data_[a_type].items_[row].id_) ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "possible duplicate, id '%s' for '%s' was already returned",
                               q_data_[a_type].items_[row].id_, a_type.c_str());
                    return false;
                }
                q_data_[a_type].id_index_[q_data_[a_type].items_[row].id_] = row;
                a_processed_ids->insert( q_data_[a_type].items_[row].id_ );
                q_data_[a_type].processed_ids_.insert( q_data_[a_type].items_[row].id_ );
            } else {
                AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "empty id for '%s'", a_type.c_str());
                return false;
            }
        }
        for (int col = 1; col <= q_data_[a_type].tupdesc_->natts; col++) {
            const char* attname = NameStr(TupleDescAttr(q_data_[a_type].tupdesc_,col-1)->attname);
            if ( rc.IsRelationship(attname) ) {
                const char* rel_id = SPI_getvalue(q_data_[a_type].items_[row].res_tuple_, q_data_[a_type].tupdesc_, col);
                if ( NULL != rel_id ) {
                    if ( 0 == strlen(rel_id) ) {
                        if ( ! config_->EmptyIsNull() ) {
                            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "empty value of relationship '%s.%s' for parent id='%s'",
                                       a_type.c_str(), attname, q_data_[a_type].items_[row].id_);
                            return false;
                        }
                    } else {
                        q_data_[a_type].items_[row].relationships_[attname].push_back(rel_id);
                        if ( 0 == a_depth && HasRelated() && !IsRelationship() && attname == GetRelated() ) {
                            RequestOperationResponseData(GetRelatedType(), rel_id);
                            q_data_[GetRelatedType()].top_processed_ = 1;
                        } else {
                            RequestResourceInclusion(a_type, a_depth, q_data_[a_type].items_[row].id_, attname, rel_id);
                        }
                    }
                }
            }
        }
    }

    return true;
}

/**
 * @brief Process postgresql result for query returning resource relationships.
 *
 * @return @li true if data is valid
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ProcessRelationships(const std::string& a_type, size_t a_depth)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s a_type:%s", __FUNCTION__, a_type.c_str())));

    const char* attname = NameStr(TupleDescAttr(SPI_tuptable->tupdesc,1)->attname);
    bool top_related = ( 0 == a_depth && HasRelated() && !IsRelationship() && attname == GetRelated() );

    if (   2 != SPI_tuptable->tupdesc->natts
        || strcmp(NameStr(TupleDescAttr(SPI_tuptable->tupdesc,0)->attname), "id")
        || ! config_->GetResource(a_type).IsRelationship(attname) ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid query column '%s' returned for resource '%s'", attname, a_type.c_str());
        return false;
    }

    for (uint32 inner_row = 0; inner_row < SPI_processed; inner_row++) {
        const char* id = SPI_getvalue(SPI_tuptable->vals[inner_row], SPI_tuptable->tupdesc, 1);
        const char* rel_id = SPI_getvalue(SPI_tuptable->vals[inner_row], SPI_tuptable->tupdesc, 2);
        if ( NULL == id || 0 == strlen(id)) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "empty value of parent while getting relationship '%s.%s'", a_type.c_str(), attname);
            return false;
        } else if ( NULL != rel_id ) {
            if ( 0 == strlen(rel_id) ) {
                if ( ! config_->EmptyIsNull() ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "empty value of relationship '%s.%s' for parent id='%s'",
                               a_type.c_str(), attname, id);
                    return false;
                }
            } else {
                uint32      row = 0;
                while (row < q_data_[a_type].processed_ && strcmp(id,q_data_[a_type].items_[row].id_) ) {
                    row++;
                }
                if ( row == q_data_[a_type].processed_ ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "got relationship '%s.%s' for unexpected id '%s'", a_type.c_str(), attname, id);
                    return false;
                }
                if ( q_data_[a_type].items_[row].relationships_.count(attname) > 0  ) {
                    for ( StringVector::const_iterator other_rel_id = q_data_[a_type].items_[row].relationships_.at(attname).begin(); other_rel_id != q_data_[a_type].items_[row].relationships_.at(attname).end(); ++other_rel_id ) {
                        if ( *other_rel_id == rel_id) {
                            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "duplicate value '%s' of relationship '%s.%s' for parent id='%s'",
                                        rel_id, a_type.c_str(), attname, id);
                            return false;
                        }
                    }
                    if ( config_->GetResource(a_type).IsToOneRelationship(attname) ) {
                        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "obtained id=%s for to-one relationship '%s.%s' that already had id=%s for parent id='%s'",
                                    rel_id, a_type.c_str(), attname, q_data_[a_type].items_[row].relationships_[attname][0].c_str() ,id);
                    }
                }
                q_data_[a_type].items_[row].relationships_[attname].push_back(rel_id);
                if ( top_related ) {
                    RequestOperationResponseData(GetRelatedType(), rel_id);
                } else {
                    RequestResourceInclusion(a_type, a_depth, q_data_[a_type].items_[row].id_, attname, rel_id);
                }
            }
        }
    }

    return true;
}

/**
 * @brief Connect to SPI and start a sub-transaction.
 *
 * @return @li true if connection succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::SPIConnect() {
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    int ret = SPI_connect();
    if ( SPI_OK_CONNECT != ret ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA005"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "SPI_connect error: %s.", SPI_result_code_string(ret));
        return false;
    } else {
        spi_connected_ = true;
        BeginInternalSubTransaction(NULL);
        return true;
    }
}

/**
 * @brief Disconnect from SPI and release the sub-transaction, doing a rollback in case any error occurred.
 *
 * @return @li true if disconnection succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::SPIDisconnect() {
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( spi_connected_ ) {

        if ( ! spi_read_only_ && ! HasErrors() ) {
            ReleaseCurrentSubTransaction();
            ereport(DEBUG3, (errmsg_internal("jsonapi: released current subtransaction")));
        } else {
            RollbackAndReleaseCurrentSubTransaction();
            ereport(DEBUG3, (errmsg_internal("jsonapi: rolled back and released current subtransaction")));
        }

        int ret = SPI_finish();
        spi_connected_ = false;
        if ( SPI_OK_FINISH != ret && SPI_ERROR_UNCONNECTED != ret ) {
            ereport(LOG, (errmsg_internal("jsonapi: SPI_finish failed: %s", SPI_result_code_string(ret))));
            return false;
        }
    }
    return true;
}

/**
 * @brief Execute a command in postgresql using SPI.
 *
 * @param a_command The command to be executed.
 * @a_expected_ret The expected return code.
 *
 * @return @li true if execution succeeds
 *         @li false if an error or exception occurs
 */
bool pg_jsonapi::QueryBuilder::SPIExecuteCommand (const std::string& a_command, const int a_expected_ret)
{
    ereport(DEBUG2, (errmsg_internal("jsonapi: %s command: %s", __FUNCTION__, a_command.c_str() )));

    bool rv = true;
    MemoryContext  curContext = CurrentMemoryContext;
    int ret = 0;

    if ( HasErrors() ) {
        return false;
    }

    PG_TRY();
    {
        ret = SPI_execute(a_command.c_str(), (spi_read_only_ && SPI_OK_UTILITY != a_expected_ret), 0);
        ereport(DEBUG3, (errmsg_internal("jsonapi: %s SPI_processed=%d", __FUNCTION__, (int)SPI_processed)));
        if ( ret < 0 || ret != a_expected_ret ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA005"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "%s", SPI_result_code_string(ret));
            rv = false;
        }
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo( curContext );
        ErrorData *errdata = CopyErrorData();
        if ( JSONAPI_ERRCODE_CATEGORY == ERRCODE_TO_CATEGORY(errdata->sqlerrcode) )
        {
            // if JSONAPI error code category is being used, we can trust that message must be sent to user
            AddError(errdata->sqlerrcode, errcodes_.GetStatus(errdata->sqlerrcode)).SetMessage(errdata->message, NULL);
        } else {
            // if another error category is being used, then use default status and message for the error code and send postgres message in internal meta
            ErrorCode::ErrorCodeDetail ecd = errcodes_.GetDetail(errdata->sqlerrcode);
            AddError(errdata->sqlerrcode, ecd.status_).SetMessage(ecd.message_, "ERROR:[ %s ] DETAIL:[ %s ] HINT:[ %s ] CONTEXT:[ %s ]", errdata->message,  errdata->detail,  errdata->hint,  errdata->context);
        }
        FreeErrorData(errdata);

        FlushErrorState();
        SPIDisconnect();
        SPI_restore_connection();

        rv = false;
    }
    PG_END_TRY();

    return rv;
}

void pg_jsonapi::QueryBuilder::AddInClause (const std::string& a_column, StringSet a_values)
{
    if ( a_values.size() ) {
        const char* comma = "";
        q_buffer_ += a_column + " IN (";
        for ( StringSet::const_iterator val = a_values.begin(); val != a_values.end(); ++val ) {
            q_buffer_ += comma;
            OperationRequest::AddQuotedStringToBuffer(q_buffer_, (*val).c_str(), /*quote*/ true, /*no_html*/ false);
            comma = ",";
        }
        q_buffer_ += ")";
    }
}

/**
 * @brief GetTopQuery
 */
const std::string& pg_jsonapi::QueryBuilder::GetTopQuery (bool a_count_rows, bool a_apply_filters)
{
    const ResourceConfig& rc = config_->GetResource(GetResourceType());
    std::string condition_start = "";
    std::string condition_separator = "";
    std::string condition_operator = "";

    q_buffer_.clear();
    q_buffer_ = "SELECT ";

    if ( a_count_rows ) {
        if ( rc.FunctionSupportsCountColumn() ) {
            q_buffer_ += rc.GetPGFunctionCountColumn();
        } else {
            q_buffer_ += " COUNT( ";
            q_buffer_ += rc.GetPGQueryColId();
            q_buffer_ += " ) ";
        }
    } else {
        q_buffer_ += rc.GetPGQueryColumns();
    }

    q_buffer_ += " FROM ";

    rc.AddPGQueryFromItem(q_buffer_);
    if ( rc.IsQueryFromAttributesFunction() ) {
        q_buffer_ += ", " + rc.GetPGQueryAttributesFunction() + "(";
        rc.AddPGQueryFromItem(q_buffer_);
        q_buffer_ += ".*) pgf ";
    }
    if ( rc.IsQueryFromFunction() )
    {
        q_buffer_ +=  "(";
        condition_separator = ",";
        condition_operator = " := ";
    } else {
        condition_start = " WHERE ";
        condition_separator = " AND ";
        condition_operator = " = ";
        /* company column */
        if ( rc.GetPGQueryCompanyColumn().size() ) {
            q_buffer_ += condition_start + "\"" + rc.GetPGQueryCompanyColumn() + "\" = " + GetRequestCompany();
            condition_start = condition_separator;
        }
    }

    /* query condition */
    if ( rc.GetPGQueryCondition().size() ) {
        q_buffer_ += condition_start + rc.GetPGQueryCondition();
        condition_start = condition_separator;
    }

    if ( a_apply_filters ) {
        if ( ! rq_filter_param_.empty() ) {

            if ( '=' == rq_filter_param_[rq_filter_param_.length()-1] ) {
                q_buffer_ += condition_start + rq_filter_param_.substr(0,rq_filter_param_.length()-1) + " IS NULL";
            } else {
                if ( rc.IsQueryFromFunction() ) {
                    q_buffer_ += condition_start + rc.GetPGFunctionArgFilter() + condition_operator;
                    OperationRequest::AddQuotedStringToBuffer(q_buffer_, rq_filter_param_.c_str(), /*quote*/ true, /*no_html*/ true);
                } else {
                    q_buffer_ += condition_start + "(" + rq_filter_param_ + ")";
                }
            }
            condition_start = condition_separator;
        }
    }

    for ( StringMap::iterator res = rq_filter_field_param_.begin(); res != rq_filter_field_param_.end(); ++res ) {
        if ( a_apply_filters || (    rc.IsQueryFromFunction()
                                 && (   rc.GetPGFunctionArgAccountingSchema() == res->first
                                     || rc.GetPGFunctionArgAccountingPrefix() == res->first
                                     || rc.GetPGFunctionArgCompanySchema() == res->first
                                     || rc.GetPGFunctionArgShardedSchema() == res->first
                                     || rc.GetPGFunctionArgCompany() == res->first
                                     || rc.GetPGFunctionArgUser() == res->first
                                    )
                                 ) ) {
            q_buffer_ += condition_start;
            if ( rc.IsQueryFromFunction() ) {
                q_buffer_ += rc.GetPGQueryColumn(res->first);
                if ( res->second.empty() ) {
                    q_buffer_ += " := NULL";
                } else {
                    q_buffer_ += condition_operator;
                    OperationRequest::AddQuotedStringToBuffer(q_buffer_, res->second.c_str(), /*quote*/ true, /*no_html*/ true);
                }
            } else {
                GetFilterTableByFieldCondition (rc.GetType(), res->first, res->second);
            }
            condition_start = condition_separator;
        }
    }

    if ( IsIndividual() ) {
        q_buffer_ += condition_start + rc.GetPGFunctionArgColId() + condition_operator;
        OperationRequest::AddQuotedStringToBuffer(q_buffer_, GetResourceId().c_str(), /*quote*/ true, /*no_html*/ false);
    } else {
        q_required_count_ = 0;
    }

    if ( ( false == a_count_rows && IsCollection() ) || ( TopFunctionReturnsJson() && rc.FunctionSupportsOrder() ) ) {
        if ( !rc.IsQueryFromFunction() || rc.FunctionSupportsOrder() ) {
            std::string sort_start = "";
            if ( rc.IsQueryFromFunction() ) {
                sort_start = condition_start + rc.GetPGFunctionArgOrder() + condition_operator + "'ORDER BY ";
            } else {
                sort_start = " ORDER BY ";
            }
            if ( ! rq_sort_param_.empty() ) {
                for ( std::vector< std::pair <std::string,std::string> >::iterator res = rq_sort_param_.begin(); res != rq_sort_param_.end(); ++res ) {
                    q_buffer_ += sort_start + rc.GetPGQueryCastedColumn(std::get<0>(*res)) + " " + std::get<1>(*res);
                    sort_start = ",";
                }
                if ( rc.IsQueryFromFunction() ) {
                    q_buffer_ += "'";
                    condition_start = condition_separator;
                }
            } else if ( ! rc.GetPGQueryOrder().empty() ) {
                q_buffer_ += sort_start + rc.GetPGQueryOrder();
                if ( rc.IsQueryFromFunction() ) {
                    q_buffer_ += "'";
                    condition_start = condition_separator;
                }
            }
        }

        if ( q_page_size_ || ( GetResourceType().length() && IsCollection() ) ) {
            char offset_buffer[32];
            char limit_buffer[32];

            if ( q_page_size_ ) {
                snprintf(offset_buffer, sizeof(offset_buffer), "%u", (q_page_number_-1) * q_page_size_ );
                snprintf(limit_buffer, sizeof(limit_buffer), "%u", q_page_size_ );
            } else {
                strcpy(offset_buffer, "0");
                snprintf(limit_buffer, sizeof(limit_buffer), "%u", config_->GetResource(GetResourceType()).PageLimit()+1 );
            }

            if ( ! rc.IsQueryFromFunction() ) {
                q_buffer_ += " OFFSET ";
                q_buffer_ += offset_buffer;

                q_buffer_ += " LIMIT ";
                q_buffer_ += limit_buffer;
            } else if ( rc.FunctionSupportsPagination() ) {
                q_buffer_ += condition_start + rc.GetPGFunctionArgPageOffset() + condition_operator + offset_buffer;
                condition_start = condition_separator;
                q_buffer_ += condition_start + rc.GetPGFunctionArgPageLimit() + condition_operator + limit_buffer;
            }
        }
    }
    if ( rc.IsQueryFromFunction() ) {
        if ( rc.FunctionSupportsCounts() ) {
            q_buffer_ += condition_start + rc.GetPGFunctionArgCount() + condition_operator + (a_count_rows? "TRUE" : "FALSE");
        }
        q_buffer_ += ")";
    }

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief Obtain a postgresql command to SELECT relationships.
 *
 * @param a_type The resource type.
 * @param a_rel The related field.
 * @param a_parent_ids The ids of the resources for which relationships are being requested.
 */
const std::string& pg_jsonapi::QueryBuilder::GetRelationshipQuery (const std::string& a_type, const std::string& a_rel, const StringSet& a_parent_ids)
{
    const ResourceConfig& rc = config_->GetResource(a_type);

    q_buffer_.clear();
    q_buffer_ = "SELECT " + rc.GetPGRelationQueryColumns(a_rel);

    q_buffer_ += " FROM ";
    rc.AddPGRelationQueryFromItem(q_buffer_, a_rel);

    /* query condition */
    std::string condition_start = " WHERE ";
    if ( rc.GetPGRelationQueryCondition(a_rel).size() ) {
        q_buffer_ += condition_start + rc.GetPGRelationQueryCondition(a_rel);
        condition_start = " AND ";
    }

    q_buffer_ += condition_start;
    AddInClause(rc.GetPGRelationQueryColParentId(a_rel), a_parent_ids);

    if ( ! rc.GetPGRelationQueryOrder(a_rel).empty() ) {
        q_buffer_ += " ORDER BY " + rc.GetPGRelationQueryOrder(a_rel);
    }

    q_required_count_ = 0;
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief GetInclusionQuery
 */
const std::string& pg_jsonapi::QueryBuilder::GetInclusionQuery (const std::string& a_type)
{
    const ResourceConfig& rc = config_->GetResource(a_type);
    char offset_buffer[32];
    char limit_buffer[32];

    q_required_count_ = q_to_be_included_[a_type].size();

    // prepare query
    q_buffer_.clear();
    q_buffer_ = "SELECT " + rc.GetPGQueryColumns();
    q_buffer_ += " FROM ";
    rc.AddPGQueryFromItem(q_buffer_);
    if ( rc.IsQueryFromAttributesFunction() ) {
        q_buffer_ += ", " + rc.GetPGQueryAttributesFunction() + "(";
        rc.AddPGQueryFromItem(q_buffer_);
        q_buffer_ += ".*) pgf ";
    }

    //#warning TODO: apply filter
    /* query condition */
    std::string condition_start = " WHERE ";
    if ( rc.GetPGQueryCondition().size() ) {
        q_buffer_ += condition_start + rc.GetPGQueryCondition();
        condition_start = " AND ";
    }

    q_buffer_ += condition_start;
    AddInClause(rc.GetPGQueryColId(), q_to_be_included_[a_type] );

    if ( ! rc.GetPGQueryOrder().empty() ) {
        q_buffer_ += " ORDER BY " + rc.GetPGQueryOrder();
    }

    ereport(DEBUG4, (errmsg_internal("jsonapi: %s a_type:%s rc.PageLimit:%u q_page_size_:%u rq_page_size_param_:%zd", __FUNCTION__, a_type.c_str(), rc.PageLimit(), q_page_size_, rq_page_size_param_)));
    if ( HasRelated() && !IsRelationship() && a_type == config_->GetResource(GetResourceType()).GetFieldResourceType(GetRelated()) ) {
        if ( -1 != rq_page_size_param_ ) {
            // only use pagination on related resource request if pagination was explicitly requested
            snprintf(offset_buffer, sizeof(offset_buffer), "%u", (q_page_number_-1) * q_page_size_ );
            snprintf(limit_buffer, sizeof(limit_buffer), "%u", q_page_size_ );
        } else {
            strcpy(offset_buffer, "0");
            snprintf(limit_buffer, sizeof(limit_buffer), "%u", rc.PageLimit()+1 );
        }

        q_buffer_ += " OFFSET ";
        q_buffer_ += offset_buffer;

        q_buffer_ += " LIMIT ";
        q_buffer_ += limit_buffer;

        q_required_count_ = 0;
    } else {
        snprintf(limit_buffer, sizeof(limit_buffer), "%u", rc.PageLimit()+1 );
        q_buffer_ += " LIMIT ";
        q_buffer_ += limit_buffer;
    }

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief Process postgresql result for executed command.
 *
 * @return @li true if data is valid
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ProcessQueryResult(const std::string& a_type, size_t a_depth)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s a_type:%s a_depth:%zd", __FUNCTION__, a_type.c_str(), a_depth)));

    StringSet new_ids;

    if ( SPI_processed > config_->GetResource(a_type).PageLimit() ) {
        if ( 0 == a_depth ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA019"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "too many values returned for resource '%s', specify a page size that will not exceed %u results", a_type.c_str(), config_->GetResource(a_type).PageLimit());
        } else {
            AddError(JSONAPI_MAKE_SQLSTATE("JA020"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "too many values returned for resource '%s', specify a page size that will not exceed %u results", a_type.c_str(), config_->GetResource(a_type).PageLimit());
        }
        return false;
    }

    if ( q_required_count_ && q_required_count_ != SPI_processed ) {
        if( 0 == SPI_processed ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA015"), E_HTTP_NOT_FOUND)
            .SetMessage(NULL,
                                "expected %zu item%s of resource '%s', statement: %s",
                                q_required_count_, q_required_count_>1? "s":"", a_type.c_str(), q_buffer_.c_str());
        } else {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR)
            .SetMessage(NULL,
                                "expected %zu item%s of resource '%s', statement: %s",
                                q_required_count_, q_required_count_>1? "s":"", a_type.c_str(), q_buffer_.c_str());
        }

        return false;
    }

    if ( HasRelated() && !IsRelationship() && a_type == config_->GetResource(GetResourceType()).GetFieldResourceType(GetRelated()) ) {
        q_data_[a_type].top_processed_ = SPI_processed;
    }

    if ( 0 == SPI_processed ) {
        return true;
    }

    if ( ! ProcessAttributes(a_type, a_depth, &new_ids) ) {
        return false;
    }

    if ( 0 == a_depth || config_->IsCompound() || rq_include_param_.size() ) {
        /* when is compound or if any relation was requested than we need to get all relations,
         * this is needed because even if a resource is excluded from response due to sparse fields
         * or was not requested to be included we need to obey to full linkage rules
         */
        for ( ResourceConfig::PGRelationSpecMap::const_iterator pg_rel = config_->GetResource(a_type).GetPGRelations().begin(); pg_rel != config_->GetResource(a_type).GetPGRelations().end(); ++pg_rel ) {
            if ( IsRequestedField(a_type, pg_rel->first) || config_->IsCompound() || rq_include_param_.size() ) {
                if ( ! SPIExecuteCommand(GetRelationshipQuery(a_type, pg_rel->first, new_ids).c_str(), SPI_OK_SELECT) ) {
                    return false;
                }
                if ( ! ProcessRelationships(a_type, a_depth) ) {
                    return false;
                }
            }
        }
    }

    CleanRelationshipInclusion();
    StringSetMapIterator it = q_to_be_included_.begin();
    while ( q_to_be_included_.end() != it ) {
        std::string type = it->first;
        if ( ! SPIExecuteCommand(GetInclusionQuery(type).c_str(), SPI_OK_SELECT) ) {
            return false;
        }
        q_data_[type].requested_ids_.insert(it->second.begin(),it->second.end());
        q_to_be_included_.erase(it);
        if ( ! ProcessQueryResult(type, a_depth+1) ) {
            return false;
        }
        it = q_to_be_included_.begin();
    }

    return true;
}

/**
 * @brief Obtain data to respond to a GET request.
 *
 * @return @li true if data was successfully requested and processed
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::FetchData()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( 1 == rq_totals_param_ && ( !IsTopQueryFromFunction() || TopFunctionSupportsCounts() ) ) {
        /* count items to be returned from main query using filters */
        if ( ! SPIExecuteCommand(GetTopQuery(true, true).c_str(), SPI_OK_SELECT) ) {
            return false;
        }
        if ( ! ProcessCounter(q_top_total_rows_) ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "could not count total rows for resource '%s'", GetResourceType().c_str());
            return false;
        }
        if ( rq_filter_param_.empty() && rq_filter_field_param_.empty() ) {
            q_top_grand_total_rows_ = q_top_total_rows_;
        } else {
            /* count items to be returned from main query if there where NO filters */
            if ( ! SPIExecuteCommand(GetTopQuery(true, false).c_str(), SPI_OK_SELECT) ) {
                return false;
            }
            if ( ! ProcessCounter(q_top_grand_total_rows_) ) {
                AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "grand total rows for resource '%s'", GetResourceType().c_str());
                return false;
            }
        }
    }
    if ( IsTopQueryFromFunction() && !TopFunctionSupportsFilter() ) {
        spi_read_only_ = false;
    }

    /* execute the main query as read-only */
    if ( ! SPIExecuteCommand(GetTopQuery().c_str(), SPI_OK_SELECT) ) {
        return false;
    }

    q_data_[GetResourceType()].top_processed_ = SPI_processed;

    if ( TopFunctionReturnsJson() ) {
        if ( ! ProcessFunctionJsonResult(GetResourceType()) ) {
            return false;
        }
    } else {
        if ( IsIndividual() ) {
            q_required_count_ = 1;
        }

        if ( ! ProcessQueryResult(GetResourceType(), 0) ) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Execute all requested operations in write mode, obtain id of new resources
 *        and fetch data to be used on response.
 *
 * @return @li true if data was successfully requested and processed
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ExecuteOperations()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
    spi_read_only_ = false;

    if ( 0 == rq_operations_.size() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA004"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "no operations to be executed");
    }

    for ( int i = 0; i < (int)rq_operations_.size(); i++ ) {
        rq_operations_[i].InitObservedStat();
        switch (rq_operations_[i].GetType()) {
            case E_OP_CREATE:
                if ( ! SPIExecuteCommand(rq_operations_[i].GetInsertCmd().c_str(), SPI_OK_INSERT_RETURNING) ) {
                    return false;
                }
                q_data_[rq_operations_[i].GetResourceType()].top_processed_ += SPI_processed;
                break;
            case E_OP_UPDATE:
                if ( ! SPIExecuteCommand(rq_operations_[i].GetUpdateCmd().c_str(), SPI_OK_UPDATE_RETURNING) ) {
                    return false;
                }
                q_data_[rq_operations_[i].GetResourceType()].top_processed_ += SPI_processed;
                break;
            case E_OP_DELETE:
                if ( ! SPIExecuteCommand(rq_operations_[i].GetDeleteCmd().c_str(), SPI_OK_DELETE_RETURNING) ) {
                    return false;
                }
                break;
            case E_OP_UNDEFINED:
                AddError(JSONAPI_MAKE_SQLSTATE("JA004"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "operations is undefined");
                return false;
        }

        if ( ! rq_operations_[i].ProcessOperationResult() ) {
            return false;
        }
    }

    /* get resources after all operations were executed */
    StringSetMapIterator it = q_to_be_included_.begin();
    while ( q_to_be_included_.end() != it ) {
        std::string type = it->first;
        if ( ! SPIExecuteCommand(GetInclusionQuery(type).c_str(), SPI_OK_SELECT) ) {
            return false;
        }
        q_data_[type].requested_ids_.insert(it->second.begin(),it->second.end());
        q_to_be_included_.erase(it);
        if ( ! ProcessQueryResult(type, 1) ) {
            return false;
        }
        it = q_to_be_included_.begin();
    }
    return true;
}

/**
 * @brief Serialize relationship data into jsonapi format.
 */
void pg_jsonapi::QueryBuilder::SerializeRelationshipData (StringInfoData& a_response, const std::string& a_type, const std::string& a_field, const ResourceData& a_rd, uint32 a_row) const
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( a_rd.items_[a_row].relationships_.count(a_field) ) {
        const char* rel_start = config_->GetResource(a_type).IsToManyRelationship(a_field) ? "[" : "";
        for ( StringVector::const_iterator rel_id = a_rd.items_[a_row].relationships_.at(a_field).begin(); rel_id != a_rd.items_[a_row].relationships_.at(a_field).end(); ++rel_id ) {
            appendStringInfo(&a_response, "%s{\"type\":\"%s\",\"id\":\"%s\"}",
                             rel_start, config_->GetResource(a_type).GetFieldResourceType(a_field).c_str(), rel_id->c_str());
            rel_start = ",";
        }
        if ( config_->GetResource(a_type).IsToManyRelationship(a_field) ) {
            appendStringInfoChar(&a_response, ']');
        }
    } else {
        if ( config_->GetResource(a_type).IsToOneRelationship(a_field) ) {
            appendStringInfoString(&a_response, "null");
        } else {
            appendStringInfoString(&a_response, "[]");
        }
    }

    if ( 1 == rq_links_param_ || (-1 == rq_links_param_ && config_->GetResource(a_type).ShowLinks(a_field)) ) {
        appendStringInfo(&a_response, ",\"links\":{\"self\":\"%s/%s/%s/relationships/%s\",\"related\":\"%s/%s/%s/%s\"}",
                         rq_base_url_.c_str(), a_type.c_str(), a_rd.items_[a_row].id_, a_field.c_str(),
                         rq_base_url_.c_str(), a_type.c_str(), a_rd.items_[a_row].id_, a_field.c_str());
    }

    return;
}

/**
 * @brief Serialize one resource data into jsonapi format.
 */
void pg_jsonapi::QueryBuilder::SerializeResource (StringInfoData& a_response, const std::string& a_type, ResourceData& a_rd, uint32 a_row) const
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    TupleDesc   res_tupdesc = a_rd.tupdesc_;
    const char* res_id      = a_rd.items_[a_row].id_;
    HeapTuple   res_tuple   = a_rd.items_[a_row].res_tuple_;

    a_rd.items_[a_row].serialized_ = true;

    /* serialize resource with type and id */
    appendStringInfo(&a_response, "{\"type\":\"%s\",\"id\":\"%s\"", a_type.c_str(), res_id);

    /* serialize attributes */
    const char* field_start = ",\"attributes\":{";

    for (int col = 1; col <= res_tupdesc->natts; col++) {
        const char* attname = NameStr(TupleDescAttr(res_tupdesc,col-1)->attname);
        bool  is_null;
        Datum datum;
        TYPCATEGORY attcat = TYPCATEGORY_UNKNOWN;

        ereport(DEBUG4, (errmsg_internal("jsonapi: %s resource:%s attname:%s type:%s oid:%d category:%c", __FUNCTION__, a_type.c_str(), attname, SPI_gettype(res_tupdesc,col), TupleDescAttr(res_tupdesc,col-1)->atttypid, TypeCategory(TupleDescAttr(res_tupdesc,col-1)->atttypid) )));
        if ( config_->GetResource(a_type).IsValidAttribute(attname) && IsRequestedField(a_type, attname) ) {
            datum = SPI_getbinval(res_tuple, res_tupdesc, col, &is_null);
            if ( is_null ) {
                if ( 1 == rq_null_param_ || (-1 == rq_null_param_ && config_->GetResource(a_type).ShowNull()) ) {
                    appendStringInfo(&a_response, "%s\"%s\":null", field_start, attname);
                    field_start = ",";
                }
            } else {
                appendStringInfo(&a_response, "%s\"%s\":", field_start, attname);
                field_start = ",";
                switch (TupleDescAttr(res_tupdesc,col-1)->atttypid) {

                    case CHAROID:
                        appendStringInfo(&a_response, "%c", DatumGetChar(datum));
                        break;

                    case VARCHAROID:
                    case TEXTOID:
                    case DATEOID:
                    case TIMESTAMPOID:
                    case TIMESTAMPTZOID:
                    case XMLOID:
                        escape_json(&a_response, SPI_getvalue(res_tuple, res_tupdesc, col));
                        break;

                    case INT2OID:
                        appendStringInfo(&a_response, "%hd", DatumGetInt16(datum));
                        break;

                    case INT4OID:
                        appendStringInfo(&a_response, "%d", DatumGetInt32(datum));
                        break;

                    case INT8OID:
                        appendStringInfo(&a_response, "%ld", DatumGetInt64(datum));
                        break;

                    case FLOAT4OID:
                        appendStringInfo(&a_response, "%f", DatumGetFloat4(datum));
                        break;

                    case FLOAT8OID:
                        appendStringInfo(&a_response, "%lf", DatumGetFloat8(datum));
                        break;

                    case BOOLOID:
                        appendStringInfo(&a_response, "%s", DatumGetFloat8(datum) ? "true" : "false");
                        break;

                    case JSONOID:
                    case JSONBOID:
                            ereport(DEBUG2, (errmsg_internal("jsonapi: %s *** JSON *** resource:%s attname:%s type:%s oid:%d category:%c", __FUNCTION__, a_type.c_str(), attname, SPI_gettype(res_tupdesc,col), TupleDescAttr(res_tupdesc,col-1)->atttypid, TypeCategory(TupleDescAttr(res_tupdesc,col-1)->atttypid) )));
                    case NUMERICOID:
                        appendStringInfo(&a_response, "%s", SPI_getvalue(res_tuple, res_tupdesc, col));
                        break;

                    default:
                        attcat = TypeCategory(TupleDescAttr(res_tupdesc,col-1)->atttypid);
                        if ( TYPCATEGORY_ARRAY == attcat ) {
                            /* convert arrays using to_json */
                            pg_jsonapi::array_to_json_internal(datum, &a_response, false);
                        } else if ( TYPCATEGORY_ENUM == attcat ) {
                            /* convert enumerations as text */
                            escape_json(&a_response, SPI_getvalue(res_tuple, res_tupdesc, col));
                        } else {
                            ereport(WARNING, (errmsg_internal("jsonapi: %s resource:%s attname:%s type:%s oid:%d category:%c", __FUNCTION__, a_type.c_str(), attname, SPI_gettype(res_tupdesc,col), TupleDescAttr(res_tupdesc,col-1)->atttypid, TypeCategory(TupleDescAttr(res_tupdesc,col-1)->atttypid) )));
                            escape_json(&a_response, SPI_getvalue(res_tuple, res_tupdesc, col));
                        }
                        break;
                }
            }
        }
    }
    if ( 1 == strlen(field_start) ) {
        appendStringInfoChar(&a_response, '}'); // attributes end
    }

    /* serialize relationships */
    field_start = ",\"relationships\":{";
    for ( ResourceConfig::RelationshipMap::const_iterator rel = config_->GetResource(a_type).GetRelationships().begin(); rel != config_->GetResource(a_type).GetRelationships().end(); ++rel ) {
        ereport(DEBUG3, (errmsg_internal("jsonapi: %s res=%s rel=%s", __FUNCTION__, a_type.c_str(), rel->first.c_str())));

        if (   IsRequestedField(a_type, rel->first)
            && ( a_rd.items_[a_row].relationships_.count(rel->first) > 0
                || ( 1 == rq_null_param_ || (-1 == rq_null_param_ && config_->GetResource(a_type).ShowNull()) )
                ) ) {
                appendStringInfo(&a_response, "%s\"%s\":{\"data\":", field_start, rel->first.c_str());
                SerializeRelationshipData(a_response, a_type, rel->first, a_rd, a_row);
                appendStringInfoChar(&a_response, '}');
                field_start = ",";
            }
    }
    if ( 1 == strlen(field_start) ) {
        appendStringInfoChar(&a_response, '}'); // relationships end
    }

    if ( 1 == rq_links_param_ || (-1 == rq_links_param_ && config_->GetResource(a_type).ShowLinks()) ) {
        /* serialize links */
        appendStringInfo(&a_response, ",\"links\":{\"self\":\"%s/%s/%s\"}",
                         rq_base_url_.c_str(), a_type.c_str(), res_id);
    }

    /* resource end */
    appendStringInfoChar(&a_response, '}');
}

/**
 * @brief Serialize common error items.
 */
void pg_jsonapi::QueryBuilder::SerializeCommonErrorItems (StringInfoData& a_response)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    bool needs_comma = false;
    for ( ErrorVector::const_iterator error = q_errors_.begin(); error != q_errors_.end(); ++error ) {
        if ( E_EXT_JSON_PATCH != rq_extension_ || ! error->IsOperation() ) {
            if ( needs_comma ) {
                appendStringInfoChar(&a_response, ',');
            }
            error->Serialize(a_response);
            needs_comma = true;
        }
    }
}

/**
 * @brief Serialize top-level errors.
 */
void pg_jsonapi::QueryBuilder::SerializeErrors (StringInfoData& a_response)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    appendStringInfoString(&a_response, "\"errors\":[");
    SerializeCommonErrorItems(a_response);
    appendStringInfoChar(&a_response, ']');
}

/**
 * @brief Serialize top-level response.
 */
void pg_jsonapi::QueryBuilder::SerializeResponse (StringInfoData& a_response)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( NeedsSearchPath() && q_old_search_path_.size() ) {
        std::string set_cmd;
        set_cmd += "SET LOCAL search_path to ";
        set_cmd += q_old_search_path_;
        SPIExecuteCommand(set_cmd.c_str(), SPI_OK_UTILITY);
    }

    if ( "GET" == rq_method_ || E_EXT_NONE == rq_extension_ || E_EXT_BULK == rq_extension_ ) {
        appendStringInfoChar(&a_response, '{');
        if ( q_errors_.size() ) {
            SerializeErrors(a_response);
        } else if ( IsTopQueryFromJobTube() ) {
            q_http_status_ = E_HTTP_ACCEPTED;
            appendStringInfo(&a_response, "\"meta\":{\"job-tube\":\"%s\"", config_->GetResource(GetResourceType()).GetJobTube().c_str() );
            if ( config_->GetResource(GetResourceType()).JobTtr() > 0 ) {
                appendStringInfo(&a_response, ",\"job-ttr\":%zu", config_->GetResource(GetResourceType()).JobTtr() );
            }
            if ( config_->GetResource(GetResourceType()).JobValidity() > 0 ) {
                appendStringInfo(&a_response, ",\"job-validity\":%zu", config_->GetResource(GetResourceType()).JobValidity() );
            }
            appendStringInfoChar(&a_response, '}');
        } else if ( "DELETE" == rq_method_ || (E_EXT_JSON_PATCH == rq_extension_ && E_OP_DELETE == rq_operations_[0].GetType() ) ) {
            rq_operations_[0].SerializeMeta(a_response, true);
        } else {
            if ( rq_operations_.size() && rq_operations_[0].SerializeMeta(a_response, false) && 1 != rq_totals_param_ ) {
                appendStringInfoChar(&a_response, ',');
            }
            appendStringInfo(&a_response, "\"data\":");

            if ( "GET" == rq_method_ || E_EXT_BULK == rq_extension_ ) {
                if ( TopFunctionReturnsJson() ) {
                    appendStringInfoString(&a_response, q_json_function_data_);
                    if ( NULL != q_json_function_included_ ) {
                        appendStringInfo(&a_response, ",\"included\":%s",q_json_function_included_);
                    }
                }
                else if ( IsRelationship() ) {
                    SerializeRelationshipData(a_response, GetResourceType(), GetRelated(), q_data_[GetResourceType()], 0);
                    SerializeIncluded(a_response);
                } else {
                    SerializeFetchData(a_response);
                }
                if ( 1 == rq_totals_param_ ) {
                    appendStringInfo(&a_response, ",\"meta\":{");
                    if ( rq_operations_.size() && rq_operations_[0].SerializeObservedInMeta(a_response) ) {
                        appendStringInfoChar(&a_response, ',');
                    }
                    appendStringInfo(&a_response, "\"total\":\"%zd\",\"grand-total\":\"%zd\"}", q_top_total_rows_, q_top_grand_total_rows_);
                }
                if ( ( 1 == rq_links_param_ || (-1 == rq_links_param_ && config_ && config_->ShowLinks()) ) && ( !IsRelationship() || q_errors_.size() ) ) {
                    appendStringInfo(&a_response, ",\"links\":{\"self\":\"%s\"}", rq_url_encoded_.c_str());
                }
            } else {
                if ( rq_operations_[0].IsRelationship() ) {
                    SerializeRelationshipData(a_response, rq_operations_[0].GetResourceType(), rq_operations_[0].GetRelated(), q_data_[rq_operations_[0].GetResourceType()], 0);
                } else {
                    SerializeResource(a_response, rq_operations_[0].GetResourceType(), q_data_[rq_operations_[0].GetResourceType()], 0);
                }
            }
        }
        if (   (  config_ && config_->HasVersion() )
            || ( !config_ && DocumentConfig::DefaultHasVersion() ) ) {
            appendStringInfo(&a_response, ",\"jsonapi\":{\"version\": \"1.0\",\"meta\":{\"libversion\":\"%s\"}}", LIB_VERSION);
        }
        appendStringInfoChar(&a_response, '}');
    } else if ( E_EXT_JSON_PATCH == rq_extension_ ) {
        appendStringInfoChar(&a_response, '[');
        for ( int i = 0; i < (int)rq_operations_.size(); i++ ) {
            if ( 0 == i ) {
                appendStringInfoChar(&a_response, '{');
            } else {
                appendStringInfoString(&a_response, ",{");
            }

            if ( 0 == q_errors_.size() ) {
                if ( E_OP_DELETE == rq_operations_[i].GetType() ) {
                    rq_operations_[i].SerializeMeta(a_response, true);
                } else {
                    ResourceData& rrd = q_data_[rq_operations_[i].GetResourceType()];

                    if ( rq_operations_[i].SerializeMeta(a_response, false) ) {
                        appendStringInfoChar(&a_response, ',');
                    }
                    if ( 0 == rrd.id_index_.count(rq_operations_[i].GetResourceId()) ) {
                        rq_operations_[i].AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "no result for resource type:'%s' id:'%s'",
                                    rq_operations_[i].GetResourceType().c_str(), rq_operations_[i].GetResourceId().c_str());
                        rq_operations_[i].SerializeErrors(a_response);
                    } else {
                        appendStringInfo(&a_response, "\"data\":[");
                        SerializeResource(a_response, rq_operations_[i].GetResourceType(), rrd, rrd.id_index_[rq_operations_[i].GetResourceId()] );
                        appendStringInfo(&a_response, "]");
                    }
                }
            }
            if ( q_errors_.size() ) {
                rq_operations_[i].SerializeErrors(a_response);
            }
            appendStringInfoChar(&a_response, '}');
        }
        appendStringInfoChar(&a_response, ']');
    } else {
        appendStringInfoChar(&a_response, '{');
        AddError(JSONAPI_MAKE_SQLSTATE("JA018"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "cannot serialize response");
        SerializeErrors(a_response);
        appendStringInfoChar(&a_response, '}');
    }

    return;
}

/**
 * @brief Serialize response for fetch request.
 */
void pg_jsonapi::QueryBuilder::SerializeFetchData (StringInfoData& a_response)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    const std::string&  top_type     = ( HasRelated() && !IsRelationship() ) ? config_->GetResource(GetResourceType()).GetFieldResourceType(GetRelated()) : GetResourceType();
    bool                top_is_array = ( HasRelated() && !IsRelationship() &&  config_->GetResource(GetResourceType()).IsToManyRelationship(GetRelated()) ) ? true : ! IsIndividual();


    if ( 0 == q_data_[top_type].processed_ ) {
        if ( top_is_array ) {
            appendStringInfoString(&a_response, "[]");
        } else {
            appendStringInfoString(&a_response, "null");
        }
    } else {
        appendStringInfo(&a_response, "%s",
                         top_is_array ? "[" : "");
        for (uint32 row = 0; row < q_data_[top_type].top_processed_; row++) {
            if ( row ) {
                appendStringInfoChar(&a_response, ',');
            }
            SerializeResource(a_response, top_type, q_data_[top_type], row);
        }
        if ( top_is_array ) {
            appendStringInfoChar(&a_response, ']' );
        }

        SerializeIncluded(a_response);
    }

    return;
}

/**
 * @brief Serialize top-level included object.
 */
void pg_jsonapi::QueryBuilder::SerializeIncluded (StringInfoData& a_response)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( config_->IsCompound() || rq_include_param_.size() ) {
        const char* res_start = ",\"included\":[";

        for ( ResourceDataMap::const_iterator res_type = q_data_.begin(); res_type != q_data_.end(); ++res_type ) {
            ResourceData& rrd = q_data_[res_type->first];

            if ( q_top_must_be_included_ && res_type->first == GetResourceType() ) {
                uint32 top_row = rrd.id_index_[GetResourceId()];
                if ( top_row < rrd.top_processed_ && false == rrd.items_[top_row].serialized_ ) {
                    appendStringInfoString(&a_response, res_start);
                    SerializeResource(a_response, res_type->first, rrd, top_row);
                    res_start = ",";
                }
            }
            for (uint32 row = rrd.top_processed_; row < rrd.processed_; row++) {
                appendStringInfoString(&a_response, res_start);
                SerializeResource(a_response, res_type->first, rrd, row);
                res_start = ",";
            }
        }
        if ( 1 == strlen(res_start)) {
            appendStringInfoChar(&a_response, ']');
        }
    }

    return;
}

/**
 * @brief Process postgresql result for executed command when expecting specific json result.
 *
 * @return @li true if data is valid
 *         @li false if an error occurs
 */
bool pg_jsonapi::QueryBuilder::ProcessFunctionJsonResult(const std::string& a_type)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s a_type:%s", __FUNCTION__, a_type.c_str())));

    bool        rv      = true;
    const char* attname = NULL;
    bool        is_null;

    if (  1 != SPI_processed ) {
        if( 0 == SPI_processed ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA015"), E_HTTP_NOT_FOUND)
            .SetMessage(NULL,
                                "expected %zu item%s of resource '%s', statement: %s",
                                q_required_count_, q_required_count_>1? "s":"", a_type.c_str(), q_buffer_.c_str());
        } else {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR)
            .SetMessage(NULL,
                                "expected %zu item%s of resource '%s', statement: %s",
                                q_required_count_, q_required_count_>1? "s":"", a_type.c_str(), q_buffer_.c_str());
        }

        return false;
    }

    if ( SPI_tuptable->tupdesc->natts < 2 ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "expecting record with 2 json fields 'data' and 'included' to be returned for resource '%s'", a_type.c_str());
        return false;
    }

    attname = NameStr(TupleDescAttr(SPI_tuptable->tupdesc,0)->attname);
    if ( strcmp(attname, "data") ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid query column '%s' returned for resource '%s'", attname, a_type.c_str());
        rv = false;
    }
    if ( JSONOID != TupleDescAttr(SPI_tuptable->tupdesc,0)->atttypid ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "column '%s' returned unexpected type(%d) for resource '%s'", attname, TupleDescAttr(SPI_tuptable->tupdesc,0)->atttypid, a_type.c_str());
        rv = false;
    }

    attname = NameStr(TupleDescAttr(SPI_tuptable->tupdesc,1)->attname);
    if ( strcmp(attname, "included") ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid query column '%s' returned for resource '%s'", attname, a_type.c_str());
        rv = false;
    }
    if ( JSONOID != TupleDescAttr(SPI_tuptable->tupdesc,1)->atttypid ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "column '%s' returned unexpected type(%d) for resource '%s'", attname, TupleDescAttr(SPI_tuptable->tupdesc,1)->atttypid, a_type.c_str());
        rv = false;
    }

    if ( SPI_tuptable->tupdesc->natts > 2 ) {
        ereport(LOG, (errmsg_internal("jsonapi: ignoring '%d' extra columns returned for resource '%s'", SPI_tuptable->tupdesc->natts-2, a_type.c_str())));
    }

    if ( false == rv ) {
        return rv;
    }

    SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if ( is_null ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid column '%s' returned for resource '%s', null is invalid", attname, a_type.c_str());
        return false;
    } else {
        q_json_function_data_ = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);
        if ( IsIndividual() && ('{'!=q_json_function_data_[0] || '}'!=q_json_function_data_[strlen(q_json_function_data_)-1]) ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid column '%s' returned for resource '%s', object was expected len=%zd", attname, a_type.c_str(),strlen(q_json_function_data_));
            return false;
        }
        if ( IsCollection() && ('['!=q_json_function_data_[0] || ']'!=q_json_function_data_[strlen(q_json_function_data_)-1]) ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid column '%s' returned for resource '%s', array was expected", attname, a_type.c_str());
            return false;
        }
    }

    SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2, &is_null);
    if ( !is_null ) {
        q_json_function_included_ = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2);
        if ( '['!=q_json_function_included_[0] || ']'!=q_json_function_included_[strlen(q_json_function_included_)-1] ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid column '%s' returned for resource '%s', array was expected len=%zd", attname, a_type.c_str(),strlen(q_json_function_included_));
            return false;
        }
    }

    return true;
}

/**
 * @brief Obtain a postgresql condition to filter main request based on fields.
 *
 * @param a_type The resource type.
 * @param a_field The related field.
 * @param a_value The value of field to be filtered.
 */
const std::string& pg_jsonapi::QueryBuilder::GetFilterTableByFieldCondition (const std::string& a_type, const std::string& a_field, const std::string& a_value)
{
    const ResourceConfig& rc = config_->GetResource(a_type);

    if ( rc.IsPGChildRelation(a_field) ) {
        q_buffer_ += rc.GetPGQueryColId();
        q_buffer_ += " IN ( SELECT " + rc.GetPGRelationQueryColParentId(a_field) + " FROM ";
        rc.AddPGRelationQueryFromItem(q_buffer_, a_field);
        q_buffer_ += " WHERE ";
        if ( rc.GetPGRelationQueryCondition(a_field).size() ) {
            q_buffer_ += rc.GetPGRelationQueryCondition(a_field) + " AND ";
        }
        q_buffer_ += rc.GetPGRelationQueryColChildId(a_field);
        if ( a_value.empty() ) {
            q_buffer_ += " IS NULL";
        } else {
            q_buffer_ += " = ";
            OperationRequest::AddQuotedStringToBuffer(q_buffer_, a_value.c_str(), /*quote*/ true, /*no_html*/ false);
        }
        q_buffer_ += " ) ";
    } else {
        if ( rc.IsQueryFromAttributesFunction() ) {
            rc.AddPGQueryItem(q_buffer_);
            q_buffer_ += ".";
        }
        q_buffer_ += rc.GetPGQueryColumn(a_field);
        if ( a_value.empty() ) {
            q_buffer_ += " IS NULL";
        } else {
            q_buffer_ += " = ";
            OperationRequest::AddQuotedStringToBuffer(q_buffer_, a_value.c_str(), /*quote*/ true, /*no_html*/ false);
        }
    }

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

void pg_jsonapi::QueryBuilder::RequestOperationResponseData (const std::string& a_type, const std::string& a_id)
{
    if ( "GET" == GetRequestMethod() && IsIndividual() && GetResourceType() == a_type && GetResourceId() == a_id ) {
        // top resource was included, we need to allow it to appear in 'included'
        q_top_must_be_included_ = true;
    } else {
        q_to_be_included_[a_type].insert(a_id);
    }
    return;
}
