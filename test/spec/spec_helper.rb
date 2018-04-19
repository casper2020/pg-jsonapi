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

require 'rack/test'
require 'sinatra'

require File.expand_path '../../index.rb', __FILE__
require File.expand_path '../jsonapi_response.rb', __FILE__

ENV['RACK_ENV'] = 'test'
ENV['SERVER_PORT'] = '9002'
ENV['SERVER_NAME'] = 'localhost'

module RSpecMixin
  include Rack::Test::Methods
  include Cld::PgJsonApi::TopLevelResponse

  def app() Cld::PgJsonApi::ApiTester end # Sinatra::Application end
end

RSpec.configure do |config|

  config.include RSpecMixin

  # ## Mock Framework
  #
  # If you prefer to use mocha, flexmock or RR, uncomment the appropriate line:
  #
  # config.mock_with :mocha
  # config.mock_with :flexmock
  # config.mock_with :rr

  # Run specs in random order to surface order dependencies. If you find an
  # order dependency and want to debug it, you can fix the order by providing
  # the seed, which is printed after each run.
  #     --seed 1234
  # config.order = "random"
end

