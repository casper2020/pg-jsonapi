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

CREATE TABLE IF NOT EXISTS public.jsonapi_config (
prefix varchar(64) PRIMARY KEY,
config text NOT NULL
);

DELETE FROM public.jsonapi_config WHERE prefix = 'http://documents.localhost:9002';

INSERT INTO public.jsonapi_config (prefix,config)
VALUES
('http://documents.localhost:9002','
{
    "compound" : true,
    "resources": [
        {"documents" : {
                        "to-many":[{"lines":{"pg-table":"document_lines","pg-parent-id":"document_id","pg-child-id":"id","resource":"document_lines"}}]                      }
                       }
    ]
}
');
