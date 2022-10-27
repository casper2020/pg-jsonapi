/**
 * @file observed_stat.h declaration of ObservedStat
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
#ifndef CLD_PG_JSONAPI_OBSERVED_STAT_H
#define CLD_PG_JSONAPI_OBSERVED_STAT_H

#include <stdlib.h>
#include <string>
#include <set>
#include <map>

#include "json/json.h"

extern "C" {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#include "postgres.h"
#include "executor/spi.h"
#pragma GCC diagnostic pop
} // extern "C"

#include "resource_config.h"

namespace pg_jsonapi
{

    class ObservedStat
    {
    private:
        Oid                relid_;
        int64              initial_inserted_;
        int64              initial_updated_;
        int64              initial_deleted_;
        int64              final_inserted_;
        int64              final_updated_;
        int64              final_deleted_;

    public: // Methods
        ObservedStat (Oid a_relid);
        virtual ~ObservedStat ();

        void  Init();
        void  CountChanges();
        int64 TotalChanges() const;

    };

    typedef std::map<std::string, ObservedStat> ObservedStatMap;
    typedef ObservedStatMap::iterator           ObservedStatMapIterator;

    inline int64 ObservedStat::TotalChanges () const
    {
        return ( final_inserted_ - initial_inserted_ + final_updated_ - initial_updated_ + final_deleted_ - initial_deleted_ );
    }

} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_OBSERVED_STAT_H
