/**
 * @file resource_data.h declaration of ResourceData
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
#ifndef CLD_PG_JSONAPI_RESOURCE_DATA_H
#define CLD_PG_JSONAPI_RESOURCE_DATA_H

#include <stdlib.h>
#include <string>
#include <set>
#include <map>

#include "json/json.h"

extern "C" {
#include "postgres.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#include "executor/spi.h"
#pragma GCC diagnostic pop
} // extern "C"

#include "resource_config.h"

namespace pg_jsonapi
{
    class ResourceItem
    {
    public:
        const char*     id_;
        std::string     internal_id_;
        bool            serialized_;
        HeapTuple       res_tuple_;
        StringVectorMap relationships_;

    public: // Methods
        ResourceItem ();
        virtual ~ResourceItem ();
    };

    typedef std::vector<ResourceItem>      ResourceItemVector;
    typedef std::map<std::string, uint32>  IdIndexMap;

    /**
     * @brief Resource data obtained from postgresql database.
     */
    class ResourceData
    {
    public:
        uint32             processed_;     // SPI_processed
        TupleDesc          tupdesc_;       // SPI_tuptable->tupdesc
        ResourceItemVector items_;         // item per returned row
        IdIndexMap         id_index_;      // id position in vector
        StringSet          requested_ids_; // all requested ids
        StringSet          processed_ids_; // all processed ids
        StringSetMap       inclusion_path_;
        uint32             top_processed_; // SPI_processed as top resources

    public: // Methods
        ResourceData ();
        virtual ~ResourceData ();
    };

    typedef std::map<std::string, ResourceData> ResourceDataMap;

} // namespace pg_jsonapi

#endif // CLD_PG_JSONAPI_RESOURCE_DATA_H
