/**
 * @file observed_stat.cc Implementation of ObservedStat
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

#include "observed_stat.h"

extern "C" {
#include "pgstat.h"
extern PgStat_TableStatus *find_tabstat_entry(Oid rel_id); //pg_stat.c
} // extern "C"

/**
 * @brief Constructor
 */
pg_jsonapi::ObservedStat::ObservedStat(Oid a_relid) : relid_(a_relid)
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    /*
     * Attribute defaults
     */
    Init();
}

/**
 * @brief Destructor
 */
pg_jsonapi::ObservedStat::~ObservedStat ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));
}

/**
 * @brief Reset this node, checking current values of inserted/updated/deleted rows on current transaction
 */
void pg_jsonapi::ObservedStat::Init ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    PgStat_TableStatus *tabentry = find_tabstat_entry(relid_);
    PgStat_TableXactStatus *trans = NULL;

    initial_inserted_ = 0;
    initial_updated_ = 0;
    initial_deleted_ = 0;

    if ( NULL != tabentry) {
        initial_inserted_ = tabentry->t_counts.t_tuples_inserted;
        for (trans = tabentry->trans; trans != NULL; trans = trans->upper)
            initial_inserted_ += trans->tuples_inserted;

        initial_updated_ = tabentry->t_counts.t_tuples_updated;
        for (trans = tabentry->trans; trans != NULL; trans = trans->upper)
            initial_updated_ += trans->tuples_updated;

        initial_deleted_ = tabentry->t_counts.t_tuples_deleted;
        for (trans = tabentry->trans; trans != NULL; trans = trans->upper)
            initial_deleted_ += trans->tuples_deleted;
    }

    final_inserted_ = initial_inserted_;
    final_updated_  = initial_updated_;
    final_deleted_  = initial_deleted_;
}

/**
* @brief Count inserted/updated/deleted rows changed since Init()
*/
void pg_jsonapi::ObservedStat::CountChanges ()
{
    ereport(DEBUG3, (errmsg_internal("jsonapi: %s", __FUNCTION__)));

    PgStat_TableStatus *tabentry = find_tabstat_entry(relid_);
    PgStat_TableXactStatus *trans = NULL;

    if ( NULL != tabentry) {
        final_inserted_ = tabentry->t_counts.t_tuples_inserted;
        for (trans = tabentry->trans; trans != NULL; trans = trans->upper)
            final_inserted_ += trans->tuples_inserted;

        final_updated_ = tabentry->t_counts.t_tuples_updated;
        for (trans = tabentry->trans; trans != NULL; trans = trans->upper)
            final_updated_ += trans->tuples_updated;

        final_deleted_ = tabentry->t_counts.t_tuples_deleted;
        for (trans = tabentry->trans; trans != NULL; trans = trans->upper)
            final_deleted_ += trans->tuples_deleted;
    }

    final_inserted_ -= initial_inserted_;
    final_updated_  -= initial_updated_;
    final_deleted_  -= initial_deleted_;
}
