/**
 * @file document_config.cc Implementation of DocumentConfig.
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

#include "json/json.h"
extern "C" {
#include "postgres.h"
#include "executor/spi.h"
#include "catalog/namespace.h"
} // extern "C"
#include "document_config.h"
#include "query_builder.h"

extern pg_jsonapi::QueryBuilder* g_qb;

/**
 * @brief Create a document configuration for the base url and initialize with default behaviour.
 *
 * @param a_base_url The base url in form scheme://domain[:port]
 */
pg_jsonapi::DocumentConfig::DocumentConfig (const std::string a_base_url) : base_url_(a_base_url)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s base_url_:%s", __FUNCTION__, base_url_.c_str())));

    /*
     * Attribute defaults
     */
    is_valid_                      = false;
    version_                       = DefaultHasVersion();
    compound_                      = DefaultIsCompound();
    page_size_                     = DefaultPageSize();
    page_limit_                    = DefaultPageLimit();
    show_links_                    = DefaultShowLinks();
    show_null_                     = DefaultShowNull();
    restrict_type_                 = DefaultTypeRestriction();
    restrict_attr_                 = DefaultAttrRestriction();
    empty_is_null_                 = DefaultEmptyIsNull();
    use_request_accounting_schema_ = DefaultRequestAccountingSchema();
    use_request_sharded_schema_    = DefaultRequestShardedSchema();
    use_request_company_schema_    = DefaultRequestCompanySchema();
    use_request_accounting_prefix_ = DefaultRequestAccountingPrefix();
}

/**
 * @brief Destructor.
 */
pg_jsonapi::DocumentConfig::~DocumentConfig ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s base_url_:%s", __FUNCTION__, base_url_.c_str())));

}

/**
 * @brief Load the jsonapi configuration from DB for base url provided in constructor.
 *
 * @return @li true if operation succeeds, even if there is no configuration available
 *         @li false if an error occurs while fetching data or if configuration is invalid
 */
bool pg_jsonapi::DocumentConfig::LoadConfigFromDB (bool& o_config_exists)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s base_url_:%s", __FUNCTION__, base_url_.c_str())));

    bool rv = true;

    JsonapiJson::Reader reader;
    JsonapiJson::Value  root;

    o_config_exists = false;

    Oid s_oid = get_namespace_oid("public", true);
    Oid relid = InvalidOid;
    if ( ! OidIsValid(s_oid) ) {
        ereport(DEBUG1, (errmsg_internal("jsonapi [libversion %s]: cannot load configuration for URL '%s' ('public' schema does not exist)",
                                      LIB_VERSION, base_url_.c_str())));
        return true; // configuration is not mandatory
    } else {
        relid = get_relname_relid( "jsonapi_config", s_oid);
        if ( ! OidIsValid(relid) ) {
            ereport(DEBUG1, (errmsg_internal("jsonapi [libversion %s]: cannot load configuration for URL '%s' ('public.jsonapi_config' does not exist)",
                                          LIB_VERSION, base_url_.c_str())));
            return true; // configuration is not mandatory
        }
    }

    /* execute the config query as read-only */
    if ( ! g_qb->SPIExecuteCommand(ConfigQuery(), SPI_OK_SELECT) ) {
        return false;
    }
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s SPI_processed=%d", __FUNCTION__, (int)SPI_processed)));
    if ( SPI_processed > 1 ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "too many rows (%d) returned for '%s' statement: %s",
																							   (int)SPI_processed, base_url_.c_str(), ConfigQuery().c_str());
        rv = false;
    } else if ( 0 == SPI_processed ) {
        ereport(DEBUG1, (errmsg_internal("jsonapi [libversion %s]: no specific configuration for prefix '%s' statement: %s",
                                      LIB_VERSION, base_url_.c_str(), ConfigQuery().c_str() )));
        rv = true; // not really needed but it's more explicit
    } else if ( 1 == SPI_processed ) {
        char* config_s = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);

        o_config_exists = true;
        if ( NULL == config_s || 0 == strlen(config_s) ) {
            ereport(DEBUG1, (errmsg_internal("jsonapi [libversion %s]: empty configuration for '%s'", LIB_VERSION, base_url_.c_str())) );
            // configuration is not mandatory
        } else {
            if ( ! reader.parse(config_s, config_s + strlen(config_s), root, false) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid json returned returned for '%s': %s",
                           base_url_.c_str(), reader.getFormatedErrorMessages().c_str());
                rv = false;
            } else {
                /* global options */
                BoolOption bool_options[] = {
                    {"version", &version_},
                    {"compound", &compound_},
                    {"show-links", &show_links_},
                    {"show-null", &show_null_},
                    {"type-restriction", &restrict_type_},
                    {"attribute-restriction", &restrict_attr_},
                    {"empty-is-null", &empty_is_null_},
                    {"request-accounting-schema", &use_request_accounting_schema_},
                    {"request-sharded-schema", &use_request_sharded_schema_},
                    {"request-company-schema", &use_request_company_schema_},
                    {"request-accounting-prefix", &use_request_accounting_prefix_}
                };
                UIntOption uint_options[] = {
                    {"page-size", &page_size_},
                    {"page-limit", &page_limit_}
                };
                StringOption str_options[] = {
                    {"pg-search_path", &template_search_path_},
                    {"pg-order-by", &default_order_by_}
                };
                for ( size_t i = 0; i <  sizeof(bool_options)/sizeof(bool_options[0]); ++i ) {
                    const JsonapiJson::Value& option = root[bool_options[i].name];

                    if ( ! option.isNull() ) {
                        if ( false == option.isBool() ) {
                            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for '%s' for '%s', boolean is expected.",
                                       bool_options[i].name, base_url_.c_str());
                            rv = false;
                        } else {
                            *(bool_options[i].ptr) = option.asBool();
                        }
                    }
                }
                for ( size_t i = 0; i < sizeof(uint_options)/sizeof(uint_options[0]); ++i ) {
                    const JsonapiJson::Value& option = root[uint_options[i].name];
                    if ( ! option.isNull() ) {
                        if ( false == option.isUInt() ) {
                            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for '%s' for '%s', uint is expected.",
                                        uint_options[i].name, base_url_.c_str());
                            rv = false;
                        } else {
                            *(uint_options[i].ptr) = option.asUInt();
                        }
                    }
                }
                for ( size_t i = 0; i < sizeof(str_options) / sizeof(str_options[0]); ++i ) {
                    const JsonapiJson::Value& option = root[str_options[i].name];
                    if ( ! option.isNull() ) {
                        if ( false == option.isString() || option.empty() ) {
                            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for '%s' for '%s', string is expected.",
                                        str_options[i].name, base_url_.c_str());
                            rv = false;
                        } else {
                            *(str_options[i].ptr) = option.asString();
                        }
                    }
                }

                if ( page_limit_ > MaximumPageLimit() ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'page-limit' for '%s', maximum allowed page-limit is %u ",
                                base_url_.c_str(), MaximumPageLimit());
                    rv = false;
                } else if ( page_size_ > page_limit_ ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'page-size' for '%s', it cannot exceed 'page-limit' which is %u ",
                                base_url_.c_str(), page_limit_);
                    rv = false;
                }

                /* resource specification */
                const JsonapiJson::Value&  resources = root["resources"];

                if ( ! resources.isNull() ) {
                    if ( false == resources.isArray() ) {
                        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid value for 'resources' for '%s', array is expected.",
                                   base_url_.c_str());
                        rv = false;
                    } else {
                        for (unsigned int index = 0; index < resources.size(); index++) {
                           if ( false == resources[index].isObject() || resources[index].size() > 1 ) {
                               g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "jsonapi: invalid value for 'resources' item for '%s', object is expected.",
                                          base_url_.c_str());
                               rv = false;
                           } else {
                               const std::string key = resources[index].getMemberNames()[0];
                               std::pair<ResourceConfigMapIterator,bool> res = resources_.insert( std::pair<std::string,pg_jsonapi::ResourceConfig>(key, ResourceConfig(this, key)) );
                               if ( false == res.second ) {
                                   g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "jsonapi: duplicate configuration for 'resources' item '%s'",
                                               key.c_str());
                                   rv = false;
                               }
                               if ( res.second ) {
                                   rv &= res.first->second.SetValues(resources[index][key]);
                               }
                           }
                        }
                    }
                }

                if ( rv ) {
                    if ( g_qb->HasErrors() ) {
                        ereport(WARNING, (errmsg_internal("jsonapi [libversion %s]: uncontrolled errors while loading configuration for prefix '%s'", LIB_VERSION, base_url_.c_str() )));
                        rv = false;
                    } else {
                        ereport(DEBUG1, (errmsg_internal("jsonapi [libversion %s]: success loading configuration for prefix '%s'", LIB_VERSION, base_url_.c_str() )));
                    }
                }
            }
        }

        if ( config_s ) {
            pfree(config_s);
        }

    }

    /* clean up memory */
    SPI_freetuptable(SPI_tuptable);

    if ( rv ) {
        rv = Validate();
    }

    return rv;
}

/**
 * @brief Validate the document configuration, checking resources and attributes according to global options.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::DocumentConfig::Validate()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    bool rv = true;

    for ( std::map<std::string, pg_jsonapi::ResourceConfig>::const_iterator res = resources_.begin(); res != resources_.end(); ++res ) {
        ereport(DEBUG3, (errmsg_internal("jsonapi: %s res=%s", __FUNCTION__, res->first.c_str())));

        for ( ResourceConfig::RelationshipMap::const_iterator rel = GetResource(res->first).GetRelationships().begin(); rel != GetResource(res->first).GetRelationships().end(); ++rel ) {
            ereport(DEBUG3, (errmsg_internal("jsonapi: %s res=%s rel=%s", __FUNCTION__, res->first.c_str(), rel->first.c_str())));

            if ( 0 == resources_.count( GetResource(res->first).GetFieldResourceType(rel->first) ) ) {
                if ( HasTypeRestriction() ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s' is not configured for '%s'",
                               GetResource(res->first).GetFieldResourceType(rel->first).c_str(), base_url_.c_str());
                    rv = false;
                } else {
                    Resource( GetResource(res->first).GetFieldResourceType(rel->first) );
                }
            }
        }
    }

    if ( rv ) {
        for ( std::map<std::string, pg_jsonapi::ResourceConfig>::const_iterator res = resources_.begin(); res != resources_.end(); ++res ) {
            if ( ! Resource(res->first)->ValidatePG(false) ) {
                rv = false;
            }
        }
    }

    is_valid_ = rv;

    return rv;
}

/**
 * @brief Validate a request against the document configuration.
 *
 * @return @li true if operation succeeds
 *         @li false if an error occurs
 */
bool pg_jsonapi::DocumentConfig::ValidateRequest(const std::string& a_type, const std::string& a_related)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    if ( false == is_valid_ ) {
        g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "invalid configuration for '%s'",
                   base_url_.c_str());
        return false;
    }

    if ( HasTypeRestriction() ) {
        if ( ! resources_.count(a_type) ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' is not configured for '%s'",
                       a_type.c_str(), base_url_.c_str());
            return false;
        }
    }
    ResourceConfig* rc = Resource(a_type);
    if ( ! rc->ValidatePG(true) ) {
        return false;
    }
    if ( a_related.size() ) {
        if ( ! rc->IsRelationship(a_related) ) {
            g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA011"), E_HTTP_BAD_REQUEST).SetMessage(NULL, "resource '%s' does not have a related '%s' field for '%s'",
                       a_type.c_str(), a_related.c_str(), base_url_.c_str());
            return false;
        }
    }

    for ( ResourceConfig::RelationshipMap::const_iterator rel = rc->GetRelationships().begin(); rel != rc->GetRelationships().end(); ++rel ) {
        ereport(DEBUG3, (errmsg_internal("jsonapi: %s res=%s rel=%s", __FUNCTION__, a_type.c_str(), rel->first.c_str())));

        const std::string& rel_type = rc->GetFieldResourceType(rel->first);
        if ( HasTypeRestriction() ) {
            if ( 0 == resources_.count( rel_type ) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s' is not configured for '%s'",
                                                                                                        rel_type.c_str(), base_url_.c_str());
                return false;
            }
        }
        if ( ! Resource(rel_type)->ValidatePG(true) ) {
            return false;
        }
    }

    for ( StringMap::const_iterator stat = rc->GetObserved().begin(); stat != rc->GetObserved().end(); ++stat ) {
        ereport(DEBUG3, (errmsg_internal("jsonapi: %s res=%s stat=%s", __FUNCTION__, a_type.c_str(), stat->first.c_str())));
        bool stat_type_is_valid = ( stat->first == a_type);

        for ( ResourceConfig::RelationshipMap::const_iterator rel = rc->GetRelationships().begin(); !stat_type_is_valid && rel != rc->GetRelationships().end(); ++rel ) {
            ereport(DEBUG3, (errmsg_internal("jsonapi: %s res=%s rel=%s", __FUNCTION__, a_type.c_str(), rel->first.c_str())));
            stat_type_is_valid = ( stat->first == rc->GetFieldResourceType(rel->first) );
        }
        if ( !stat_type_is_valid ) {
            if ( HasTypeRestriction() ) {
                if ( 0 == resources_.count( stat->first ) ) {
                    g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA017"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "resource '%s' is not configured for '%s'",
                                                                                                            stat->first.c_str(), base_url_.c_str());
                    return false;
                }
            }
            if ( ! Resource(stat->first)->ValidatePG(true) ) {
                return false;
            }
        }
    }

    return true;
}
