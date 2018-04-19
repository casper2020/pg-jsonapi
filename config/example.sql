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

DELETE FROM public.jsonapi_config WHERE prefix = 'http://example.localhost:9002';

INSERT INTO public.jsonapi_config (prefix,config)
VALUES
('http://example.localhost:9002','
{
    "version": true,
    "compound": false,
    "links": false,
    "resources": [
        {"Articles": {"pg-table":"articles",
                      "pg-id":"id",
                      "attributes":[ {"Title":{"pg-column":"title"}},
                                      "subtitle"
                                   ],
                      "to-one":[{"author":{"resource":"people"}},
                                {"co-author":{"pg-child-id":"co_author_id","resource":"people"}},
                                {"preface":{"pg-table":"article_preface","pg-parent-id":"articles_id","pg-child-id":"people_id","resource":"people"}}],
                      "to-many":[{"comments":{"pg-table":"article_comments","pg-parent-id":"articles_id","pg-child-id":"comments_id","resource":"Comments"}}]
                     }
        },
        {"Comments": {"pg-table":"comments",
                      "pg-id":"id",
                      "to-one":[{"author":{"resource":"people"}}]
                     }
        },
        {"people"  : {"attributes":[{"FirstName":{"pg-column":"first_name"}},
                                    {"LastName":{"pg-column":"last_name"}},
                                    "twitter"
                                   ]
                      }
        }
    ]
}
');
