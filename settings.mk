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

# forcing GCC_ENABLE_CPP_EXCEPTIONS to NO, because of PostgreSQL guidelines for C++ extensions
#ifeq ($(GCC_ENABLE_CPP_EXCEPTIONS),NO)
#CXXFLAGS+=
#endif

ifdef GCC_OPTIMIZATION_LEVEL
CFLAGS+= -O$(GCC_OPTIMIZATION_LEVEL)
endif

CFLAGS+= -DLIB_VERSION=\"$(LIB_VERSION)\"

ifdef GCC_GENERATE_DEBUGGING_SYMBOLS
CFLAGS+= -g
endif

## warning level in paranoid mode
CFLAGS += -Wall -W -Wextra -Wunused -Wpointer-arith -Wmissing-declarations -Wmissing-noreturn -Winline
CFLAGS += -Wno-unused-parameter
ifeq ($(shell uname -s),Darwin)
  CFLAGS += -Wbad-function-cast -Wstrict-prototypes -Wmissing-prototypes -Wnested-externs -Wcast-qual -Wredundant-decls -Wno-deprecated-register
  CLANG_CXX_LANGUAGE_STANDARD = c++11
else
  CFLAGS += -DENABLE_NLS=1
  CXXFLAGS += -std=c++11
endif


ifeq ($(GCC_WARN_SHADOW),YES)
CFLAGS+= -Wshadow
endif

