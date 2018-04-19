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
require 'sinatra/base'
require 'json'
require 'yaml'
require 'pg'
require 'byebug'

require File.expand_path(File.join(File.dirname(__FILE__), 'utils'))

Bundler.require

set :logging, true
set :port, YAML.load_file(File.expand_path '../config.yml', __FILE__)['server']['port']

$pg_host= YAML.load_file(File.expand_path '../config.yml', __FILE__)['pg']['host']
$pg_port= YAML.load_file(File.expand_path '../config.yml', __FILE__)['pg']['port']
$pg_dbname= YAML.load_file(File.expand_path '../config.yml', __FILE__)['pg']['dbname']
$pg_user= YAML.load_file(File.expand_path '../config.yml', __FILE__)['pg']['user']

$db  = PG.connect( host: $pg_host, port: $pg_port, dbname: $pg_dbname, user: $pg_user)
# functions must have been created using config/create_functions.sql

module Cld
  module PgJsonApi
    class ApiTester < Sinatra::Application

      def select_jsonapi(method,url,body)
        res = $db.exec("SELECT * FROM jsonapi('#{method}','#{url}','#{body}','','')")
        [
          if res[0]['jsonapi'].start_with?('{"errors":')
            JSON.parse(res[0]['jsonapi'])['errors'].map { |error| error['status'].to_i }.max
          else
            200
          end,
          res[0]['jsonapi']
        ]
      end

      def process_request
        content_type 'application/vnd.api+json', :charset => 'utf-8'
        begin
          select_jsonapi(request.request_method, request.url, request.body.read)
          rescue Exception => e
          [ 500,  { errors: [ {status: '500', code: e.message } ],
                    links: { self: request.url },
                    jsonapi: { version: '1.0' }
                    }.to_json ]
        end
      end

      get '/*' do
        process_request
      end

      post '/*' do
        process_request
      end

      put '/*' do
        process_request
      end

      patch '/*' do
        process_request
      end

      delete '/*' do
        process_request
      end

    end
  end
end

