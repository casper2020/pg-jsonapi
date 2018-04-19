-- Copyright (c) 2011-2018 Cloudware S.A. All rights reserved.
--
-- This file is part of pg-jsonapi.
--
-- pg-jsonapi is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Affero General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- pg-jsonapi is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU Affero General Public License
-- along with pg-jsonapi.  If not, see <http://www.gnu.org/licenses/>.

CREATE OR REPLACE FUNCTION public.jsonapi (
  IN method               text,
  IN uri                  text,
  IN body                 text,
  IN user_id              text,
  IN company_id           text,
  IN company_schema       text,
  IN sharded_schema       text,
  IN accounting_schema    text,
  IN accounting_prefix    text,
  OUT http_status         integer,
  OUT response            text
) RETURNS record AS '$libdir/pg-jsonapi.so', 'jsonapi' LANGUAGE C;

CREATE OR REPLACE FUNCTION public.inside_jsonapi (
) RETURNS boolean AS '$libdir/pg-jsonapi.so', 'inside_jsonapi' LANGUAGE C;

CREATE OR REPLACE FUNCTION public.get_jsonapi_user (
) RETURNS text AS '$libdir/pg-jsonapi.so', 'get_jsonapi_user' LANGUAGE C;

CREATE OR REPLACE FUNCTION public.get_jsonapi_company (
) RETURNS text AS '$libdir/pg-jsonapi.so', 'get_jsonapi_company' LANGUAGE C;

CREATE OR REPLACE FUNCTION public.get_jsonapi_company_schema (
) RETURNS text AS '$libdir/pg-jsonapi.so', 'get_jsonapi_company_schema' LANGUAGE C;

CREATE OR REPLACE FUNCTION public.get_jsonapi_sharded_schema (
) RETURNS text AS '$libdir/pg-jsonapi.so', 'get_jsonapi_sharded_schema' LANGUAGE C;

CREATE OR REPLACE FUNCTION public.get_jsonapi_accounting_schema (
) RETURNS text AS '$libdir/pg-jsonapi.so', 'get_jsonapi_accounting_schema' LANGUAGE C;

CREATE OR REPLACE FUNCTION public.get_jsonapi_accounting_prefix (
) RETURNS text AS '$libdir/pg-jsonapi.so', 'get_jsonapi_accounting_prefix' LANGUAGE C;
