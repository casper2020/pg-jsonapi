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

module Cld
  module PgJsonApi
    module TopLevelResponse

      def top_level_members
        [ "jsonapi", "links", "data", "included", "errors", "meta" ]
      end

      def valid_top_level?(doc)
        return false if ! doc.is_a? Hash
        return false if doc.empty?
        doc.keys.map { |k| top_level_members.include? k or return false }
        true
      end

      def valid_jsonapi?(doc)
        # JOANA TODO
        true
      end

      def valid_links?(doc)
        # JOANA TODO
        true
      end

      def data_members
        [ "type", "id", "attributes", "relationships", "links", "meta" ]
      end

      def valid_data_object?(obj)
        return false if ! obj.is_a? Hash
        return false if obj.empty?
        obj.keys.map { |k| data_members.include? k or return false }
        true
      end

      def valid_top_data?(doc)
        valid_top_level? doc or return false
        return false if ! doc.keys.include? "data"
        return false if doc.keys.include? "errors"
        return valid_data_object? doc["data"] if doc["data"].is_a? Hash
        return false if ! doc["data"].is_a? Array
        doc["data"].each { |d| return false if ! valid_data_object? d }
        true
      end

      def valid_top_data_collection?(doc)
        valid_top_level? doc or return false
        return false if ! doc.keys.include? "data"
        return false if doc.keys.include? "errors"
        return false if ! doc["data"].is_a? Array
        doc["data"].each { |d| return false if ! valid_data_object? d }
        true
      end

      def valid_top_data_resource?(doc)
        valid_top_level? doc or return false
        return false if ! doc.keys.include? "data"
        return false if doc.keys.include? "errors"
        return false if ! doc["data"].is_a? Hash
        return valid_data_object? doc["data"]
      end

      def valid_included?(doc)
        # JOANA TODO
        true
      end

      def error_members
        [ "id", "links", "status", "code", "title", "detail", "source", "meta" ]
      end

      def valid_error_object?(obj)
        return false if ! obj.is_a? Hash
        return false if obj.empty?
        obj.keys.map { |k| error_members.include? k or return false }
        true
      end

      def valid_top_error?(doc)
        valid_top_level? doc or return false
        return false if ! doc.keys.include? "errors"
        return false if doc.keys.include? "data"
        return false if ! doc["errors"].is_a? Array
        return false if doc["errors"].empty?
        doc["errors"].each { |e| return false if ! valid_error_object? e }
        true
      end

      def http_client_error?(code)
        ( code >= 400 && code <=499 )
      end

      def http_server_error?(code)
        ( code >= 500 && code <= 599 )
      end

      def valid_top_meta?(doc)
        valid_top_level? doc or return false
        return false if doc.keys.include? "errors"
        return false if doc.keys.include? "data"
        return false if ! doc.keys.include? "meta"
        true
      end

    end
  end
end
