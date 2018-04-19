/**
 * @file operation_request.rl Implementation of OperationRequest
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

#include "operation_request.h"

#include "query_builder.h"

extern pg_jsonapi::QueryBuilder* g_qb;

/**
 * @brief Constructor
 */
pg_jsonapi::OperationRequest::OperationRequest ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    rq_index_        = 0;
    rq_type_         = E_OP_UNDEFINED;
    rq_relationship_ = false;
    rq_body_data_    = NULL;
    q_required_count_ = 1;
}

/**
 * @brief Destructor
 */
pg_jsonapi::OperationRequest::~OperationRequest ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
    /*
     * delete contained objects
     */
}

/**
 * @brief Create an error and keep the association to current operation request.
 *
 * @return @li ErrorObject the new error
 */
pg_jsonapi::ErrorObject& pg_jsonapi::OperationRequest::AddError(int a_sqlerrcode, HttpStatusErrorCode a_status)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
    pg_jsonapi::ErrorObject& e = g_qb->AddError(a_sqlerrcode, a_status, true);
    error_index_.push_back(g_qb->ErrorsSize()-1);
    return e;
}

/**
 * @brief Serialize operation errors.
 */
void pg_jsonapi::OperationRequest::SerializeErrors (StringInfoData& a_response)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    bool needs_comma = false;
    appendStringInfoString(&a_response, "\"errors\":[");

    if ( 0 == error_index_.size() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA004"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "transaction errors occured, check common errors");
    }

    for ( SizeVector::iterator err_i = error_index_.begin(); err_i != error_index_.end(); ++err_i ) {
        ereport(DEBUG3, (errmsg_internal("jsonapi: err_i=%zd", *err_i)));
        if ( needs_comma ) {
            appendStringInfoChar(&a_response, ',');
        }
        if ( 0 == rq_index_ ) {
            g_qb->GetError(*err_i).Serialize(a_response, true);
            g_qb->SerializeCommonErrorItems(a_response);
            appendStringInfoString(&a_response, "]}}");
        } else {
            g_qb->GetError(*err_i).Serialize(a_response, false);
        }
        needs_comma = true;

    }

    appendStringInfoChar(&a_response, ']');
}

/**
 * @brief Parse the request path combinated with base URL
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::OperationRequest::ParsePath (std::string a_patch_path)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( "/-" == a_patch_path ) {
        /* target is the end of a collection */
        if ( E_OP_CREATE != rq_type_ ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "path can only target a resource collection (/-) on 'add' operations");
            return false;
        }
        if ( ! ( rq_resource_id_.empty() || rq_relationship_ ) ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "path can only target a resource collection (/-) for resource or relationship");
            return false;
        }
        return true;
    } else if ( ! a_patch_path.empty() ) {
        std::string full_path = g_qb->GetRequestUrl().c_str()+g_qb->GetRequestBaseUrl().length() + a_patch_path;
        int   cs;
        char* p     = (char*)full_path.c_str();
        char* pe    = p + full_path.length();
        char* eof   = pe;
        char* start = NULL;

        %%{
            machine OperationPath;
            include RequestBasicElements "basic_elements.rlh";

            main := ( '/'+ resource ('/' id ( ('/' attribute) | (relationship '/' related) )? )? );
            write data;
            write init;
            write exec;
        }%%

        if ( cs < OperationPath_first_final)
        {
            AddError(JSONAPI_MAKE_SQLSTATE("JA013"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "parse_error near col %d (%s).",
                        int(p - full_path.c_str()), full_path.c_str());
            return false;
        }
    	(void) OperationPath_en_main;
    	(void) OperationPath_error;
    }

    if ( HasError() ) {
        return false;
    }
    return true;
}

/**
 * @brief Check if the request body is a valid resource object data
 *
 * @return @li true if data corresponds to a resource object data
 *         @li false if not
 */
bool pg_jsonapi::OperationRequest::BodyHasValidResourceData()
{
    if (! (*rq_body_data_).isObject() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource value must be a valid jsonapi resource object");
        return false;
    }

    const JsonapiJson::Value::Members& members = (*rq_body_data_).getMemberNames();
    for ( JsonapiJson::Value::Members::const_iterator key = members.begin(); key != members.end(); ++key ) {
        if ( "type" == *key ) {
            if ( ! (*rq_body_data_)["type"].isString()
                || (*rq_body_data_)["type"].asString() != rq_resource_type_ ) {
                AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "type in body is required, must match path and be a non-empty string");
                return false;
            }
        } else if ( "id" == *key ) {
            if ( ! (*rq_body_data_)["id"].isString()
                || ( rq_resource_id_.length() && (*rq_body_data_)["id"].asString() != rq_resource_id_ ) ) {
                AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "id in body is required, must match path and be a non-empty string");
                return false;
            }
            rq_resource_id_ = (*rq_body_data_)["id"].asString();
        } else if ( "attributes" == *key ) {
            if ( ! BodyHasValidAttributes((*rq_body_data_)["attributes"]) ) {
                return false;
            }
        } else if ( "relationships" == *key ) {
            if ( ! BodyHasValidRelationships((*rq_body_data_)["relationships"]) ) {
                return false;
            }
        } else {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource value must be a valid jsonapi resource object");
            return false;
        }
    }

    return true;
}

/**
 * @brief Check if the request body "attributes" has valid jsonapi data.
 *
 * @return @li true if is a valid attributes object data
 *         @li false if not
 */
bool pg_jsonapi::OperationRequest::BodyHasValidAttributes(const JsonapiJson::Value& a_value)
{
    if ( a_value.isNull() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "null value for attributes");
        return false;
    }

    if (   ! a_value.isObject() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for attributes");
        return false;
    }

    const JsonapiJson::Value::Members& members = a_value.getMemberNames();
    for ( JsonapiJson::Value::Members::const_iterator key = members.begin(); key != members.end(); ++key ) {
        //#warning joana TODO: check key?
        if ( ! (a_value[*key].isConvertibleTo(JsonapiJson::stringValue) || a_value[*key].isArray() ) ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for attribute '%s'", (*key).c_str());
            return false;
        } else if ( a_value[*key].isArray() ) {
            for ( JsonapiJson::ArrayIndex i = 0; i < a_value[*key].size(); i++ ) {
                if ( ! a_value[*key][i].isConvertibleTo(JsonapiJson::stringValue) ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for attribute '%s[%d]'", (*key).c_str(), i);
                    return false;
                }
            }
        }
    }

    return true;
}

/**
 * @brief Check if the request body "relationships" has valid jsonapi data.
 *
 * @return @li true if is a valid relationships object data
 *         @li false if not
 */
bool pg_jsonapi::OperationRequest::BodyHasValidRelationships(const JsonapiJson::Value& a_value)
{
    if ( a_value.isNull() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "null value for relationships");
        return false;
    }

    if (   ! a_value.isObject() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for relationships");
        return false;
    }

    if ( ! a_value.isObject() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid data for \"relationships\"");
        return false;
    }

    const JsonapiJson::Value::Members& members = a_value.getMemberNames();
    for ( JsonapiJson::Value::Members::const_iterator related = members.begin(); related != members.end(); ++related ) {
        //#warning joana TODO: check key? check to-one vs to-many?

        if ( 1 != a_value[*related].size() || ! a_value[*related].isMember("data") ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid data for relationship \"%s\"", (*related).c_str());
            return false;
        }

        if ( a_value[*related]["data"].isObject() ) {
            if ( ! BodyHasValidRelationshipData(a_value[*related]["data"]) ) {
                return false;
            }
        } else if ( a_value[*related]["data"].isArray() ) {
            for (unsigned int index = 0; index < a_value[*related]["data"].size(); index++) {
                if ( ! BodyHasValidRelationshipData( a_value[*related][index] ) ) {
                    return false;
                }
            }
        } else {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for relationship \"%s\"", (*related).c_str());
            return false;
        }
    }

    return true;
}

/**
 * @brief Check if the data for each item in "relationships" has valid jsonapi data.
 *
 * @return @li true if data is valid relationships object data
 *         @li false if not
 */
bool pg_jsonapi::OperationRequest::BodyHasValidRelationshipData(const JsonapiJson::Value& a_value)
{
    if ( a_value.isNull() ) {
        return true;
    }

    if (   ! a_value.isObject()
        || ( 2 != a_value.getMemberNames().size() )
        || ( !a_value.isMember("type") || !a_value["type"].isString() || a_value["type"].asString().empty() )
        || ( !a_value.isMember("id")   || !a_value["id"].isString()   || a_value["id"].asString().empty()   )
        || ( rq_relationship_ && a_value["type"].asString() != rq_related_ )
        ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for relationship");
        return false;
    }

    return true;
}

/**
 * @brief Set content of request and validates the request data.
 *
 * @return @li true if data is valid
 *         @li false if an error occurs
 */
bool pg_jsonapi::OperationRequest::SetRequest (const JsonapiJson::Value* a_data, std::string a_patch_path)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    bool rv = true;
    rq_body_data_     = a_data;

    if ( a_patch_path.length() ) {
        if ( ! ParsePath(a_patch_path) ) {
            return false;
        }
    }
    if ( "/-" == a_patch_path || a_patch_path.empty() ) {
        rq_resource_type_ = g_qb->GetResourceType();
        rq_resource_id_   = g_qb->GetResourceId();
        rq_related_       = g_qb->GetRelated();
        rq_relationship_  = g_qb->IsRelationship();
    }

    if ( rq_resource_type_.empty() ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource type must be defined on url or operation path");
        rv = false;
    }

    if ( !rq_attribute_.empty() ) {
        if ( E_OP_UPDATE != rq_type_ ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "attribute can only be specified on path for replace operations");
            rv = false;
        }
        if ( !(*rq_body_data_).isConvertibleTo(JsonapiJson::stringValue) ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for attribute '%s'", rq_attribute_.c_str());
            rv = false;
        }
    } else if ( rq_relationship_ ) {
        if ( (*rq_body_data_).isObject() ) {
            if ( ! BodyHasValidRelationshipData(*rq_body_data_) ) {
                rv = false;
            }
        } else if ( (*rq_body_data_).isArray() ) {
            for (unsigned int index = 0; index < (*rq_body_data_).size(); index++) {
                if ( ! BodyHasValidRelationshipData((*rq_body_data_)[index]) ) {
                    rv = false;
                }
            }
        } else {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "invalid value for relationship");
            rv = false;
        }
    } else { // resource object
        if ( NULL != rq_body_data_  ) {
            if ( !BodyHasValidResourceData() ) {
                return false;
            }
            if (   ( E_OP_UPDATE == rq_type_ || E_OP_DELETE == rq_type_ )
                && (   ! (*rq_body_data_).isMember("id")
                    || ! (*rq_body_data_)["id"].isString()
                    || (*rq_body_data_)["id"].asString().empty() ) ) {
                    AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "type and id must be specified in body" );
                    rv = false;
                }
        }
    }

    if ( E_OP_CREATE == rq_type_ ) {
        if ( !rq_attribute_.empty() ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "create operations are only valid to define attributes");
            rv = false;
        }
    } else {
        if ( rq_resource_id_.empty() ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource id must be defined on url or operation path, except on creation operations");
            rv = false;
        }
    }

    return rv;
}

/**
 * @brief Get a Json Array converted to a SQL Array Value to be used on insert/update.
 */
void pg_jsonapi::OperationRequest::GetArrayAsSQLValue(const JsonapiJson::Value& a_value, std::string& a_sql_value)
{
    a_sql_value += "'{";
    const char* sep = "";
    for ( JsonapiJson::ArrayIndex i = 0; i < a_value.size(); i++ ) {
        a_sql_value += sep;
        a_sql_value += '"';
        AddQuotedStringToBuffer(a_sql_value, a_value[i].asString().c_str(), false);
        a_sql_value += '"';
        sep = ",";
    }
    a_sql_value += "}'";
}

/**
 * @brief Get a command to INSERT a resource returning it's id.
 */
const std::string& pg_jsonapi::OperationRequest::GetResourceInsertCmd()
{
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);
    std::string values;

    q_buffer_ = "INSERT INTO ";
    rc.AddPGQueryFromItem(q_buffer_);

    if ( (*rq_body_data_).isMember("id") ) {
        q_buffer_ += " (" + rc.GetPGQueryColId();
        values += " VALUES (";
        AddQuotedStringToBuffer(values, (*rq_body_data_)["id"].asString().c_str(), true);
    }

    const JsonapiJson::Value::Members& attrs = (*rq_body_data_)["attributes"].getMemberNames();
    for ( JsonapiJson::Value::Members::const_iterator key = attrs.begin(); key != attrs.end(); ++key ) {
        if ( values.empty() ) {
            q_buffer_ += " (" ;
            values = " VALUES (";
        } else {
            q_buffer_ += ",";
            values    += ",";
        }
        q_buffer_ += rc.GetPGQueryColumn(*key);

        if ( (*rq_body_data_)["attributes"][*key].isNull() ) {
            values += "NULL";
        } else if ( (*rq_body_data_)["attributes"][*key].isArray() ) {
            GetArrayAsSQLValue((*rq_body_data_)["attributes"][*key], values);
        } else {
            AddQuotedStringToBuffer(values, (*rq_body_data_)["attributes"][*key].asString().c_str(), true);
        }
    }

    const JsonapiJson::Value::Members& relats = (*rq_body_data_)["relationships"].getMemberNames();
    for ( JsonapiJson::Value::Members::const_iterator key = relats.begin(); key != relats.end(); ++key ) {
        if ( ! rc.IsPGChildRelation(*key) ) { // relationship on parent table
            if ( values.empty() ) {
                q_buffer_ += " (" ;
                values = " VALUES (";
            } else {
                q_buffer_ += ",";
                values    += ",";
            }
            q_buffer_ += rc.GetPGQueryColumn(*key);
            AddQuotedStringToBuffer(values, (*rq_body_data_)["relationships"][*key]["data"]["id"].asString().c_str(), true);
        }
    }
//#warning TODO "relationships that are NOT in same table"

    q_buffer_ += ")";
    values += ")";

    q_buffer_ += values + " RETURNING "+ rc.GetPGQueryColId() +" AS id;";

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief Add a quoted String to the query buffer
 */
void pg_jsonapi::OperationRequest::AddQuotedStringToBuffer(std::string& a_buffer, const char* a_value, bool a_quote_value)
{
    if ( a_quote_value ) {
        a_buffer += '\'';
    }
    for (const char *valptr = a_value; *valptr; valptr++)
    {
        a_buffer += *valptr;
        if ( '\'' == *valptr ) {
            a_buffer += *valptr;
        }
    }
    if ( a_quote_value ) {
        a_buffer += '\'';
    }
}

/**
 * @brief Get a command to UPDATE a resource returning it's id.
 */
const std::string& pg_jsonapi::OperationRequest::GetResourceUpdateCmd()
{
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);
    bool first = true;

    q_buffer_ = "UPDATE ";
    rc.AddPGQueryFromItem(q_buffer_);

    const JsonapiJson::Value::Members& attrs = (*rq_body_data_)["attributes"].getMemberNames();
    for ( JsonapiJson::Value::Members::const_iterator key =attrs.begin(); key != attrs.end(); ++key ) {
        if ( first ) {
            first = false;
            q_buffer_ += " SET " ;
        } else {
            q_buffer_ += ",";
        }
        if ( (*rq_body_data_)["attributes"][*key].isNull() ) {
            q_buffer_ += rc.GetPGQueryColumn(*key) + "= NULL";
        } else if ( (*rq_body_data_)["attributes"][*key].isArray() ) {
            q_buffer_ += rc.GetPGQueryColumn(*key) + "=";
            GetArrayAsSQLValue((*rq_body_data_)["attributes"][*key], q_buffer_);
        } else {
            q_buffer_ += rc.GetPGQueryColumn(*key) + "=";
            AddQuotedStringToBuffer(q_buffer_, (*rq_body_data_)["attributes"][*key].asString().c_str(), true);
        }
    }

    const JsonapiJson::Value::Members& relats = (*rq_body_data_)["relationships"].getMemberNames();
    for ( JsonapiJson::Value::Members::const_iterator key = relats.begin(); key != relats.end(); ++key ) {
        if ( ! rc.IsPGChildRelation(*key) ) { // relationship on parent table
            if ( first ) {
                first = false;
                q_buffer_ += " SET " ;
            } else {
                q_buffer_ += ",";
            }
            q_buffer_ += rc.GetPGQueryColumn(*key) + "=";
            AddQuotedStringToBuffer(q_buffer_, (*rq_body_data_)["relationships"][*key]["data"]["id"].asString().c_str(), true);
        }
    }
    q_buffer_ += " WHERE ";
    if ( rc.GetPGQueryCondition().size() ) {
        q_buffer_ += rc.GetPGQueryCondition() + " AND ";
    }
    q_buffer_ += rc.GetPGQueryColId() + " = '" + rq_resource_id_ +
    "' RETURNING " + rc.GetPGQueryColId() + " AS id;";

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}
/**
 * @brief Get a command to delete a resource returning it's id.
 */
const std::string& pg_jsonapi::OperationRequest::GetResourceDeleteCmd()
{
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    q_buffer_ = "DELETE FROM ";
    rc.AddPGQueryFromItem(q_buffer_);

    q_buffer_ += " WHERE ";
    if ( rc.GetPGQueryCondition().size() ) {
        q_buffer_ += rc.GetPGQueryCondition() + " AND ";
    }
    q_buffer_ += rc.GetPGQueryColId() + " = '" + rq_resource_id_ +
    "' RETURNING " + rc.GetPGQueryColId() + " AS id;";

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief Get a command to UPDATE one resource field to given value, returning resource id.
 */
const std::string& pg_jsonapi::OperationRequest::GetFieldUpdateCmd (const std::string& a_field,  const char* a_value)
{
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    q_buffer_ = "UPDATE ";
    rc.AddPGQueryFromItem(q_buffer_);
    if ( NULL == a_value ) {
        q_buffer_ += " SET " + a_field + " = NULL";
    } else {
        q_buffer_ += " SET " + a_field + " = ";
        AddQuotedStringToBuffer(q_buffer_, a_value, true);
    }

    q_buffer_ += " WHERE ";
    if ( rc.GetPGQueryCondition().size() ) {
        q_buffer_ += rc.GetPGQueryCondition() + " AND ";
    }
    q_buffer_ += rc.GetPGQueryColId() + " = '" + rq_resource_id_ +
    "' RETURNING " + rc.GetPGQueryColId() + " AS id;";

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief Get a command to INSERT elements into a relationship, returning id of resource and new relationships.
 */
const std::string& pg_jsonapi::OperationRequest::GetPGChildRelationshipInsertCmd ()
{
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    q_buffer_ = "INSERT INTO ";
    rc.AddPGRelationQueryFromItem(q_buffer_, rq_related_);

    q_buffer_ += " VALUES (" + rc.GetPGRelationQueryColParentId(rq_related_)
              + "," + rc.GetPGRelationQueryColChildId(rq_related_) + ") ";

    if ( (*rq_body_data_).isObject() ) {
        q_buffer_ += "('" + rq_resource_id_ + "', '" + (*rq_body_data_)["id"].asString() + "') ";
    } else { // (*rq_body_data_).isArray()
        for (unsigned int index = 0; index < (*rq_body_data_).size(); index++) {
            if ( index ) {
                q_buffer_ += ",";
            }
            q_buffer_ += "('" + rq_resource_id_ + "', '" + (*rq_body_data_)["id"].asString() + "') ";
        }
    }
    //#warning TODO JOANA RETURNING
    q_buffer_ += ";";

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief Get a command to delete all elements from a relationship, returning id of resource and deleted relationships.
 */
const std::string& pg_jsonapi::OperationRequest::GetPGChildRelationshipDeleteCmd ()
{
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    q_buffer_ = "DELETE FROM ";
    rc.AddPGRelationQueryFromItem(q_buffer_, rq_related_);

    q_buffer_ += " WHERE ";
    if ( rc.GetPGRelationQueryCondition(rq_related_).size() ) {
        q_buffer_ +=  rc.GetPGRelationQueryCondition(rq_related_) + " AND ";
    }
    q_buffer_ += rc.GetPGRelationQueryColParentId(rq_related_) + " = '" + rq_resource_id_ + "';";
    //#warning TODO JOANA RETURNING

    ereport(DEBUG3, (errmsg_internal("jsonapi: %s return:%s",
                                     __FUNCTION__, q_buffer_.c_str())));
    return q_buffer_;
}

/**
 * @brief Get a command to execute requested INSERT operation.
 */
const std::string& pg_jsonapi::OperationRequest::GetInsertCmd ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    if ( rq_relationship_ ) {
        if ( rc.IsPGChildRelation(rq_related_ ) ) {
            GetPGChildRelationshipInsertCmd();
        } else {
            GetFieldUpdateCmd(rq_related_, (*rq_body_data_)["id"].asString().c_str());
        }
    } else if ( !rq_attribute_.empty() ) {
        GetFieldUpdateCmd(rq_attribute_, (*rq_body_data_).asString().c_str());
    } else { // resource object
        GetResourceInsertCmd();
    }

    return q_buffer_;
}

/**
 * @brief Get a command to execute requested UPDATE operation.
 */
const std::string& pg_jsonapi::OperationRequest::GetUpdateCmd ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    if ( rq_relationship_ ) {
        if ( rc.IsPGChildRelation(rq_related_ ) ) {
            GetPGChildRelationshipDeleteCmd();
            GetPGChildRelationshipInsertCmd();
        } else {
            GetFieldUpdateCmd(rq_related_, ((*rq_body_data_)["id"].asString().c_str()));
        }
    } else if ( !rq_attribute_.empty() ) {
        GetFieldUpdateCmd(rq_attribute_, (*rq_body_data_).asString().c_str());
    } else { // resource object
        GetResourceUpdateCmd();
    }

    return q_buffer_;
}

/**
 * @brief Get a command to execute requested DELETE operation.
 */
const std::string& pg_jsonapi::OperationRequest::GetDeleteCmd ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    if ( rq_relationship_ ) {
        if ( rc.IsPGChildRelation(rq_related_ ) ) {
            GetPGChildRelationshipDeleteCmd();
        } else {
            GetFieldUpdateCmd(rq_related_, NULL);
        }
    } else if ( !rq_attribute_.empty() ) {
        GetFieldUpdateCmd(rq_attribute_, NULL);
    } else { // resource object
        GetResourceDeleteCmd();
    }

    return q_buffer_;
}

void pg_jsonapi::OperationRequest::InitObservedStat()
{
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);

    for ( StringMap::const_iterator it = rc.GetObserved().begin(); it != rc.GetObserved().end(); ++it ) {
        std::string res_s = it->first;
        const ResourceConfig& rc_s = g_qb->GetDocumentConfig()->GetResource(res_s);

        Oid stat_oid = rc_s.GetOid();
        if ( q_observed_stat_.count(it->first) ) {
            q_observed_stat_.at(it->first).Init();
        } else {
            q_observed_stat_.insert( std::pair<std::string, pg_jsonapi::ObservedStat>( it->first, ObservedStat(stat_oid)) );
        }
    }
}

/**
 * @brief Process postgresql result for executed command.
 *
 * @return @li true if data is valid
 *         @li false if an error occurs
 */
bool pg_jsonapi::OperationRequest::ProcessOperationResult ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s",__FUNCTION__)));

    for ( ObservedStatMap::iterator it = q_observed_stat_.begin(); it != q_observed_stat_.end(); ++it ) {
        (it->second).CountChanges();
    }

    if ( q_required_count_ && q_required_count_ != SPI_processed ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA015"), E_HTTP_NOT_FOUND).SetMessage(NULL, "expected %zu item%s of resource '%s', statement: %s",
                    q_required_count_, q_required_count_>1? "s":"", rq_resource_type_.c_str(), q_buffer_.c_str());
        return false;
    }
    if ( 0 == SPI_processed || 0 == SPI_tuptable->tupdesc->natts ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA015"), E_HTTP_NOT_FOUND).SetMessage(NULL, "nonexistent resource '%s' with id '%s', statement: %s",
                    rq_resource_type_.c_str(), rq_resource_id_.c_str(), q_buffer_.c_str());
        return false;
    }

    const char* id = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);
    const char* attname = NameStr(SPI_tuptable->tupdesc->attrs[0]->attname);

    if ( NULL == id ||  0 == strlen(id) ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "empty or NULL id was returned for resource '%s'",
                    rq_resource_type_.c_str());
        return false;
    }
    if ( strcmp(attname, "id") ) {
        AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid query column '%s' returned for resource '%s'",
                    attname, rq_resource_type_.c_str());
        return false;
    }


    if ( !rq_resource_id_.empty() && id != rq_resource_id_ ) {
        if ( rq_relationship_ || rq_attribute_.empty() ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s' id changed from '%s' to '%s'",
                        rq_resource_type_.c_str(), rq_resource_id_.c_str(), id);
            return false;
        }
        ereport(LOG, (errmsg_internal("jsonapi: resource '%s' id changed from '%s' to '%s'",
                                      rq_resource_type_.c_str(), rq_resource_id_.c_str(), id)));
    }
    rq_resource_id_ = id;


    if ( rq_relationship_ ) {
        const char* field = NameStr(SPI_tuptable->tupdesc->attrs[1]->attname);
        if (   2 != SPI_tuptable->tupdesc->natts
            || strcmp(field, rq_related_.c_str()) ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid query column '%s' returned for resource '%s'",
                        field, rq_resource_type_.c_str());
            return false;
        }
    } else { // resource object or attribute changed
        if (   1 != SPI_tuptable->tupdesc->natts ) {
            AddError(JSONAPI_MAKE_SQLSTATE("JA016"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid query column '%s' returned for resource '%s'",
                        attname, rq_resource_type_.c_str());
            return false;
        }
    }

    if ( E_OP_DELETE != rq_type_) {
        g_qb->RequestOperationResponseData(rq_resource_type_, rq_resource_id_);
    }

    return true;
}

/**
 * @brief Serialize operation observed flags inside meta.
 */
bool pg_jsonapi::OperationRequest::SerializeObservedInMeta (StringInfoData& a_response)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
    const ResourceConfig& rc = g_qb->GetDocumentConfig()->GetResource(rq_resource_type_);
    size_t needs_comma = false;
    StringSet serialized_meta;
    int64  total_changes;
    ObservedStatMap::const_iterator rem;

    if ( q_observed_stat_.size() ) {
        appendStringInfo(&a_response, "\"observed\":{");
        for ( ObservedStatMap::const_iterator it = q_observed_stat_.begin(); it != q_observed_stat_.end(); ++it ) {
            if ( 0 == serialized_meta.count(rc.GetObservedMetaName(it->first)) ) {
                serialized_meta.insert(rc.GetObservedMetaName(it->first));
                if ( needs_comma ) {
                    appendStringInfoChar(&a_response, ',');
                }
                total_changes = it->second.TotalChanges();
                /* sum total from remaining observed with same meta name */
                rem = it;
                rem++;
                for ( ; rem != q_observed_stat_.end(); ++rem) {
                    if ( rc.GetObservedMetaName(rem->first) == rc.GetObservedMetaName(it->first) ) {
                        total_changes += rem->second.TotalChanges();
                    }
                }
                appendStringInfo(&a_response, "\"%s\":%ld", rc.GetObservedMetaName(it->first).c_str(), total_changes);
                needs_comma = true;
            }
        }
        appendStringInfo(&a_response, "}");
    }

    return needs_comma;
}

/**
 * @brief Serialize operation meta.
 */
bool pg_jsonapi::OperationRequest::SerializeMeta (StringInfoData& a_response, bool a_write_empty)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( 0 == q_observed_stat_.size() ) {
        if ( a_write_empty ) {
            appendStringInfo(&a_response, "\"meta\":{}");
            return true;
        } else {
            return false;
        }
    } else {
        appendStringInfo(&a_response, "\"meta\":{");
        SerializeObservedInMeta(a_response);
        appendStringInfoChar(&a_response, '}');
        return true;
    }
}
