/*
 * Copyright 2017-present Shawn Cao
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/Conv.h>
#include <folly/String.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include "common/Errors.h"
#include "surface/DataSurface.h"

/**
 * A wrapper for interacting with AWS / S3
 */
namespace nebula {
namespace storage {

class CsvRow : public nebula::surface::RowData {
public:
  CsvRow(char delimiter) : delimiter_{ delimiter } {}
  virtual ~CsvRow() = default;

  bool isNull(const std::string&) const override {
    // TODO(cao) - CSV reader doesn't produce null valus for now
    return false;
  }

#define CONV_TYPE_INDEX(TYPE, FUNC)                    \
  TYPE FUNC(const std::string& field) const override { \
    auto index = columnLookup_(field);                 \
    try {                                              \
      return folly::to<TYPE>(data_.at(index));         \
    } catch (std::exception & ex) {                    \
      return TYPE();                                   \
    }                                                  \
  }

  CONV_TYPE_INDEX(bool, readBool)
  CONV_TYPE_INDEX(int8_t, readByte)
  CONV_TYPE_INDEX(int16_t, readShort)
  CONV_TYPE_INDEX(int32_t, readInt)
  CONV_TYPE_INDEX(int64_t, readLong)
  CONV_TYPE_INDEX(float, readFloat)
  CONV_TYPE_INDEX(double, readDouble)

  std::string_view readString(const std::string& field) const override {
    auto index = columnLookup_(field);
    return data_.at(index);
  }

#undef CONV_TYPE_INDEX

  // compound types
  std::unique_ptr<nebula::surface::ListData> readList(const std::string&) const override {
    throw NException("Array not supported yet.");
  }

  std::unique_ptr<nebula::surface::MapData> readMap(const std::string&) const override {
    throw NException("Map not supported yet.");
  }

  void setData(const std::vector<std::string> data) {
    data_ = std::move(data);
  }

  void setSchema(const std::function<size_t(const std::string&)>& columnLookup) {
    columnLookup_ = columnLookup;
  }

public:
  void readNext(std::istream&);
  inline const std::vector<std::string>& rawData() const {
    return data_;
  }

private:
  char delimiter_;
  // reference a line generated by reader
  std::vector<std::string> data_;
  std::function<size_t(std::string)> columnLookup_;
};

// declare a operator to feed in stream to a csv row
std::istream& operator>>(std::istream&, CsvRow&);

class CsvReader : public nebula::surface::RowCursor {
public:
  CsvReader(const std::string& file, char delimiter = ',', const std::vector<std::string>& columns = {})
    : nebula::surface::RowCursor(0), fstream_{ file }, row_{ delimiter }, cacheRow_{ delimiter } {
    LOG(INFO) << "Reading a delimiter separated file: " << file << " by " << delimiter;
    // if the schema is given
    if (columns.size() > 0) {
      for (size_t i = 0, size = columns.size(); i < size; ++i) {
        columns_[columns.at(i)] = i;
      }
    } else if (fstream_ >> row_) {
      // row_ has headers - build the name-index mapping
      const auto& raw = row_.rawData();
      for (size_t i = 0, size = raw.size(); i < size; ++i) {
        columns_[raw.at(i)] = i;
      }
    }

    cacheRow_.setSchema([this](const std::string& name) -> size_t {
      return columns_.at(name);
    });

    // read one row
    if (fstream_ >> row_) {
      size_ = 1;
    }
  }

  virtual ~CsvReader() = default;

  // next row data of CsvRow
  virtual const nebula::surface::RowData& next() override {
    // consume a row and read a new row
    cacheRow_.setData(std::move(row_.rawData()));

    // read next row
    // TODO(cao) - current CSV reading implementation ignores escape special chars case
    // we should handle those to skip less rows
    while (fstream_ >> row_) {
      if (row_.rawData().size() == columns_.size()) {
        size_ += 1;
        break;
      }
    }

    index_++;
    return cacheRow_;
  }

  virtual std::unique_ptr<nebula::surface::RowData> item(size_t) const override {
    throw NException("CSV Reader does not support random access by row number");
  }

private:
  std::ifstream fstream_;
  CsvRow row_;
  CsvRow cacheRow_;
  std::unordered_map<std::string, size_t> columns_;
};

} // namespace storage
} // namespace nebula