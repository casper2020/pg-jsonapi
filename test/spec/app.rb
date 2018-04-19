# encoding: utf-8
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
require File.expand_path '../spec_helper.rb', __FILE__

describe "The JSON API" do

  it "should allow accessing some data" do
    get '/users'
    expect(last_response).to be_ok
    parsed_body = JSON.parse(last_response.body)
    expect(valid_top_data?(parsed_body)).to be true
  end

  it "should allow accessing errors" do
    get '/oops'
    expect(last_response).not_to be_ok
    parsed_body = JSON.parse(last_response.body)
    expect(valid_top_error?(parsed_body)).to be true
  end

end
