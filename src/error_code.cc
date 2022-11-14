/**
 * @file error_code.cc Implementation of ErrorCode.
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

#include "query_builder.h"
#include "error_code.h"

extern pg_jsonapi::QueryBuilder* g_qb;

namespace pg_jsonapi {

    static ErrorCodeMessage g_jsonpi_error_messages[] = {
        // default error
         { "JA000", E_HTTP_BAD_REQUEST,           "Erro de sistema JA000. Por favor contacte o suporte técnico." }                              // generic error

        // default errors to be used per HTTP status codes
        ,{ "JA001", E_HTTP_NOT_FOUND,             "Erro de sistema JA001: recurso não existente. Por favor contacte o suporte técnico." }       // not found
        ,{ "JA002", E_HTTP_FORBIDDEN,             "Erro de sistema JA002: pedido não suportado. Por favor contacte o suporte técnico." }        // unsupported request
        ,{ "JA003", E_HTTP_CONFLICT,              "Erro de sistema JA003: conflito de acesso. Por favor contacte o suporte técnico." }          // conflict
        ,{ "JA004", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema JA004: erro interno no servidor. Por favor contacte o suporte técnico." }    // internal server error
        ,{ "JA005", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema JA005: erro interno na base de dados. Por favor contacte o suporte técnico." }  // postgresql error
        ,{ "JA006", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema JA006: erro interno na base de dados. Por favor contacte o suporte técnico." }  // postgresql exception

         // specific jsonapi error codes
        ,{ "JA010", E_HTTP_BAD_REQUEST,           "Erro de sistema JA010: argumentos inválidos. Por favor contacte o suporte técnico." }        // invalid arguments
        ,{ "JA011", E_HTTP_BAD_REQUEST,           "Erro de sistema JA011: pedido inválido. Por favor contacte o suporte técnico." }             // invalid request
        ,{ "JA012", E_HTTP_BAD_REQUEST,           "Erro de sistema JA012: método inválido. Por favor contacte o suporte técnico." }             // invalid method
        ,{ "JA013", E_HTTP_BAD_REQUEST,           "Erro de sistema JA013: pedido inválido. Por favor contacte o suporte técnico." }             // invalid path
        ,{ "JA014", E_HTTP_BAD_REQUEST,           "Erro de sistema JA014: pedido incompleto. Por favor contacte o suporte técnico." }           // missing body
        ,{ "JA015", E_HTTP_NOT_FOUND,             "Erro de sistema JA015: recurso não existente. Por favor contacte o suporte técnico." }       // data not found
        ,{ "JA016", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema JA016: dados inconsistentes. Por favor contacte o suporte técnico." }        // inconsistent data
        ,{ "JA017", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema JA017: configuração inválida. Por favor contacte o suporte técnico." }       // invalid configuration
        ,{ "JA018", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema JA018: pedido inválido. Por favor contacte o suporte técnico." }             // unsupported request detected when serializing response
        ,{ "JA019", E_HTTP_BAD_REQUEST,           "Erro de sistema JA019: demasiados resultados na resposta de topo. Por favor utilize menos items por página." }       // too many resources on top level
        ,{ "JA020", E_HTTP_BAD_REQUEST,           "Erro de sistema JA020: demasiados resultados nas relações a incluir. Por favor utilize menos items por página." }    // too many items on resource to be included

        // XssAttack to be used on xss_validators // validators_setting_[E_DB_CONFIG_XSS]
        ,{ "JA101", E_HTTP_BAD_REQUEST,           "Texto inválido num dos campos." }                                                            // invalid request XssAttack
        // SQL read injection to be used on sql_validators // validators_setting_ E_DB_CONFIG_SQL_WHITELIST / E_DB_CONFIG_SQL_BLACKLIST
        ,{ "JA102", E_HTTP_BAD_REQUEST,           "Texto inválido num dos campos." }                                                            // invalid request SQL read injection

        // default error to be used per HTTP status codes
        // http://www.postgresql.org/docs/9.4/static/errcodes-appendix.html

        // Class 23 — Integrity Constraint Violation
        ,{ "23000", E_HTTP_BAD_REQUEST,           "Erro de restrição numa tabela relacionada." }                // integrity_constraint_violation
        ,{ "23001", E_HTTP_BAD_REQUEST,           "O valor indicado não é válido para o campo." }               // restrict_violation
        ,{ "23502", E_HTTP_BAD_REQUEST,           "O campo não pode ser vazio." }                               // not_null_violation
        ,{ "23503", E_HTTP_BAD_REQUEST,           "Erro de restrição numa tabela relacionada." }                // foreign_key_violation
        ,{ "23505", E_HTTP_BAD_REQUEST,           "O valor indicado já existe na tabela." }                     // unique_violation
        ,{ "23514", E_HTTP_BAD_REQUEST,           "O valor indicado não respeita as regras de validação." }     // check_violation

        // Class P0 — PL/pgSQL Error
        ,{ "P0000", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema P0000: erro interno na base de dados. Por favor contacte o suporte técnico." }  // plpgsql_error
        ,{ "P0001", E_HTTP_INTERNAL_SERVER_ERROR, "Erro de sistema P0001: erro interno na base de dados. Por favor contacte o suporte técnico." }  // raise_exception
        ,{ "P0002", E_HTTP_NOT_FOUND,             "Não existem dados." }        // no_data_found
        ,{ "P0003", E_HTTP_INTERNAL_SERVER_ERROR, "Existem dados repetidos." }  // too_many_rows

        // Class 57 — Operator Intervention
        ,{ "57000", E_HTTP_INTERNAL_SERVER_ERROR, "Não é possível executar a operação neste momento. Por favor tente mais tarde." }  // operator_intervention
        ,{ "57014", E_HTTP_INTERNAL_SERVER_ERROR, "Não é possível executar a operação neste momento. Por favor tente mais tarde." }  // query_canceled
        ,{ "57P01", E_HTTP_INTERNAL_SERVER_ERROR, "Não é possível executar a operação neste momento. Por favor tente mais tarde." }  // admin_shutdown
        ,{ "57P02", E_HTTP_INTERNAL_SERVER_ERROR, "Não é possível executar a operação neste momento. Por favor tente mais tarde." }  // crash_shutdown
        ,{ "57P03", E_HTTP_INTERNAL_SERVER_ERROR, "Não é possível executar a operação neste momento. Por favor tente mais tarde." }  // cannot_connect_now
        ,{ "57P04", E_HTTP_INTERNAL_SERVER_ERROR, "Não é possível executar a operação neste momento. Por favor tente mais tarde." }  // database_dropped

    };

}

pg_jsonapi::ErrorCode::ErrorCodeDetailMap pg_jsonapi::ErrorCode::sql_error_map_;

pg_jsonapi::ErrorCode::ErrorCode()
{
    size_t          entries = sizeof(g_jsonpi_error_messages) / sizeof(g_jsonpi_error_messages[0]);
    ErrorCodeDetail   err_info;

    if ( 0 == sql_error_map_.size() ) {
        for (size_t i = 0; i < entries; i++ ) {
            if ( 5 != strlen(g_jsonpi_error_messages[i].sqlerrcode_) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA004"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "error code '%s' is invalid", g_jsonpi_error_messages[i].sqlerrcode_);
            }
            if ( 5 != strlen(g_jsonpi_error_messages[i].sqlerrcode_) ) {
                g_qb->AddError(JSONAPI_MAKE_SQLSTATE("JA004"), E_HTTP_INTERNAL_SERVER_ERROR).SetMessage(NULL, "error code '%s' is invalid", g_jsonpi_error_messages[i].sqlerrcode_);
            }
            err_info.status_  = g_jsonpi_error_messages[i].status_;
            err_info.message_ = g_jsonpi_error_messages[i].message_;
            sql_error_map_[ JSONAPI_MAKE_SQLSTATE(g_jsonpi_error_messages[i].sqlerrcode_) ] = err_info;
        }
    }

}

pg_jsonapi::ErrorCode::~ErrorCode()
{

}
