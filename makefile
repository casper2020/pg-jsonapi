#
# Copyright (c) 2011-2018 Cloudware S.A. All rights reserved.
#
# This file is part of pg-jsonapi.
#
# pg-jsonapi is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# pg-jsonapi is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with pg-jsonapi.  If not, see <http://www.gnu.org/licenses/>.
#

.PHONY: debug

LIB_NAME:= pg-jsonapi
ifndef LIB_VERSION
	LIB_VERSION := 2.4.2
endif

include settings.mk

ifneq (Darwin,$(shell uname -s))
  SO_NAME = $(LIB_NAME).so.$(LIB_VERSION)
  LINK_FLAGS += -Wl,-soname,$(SO_NAME) -Wl,-z,relro -Bsymbolic
endif
$(shell sed -e s#@VERSION@#${LIB_VERSION}#g pg-jsonapi.control.tpl > pg-jsonapi.control)

RAGEL=ragel

RAGEL_FILES=src/query_builder.rl src/operation_request.rl
SRC_FILES=src/pg_jsonapi.cc json/jsoncpp.cc src/document_config.cc src/error_code.cc src/error_object.cc src/resource_config.cc src/resource_data.cc src/observed_stat.cc src/utils_adt_json.cc
OBJS=$(SRC_FILES:.cc=.o) $(RAGEL_FILES:.rl=.o)

#%.o:%.cc
#	$(CC) -c $(CFLAGS) $(CXXFLAGS) $(OTHER_CFLAGS) $< -o $@

%.cc: %.rl
	$(RAGEL) $< -G2 -o $@

%.bc : %.cc
	$(COMPILE.cxx.bc) $(CCFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

EXTENSION   := $(LIB_NAME)
EXTVERSION  := $(LIB_VERSION)
SHLIB_LINK  := -lstdc++ $(LINK_FLAGS)
PG_CPPFLAGS := -fPIC $(CFLAGS) $(CXXFLAGS) $(OTHER_CFLAGS)
MODULE_big  := $(LIB_NAME)
PG_CONFIG   ?= pg_config
EXTRA_CLEAN := $(RAGEL_FILES:.rl=.cc) $(LIB_NAME).so*

PGXS        := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
SHLIB_LINK  += -L/Applications/casper.app/Contents/MacOS/openssl/lib

debug:
	xcodebuild -configuration Debug
	make install
