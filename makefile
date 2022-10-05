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

####################
# PLATFORM
####################
PLATFORM:=$(shell uname -s)
MACHINE_ARCH:=$(shell uname -m)

####################
# ARCH
####################

PRJ_ARCH?=$(MACHINE_ARCH)
MULTI_ARCH_BUILD_MACHINE?=false

####################
# Set target type
####################
ifeq (Darwin, $(PLATFORM))
 ifndef TARGET
   TARGET:=Debug
 else
   override TARGET:=$(shell echo $(TARGET) | tr A-Z a-z)
   $(eval TARGET_E:=$$$(TARGET))
   TARGET_E:=$(shell echo $(TARGET_E) | tr A-Z a-z )
   TARGET_S:=$(subst $(TARGET_E),,$(TARGET))
   TARGET_S:=$(shell echo $(TARGET_S) | tr a-z A-Z )
   TMP_TARGET:=$(TARGET_S)$(TARGET_E)
   override TARGET:=$(TMP_TARGET)
 endif
else
 ifndef TARGET
   TARGET:=debug
  else
   override TARGET:=$(shell echo $(TARGET) | tr A-Z a-z)
  endif
endif
TARGET_LC:=$(shell echo $(TARGET) | tr A-Z a-z)

# validate target
ifeq (release, $(TARGET_LC))
  #
else
  ifeq (debug, $(TARGET_LC))
    #
  else
    $(error "Don't know how to build for target $(TARGET_LC) ")
  endif
endif

### ... ###

LIB_NAME:= pg-jsonapi
ifndef LIB_VERSION
	LIB_VERSION := 2.5.10
endif

include settings.mk

ifeq (Darwin,$(PLATFORM))
  CFLAGS+= -DDEVELOPER_MODE=1
  ARCH_CFLAGS=-arch $(PRJ_ARCH)
  ARCH_CXXFLAGS=-arch $(PRJ_ARCH)
  ARCH_LDFLAGS=-m64 -arch $(PRJ_ARCH)
  ifeq (true, $(MULTI_ARCH_BUILD_MACHINE))
    OPENSSL_VERSION:=$(shell cat ../casper-packager/openssl/version | tr -dc '0-9.' | cut -d'.' -f1-2)
    OPENSSL_DIR:=/usr/local/casper/openssl/$(PRJ_ARCH)/$(TARGET)/$(OPENSSL_FULL_VERSION)
  else
    OPENSSL_DIR:=/Applications/casper.app/Contents/MacOS/openssl/lib
  endif
  OPENSSL_LDFLAGS:=-L$(OPENSSL_DIR)
else
  SO_NAME = $(LIB_NAME).so.$(LIB_VERSION)
  LINK_FLAGS += -Wl,-soname,$(SO_NAME) -Wl,-z,relro -Bsymbolic
endif
$(shell sed -e s#@VERSION@#${LIB_VERSION}#g pg-jsonapi.control.tpl > pg-jsonapi.control)

RAGEL:=$(shell which ragel)

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
PG_CPPFLAGS := -fPIC $(CFLAGS) $(CXXFLAGS) $(OTHER_CFLAGS) $(ARCH_CFLAGS)
MODULE_big  := $(LIB_NAME)
PG_CONFIG   ?= pg_config
EXTRA_CLEAN := $(RAGEL_FILES:.rl=.cc) $(LIB_NAME).so*

PGXS        := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

## OVERRIDE sysroot ##
ifeq (Darwin,$(PLATFORM))
  XCODE_APP_DIR:=$(shell echo "${XCODE_APP_DIR}")
  # only if XCODE_APP_DIR is not set ( it should only be set on beta machines )
  ifndef XCODE_APP_DIR
    override CPPFLAGS := $(subst Xcode-beta,Xcode,$(CPPFLAGS))
  endif
endif

SHLIB_LINK  += $(OPENSSL_LDFLAGS)

# developer
dev:
	xcodebuild -configuration Debug
	make install

# so
so:
	@echo "* $(PLATFORM) $(TARGET) $(PRJ_ARCH) rebuild..."
	@make -f makefile clean all

# debug
debug:
	@make -f makefile TARGET=debug so
ifeq (Darwin, $(PLATFORM))
	@lipo -info $(LIB_NAME).so
endif

# release
release:
	@echo "* $(PLATFORM) $(TARGET) $(PRJ_ARCH) rebuild..."
	@make -f makefile TARGET=release so
ifeq (Darwin, $(PLATFORM))
	@lipo -info $(LIB_NAME).so
endif
