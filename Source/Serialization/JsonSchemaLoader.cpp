
#include "JsonSchemaLoader.hpp"

#include "../Expression.hpp"
#include "../Utilities.hpp"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <fstream>
#include <functional>
#include <string>
#include <vector>

using boss::utilities::operator""_;

namespace boss::serialization {

class JsonEventConsumer : public json::json_sax_t {
public:
  explicit JsonEventConsumer(std::function<void(Expression const&)>&& evaluate)
      : m_evaluateFunc(evaluate) {
    m_parseStack.push_back(PS_Root);
  }

  bool null() override {
    switch(m_parseStack.back()) {
    default:
      throw std::logic_error("unexpected null in " + ParseStateName(m_parseStack.back()));
    }

    m_parseStack.pop_back();
    return true;
  }

  bool boolean(bool /*b*/) override {
    switch(m_parseStack.back()) {
    default:
      throw std::logic_error("unexpected boolean type in " + ParseStateName(m_parseStack.back()));
    }

    m_parseStack.pop_back();
    return true;
  }

  bool number_integer(number_integer_t /*i*/) override {
    switch(m_parseStack.back()) {
    default:
      throw std::logic_error("unexpected number type in " + ParseStateName(m_parseStack.back()));
    }

    m_parseStack.pop_back();
    return true;
  }

  bool number_unsigned(number_unsigned_t /*u*/) override {
    switch(m_parseStack.back()) {
    default:
      throw std::logic_error("unexpected unsigned number type in " +
                             ParseStateName(m_parseStack.back()));
    }

    m_parseStack.pop_back();
    return true;
  }

  bool number_float(number_float_t /*val*/, const string_t& /*s*/) override {
    switch(m_parseStack.back()) {
    default:
      throw std::logic_error("unexpected float type in " + ParseStateName(m_parseStack.back()));
    }

    m_parseStack.pop_back();
    return true;
  }

  bool binary(json::binary_t& /*val*/) override {
    switch(m_parseStack.back()) {
    default:
      throw std::logic_error("unexpected binary type in " + ParseStateName(m_parseStack.back()));
    }

    m_parseStack.pop_back();
    return true;
  }

  bool string(string_t& str) override {
    switch(m_parseStack.back()) {
    case PS_TableName: {
      m_table.name = str;
    } break;

    case PS_ColumnName: {
      column.name = str;
    } break;

    case PS_Datatype: {
      column.datatype.name = str;
    } break;

    case PS_DataTypeLength: {
      column.datatype.length = std::stoi(str);
    } break;

    case PS_ForeignKeyReferenceTable: {
      column.reference.table = str;
    } break;

    case PS_ForeignKeyReferenceColumn: {
      column.reference.column = str;
    } break;

    default:
      throw std::logic_error("unexpected string type in " + ParseStateName(m_parseStack.back()));
    }

    m_parseStack.pop_back();
    return true;
  }

  bool start_object(std::size_t /*elements*/) override {
    switch(m_parseStack.back()) {
    case PS_Root:
    case PS_TableArray: {
      m_parseStack.push_back(PS_Table);
    } break;

    case PS_TableColumns:
    case PS_PrimaryKeyColumns:
    case PS_ForeignKeyColumns: {
      m_parseStack.push_back(PS_Column);
    } break;

    case PS_TablePrimaryKey: {
      m_parseStack.back() = PS_PrimaryKey;
    } break;

    case PS_TableForeignKey: {
      m_parseStack.back() = PS_ForeignKey;
    } break;

    case PS_ColumnType: {
      m_parseStack.back() = PS_ColumnTypeAttributes;
    } break;

    case PS_TableOptions: {
      m_parseStack.back() = PS_TableOptionsAttributes;
    } break;

    case PS_ColumnOptions: {
      m_parseStack.back() = PS_ColumnOptionsAttributes;
    } break;

    default:
      throw std::logic_error("unexpected object in " + ParseStateName(m_parseStack.back()));
    }

    return true;
  }

  bool end_object() override {
    ParseState ObjectState = m_parseStack.back();
    m_parseStack.pop_back();

    switch(ObjectState) {
    case PS_Root: {
      // nothing to do
    } break;

    case PS_Table: {
      Symbol tableSymbol(m_table.name);
      auto createTable = "CreateTable"_(tableSymbol);

      m_evaluateFunc(createTable);

      for(auto const& column : m_table.columns) {
        // TODO: mark specific type to columns
        // check column.datatype.name with "INTEGER", etc
        auto addColumn = "AddColumn"_(tableSymbol, column.name);
        m_evaluateFunc(addColumn);
      }

      m_table.clear();
    } break;

    case PS_Column: {
      switch(m_parseStack.back()) {
      case PS_TableColumns: {
        m_table.columns.push_back(column);
        column.clear();
      } break;

      case PS_PrimaryKeyColumns: {
        m_table.primaryColumns.push_back(column);
        column.clear();
      } break;

      case PS_ForeignKeyColumns: {
        m_table.foreignColumns.push_back(column);
        column.clear();
      } break;

      default:
        throw std::logic_error("unexpected state " + ParseStateName(m_parseStack.back()));
      }
    } break;

    // nothing to do for column type
    case PS_ColumnTypeAttributes:
    // nothing to handle for now
    case PS_TableOptionsAttributes:
    case PS_ColumnOptionsAttributes:
    // TODO: handle primary/foreign key
    case PS_PrimaryKey:
    case PS_ForeignKey:
      break;

    default:
      throw std::logic_error("unexpected end of object in " + ParseStateName(m_parseStack.back()));
    }

    return true;
  }

  bool start_array(std::size_t /*elements*/) override {
    switch(m_parseStack.back()) {
    case PS_Root: {
      m_parseStack.back() = PS_TableArray;
    } break;

    case PS_TableColumns:
    case PS_PrimaryKeyColumns:
    case PS_ForeignKeyColumns: {
      // nothing to do
    } break;

    default:
      throw std::logic_error("unexpected array in " + ParseStateName(m_parseStack.back()));
    }

    return true;
  }

  bool end_array() override {
    switch(m_parseStack.back()) {
    case PS_TableArray:
    case PS_TableColumns:
    case PS_PrimaryKeyColumns:
    case PS_ForeignKeyColumns: {
      m_parseStack.pop_back();
    } break;

    default:
      throw std::logic_error("unexpected end of array in " + ParseStateName(m_parseStack.back()));
    }

    return true;
  }

  bool key(string_t& str) override {
    switch(m_parseStack.back()) {
    case PS_Table: {
      if(str == "name") {
        m_parseStack.push_back(PS_TableName);
      } else if(str == "options") {
        m_parseStack.push_back(PS_TableOptions);
      } else if(str == "columns") {
        m_parseStack.push_back(PS_TableColumns);
      } else if(str == "primaryKey") {
        m_parseStack.push_back(PS_TablePrimaryKey);
      } else if(str == "foreignKey") {
        m_parseStack.push_back(PS_TableForeignKey);
      } else {
        throw std::logic_error("unknown table attribute " + str);
      }
    } break;

    case PS_Column: {
      if(str == "name") {
        m_parseStack.push_back(PS_ColumnName);
      } else if(str == "options") {
        m_parseStack.push_back(PS_ColumnOptions);
      } else if(str == "type") {
        m_parseStack.push_back(PS_ColumnType);
      } else if(str == "reference table") {
        m_parseStack.push_back(PS_ForeignKeyReferenceTable);
      } else if(str == "reference column") {
        m_parseStack.push_back(PS_ForeignKeyReferenceColumn);
      } else {
        throw std::logic_error("unknown column attribute " + str);
      }
    } break;

    case PS_PrimaryKey: {
      if(str == "columns") {
        m_parseStack.push_back(PS_PrimaryKeyColumns);
      } else {
        throw std::logic_error("unknown primary key attribute " + str);
      }
    } break;

    case PS_ForeignKey: {
      if(str == "columns") {
        m_parseStack.push_back(PS_ForeignKeyColumns);
      } else {
        throw std::logic_error("unknown foreign key attribute " + str);
      }
    } break;

    case PS_ColumnTypeAttributes: {
      if(str == "datatype") {
        m_parseStack.push_back(PS_Datatype);
      } else if(str == "length") {
        m_parseStack.push_back(PS_DataTypeLength);
      } else {
        throw std::logic_error("unknown column type attribute " + str);
      }
    } break;

    default:
      throw std::logic_error("unexpected key in " + ParseStateName(m_parseStack.back()));
    }

    return true;
  }

  bool parse_error(std::size_t /*position*/, const std::string& /*last_token*/,
                   const json::exception& ex) override {
    throw ex;
  }

private:
  std::function<void(Expression const&)> m_evaluateFunc;

  enum ParseState {
    PS_Root,
    PS_TableArray,

    PS_Table,
    PS_TableOptions,
    PS_TableName,
    PS_TableColumns,
    PS_TablePrimaryKey,
    PS_TableForeignKey,

    PS_Column,
    PS_ColumnName,
    PS_ColumnOptions,
    PS_ColumnType,

    PS_ColumnTypeAttributes,
    PS_Datatype,
    PS_DataTypeLength,

    PS_TableOptionsAttributes,
    PS_ColumnOptionsAttributes,

    PS_PrimaryKey,
    PS_PrimaryKeyColumns,
    PS_PrimaryKeyColumn,

    PS_ForeignKey,
    PS_ForeignKeyColumns,
    PS_ForeignKeyColumn,

    PS_ForeignKeyReferenceTable,
    PS_ForeignKeyReferenceColumn,
  };

  struct JsonColumn {
    JsonColumn() = default;
    void clear() { name.clear(); }

    std::string name;

    struct Datatype {
      Datatype() : length(0) {}
      std::string name;
      size_t length;
    } datatype; // for table column

    struct Reference {
      Reference() = default;
      std::string table;
      std::string column;
    } reference; // for foreign key column
  } column;

  struct Table {
    Table() = default;

    void clear() {
      name.clear();
      columns.clear();
      primaryColumns.clear();
      foreignColumns.clear();
    }

    std::string name;
    std::vector<JsonColumn> columns;
    std::vector<JsonColumn> primaryColumns;
    std::vector<JsonColumn> foreignColumns;
  };

  Table m_table;
  std::vector<ParseState> m_parseStack;

  static std::string const& ParseStateName(ParseState parseState) {
    static std::map<ParseState, std::string> nameMapping = {
        {PS_Root, "Root"},
        {PS_TableArray, "TableArray"},

        {PS_Table, "Table"},
        {PS_TableOptions, "TableOptions"},
        {PS_TableName, "TableName"},
        {PS_TableColumns, "TableColumns"},
        {PS_TablePrimaryKey, "TablePrimaryKey"},
        {PS_TableForeignKey, "TableForeignKey"},

        {PS_Column, "Column"},
        {PS_ColumnName, "ColumnName"},
        {PS_ColumnOptions, "ColumnOptions"},
        {PS_ColumnType, "ColumnType"},

        {PS_ColumnTypeAttributes, "ColumnTypeAttributes"},
        {PS_Datatype, "Datatype"},
        {PS_DataTypeLength, "DataTypeLength"},

        {PS_TableOptionsAttributes, "TableOptionsAttributes"},
        {PS_ColumnOptionsAttributes, "ColumnOptionsAttributes"},

        {PS_PrimaryKey, "PrimaryKey"},
        {PS_PrimaryKeyColumns, "PrimaryKeyColumns"},
        {PS_PrimaryKeyColumn, "PrimaryKeyColumn"},

        {PS_ForeignKey, "ForeignKey"},
        {PS_ForeignKeyColumns, "ForeignKeyColumns"},
        {PS_ForeignKeyColumn, "ForeignKeyColumn"},

        {PS_ForeignKeyReferenceTable, "ForeignKeyReferenceTable"},
        {PS_ForeignKeyReferenceColumn, "ForeignKeyReferenceColumn"},
    };

    return nameMapping[parseState];
  }
};

JsonSchemaLoader::JsonSchemaLoader(std::string const& filepath) : m_iFileStream(filepath) {
  if(m_iFileStream.fail()) {
    throw std::runtime_error("failed to open " + filepath);
  }
}

bool JsonSchemaLoader::loadTables(std::function<void(Expression const&)>&& evaluate) {
  if(m_iFileStream.fail()) {
    throw std::runtime_error("file is not open");
  }

  JsonEventConsumer consumer(std::move(evaluate));
  json::sax_parse(m_iFileStream, &consumer);
  return true;
}

} // namespace boss::serialization
