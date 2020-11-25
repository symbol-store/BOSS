#pragma once

#include "SymbolicData.hpp"

#include "../Utils/UniqueString.hpp"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/error/error.h"

#include "cpp-httplib/httplib.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace boss::engines::bulk {

template <typename TableMaterializedView, typename T> class FetchedData : public MissingData<T> {
public:
  FetchedData(std::string const& url, std::string const& command, std::string const& field)
      : MissingData<T>(), m_url(url), m_command(command), m_field(field) {}

  bool Evaluate(void const* view, size_t rowIndex) override {
    TableMaterializedView const& materializedView =
        *reinterpret_cast<TableMaterializedView const*>(view);

    std::string command = m_command;

    // replace each argument in the url by corresponding column
    size_t startArg = 0, endArg = 0;
    while((startArg = command.find('{', endArg)) != std::string::npos) {
      endArg = command.find('}', endArg);
      if(endArg == std::string::npos) {
        std::cerr << "ERROR: cannot find a closing } in " << command.substr(startArg) << std::endl;
        break;
      }

      size_t argLength = endArg + 1 - startArg;

      std::string columnName = command.substr(startArg + 1, argLength - 2);
      int columnIndex = materializedView.columnIndex(columnName);
      if(columnIndex < 0) {
        std::cerr << "ERROR: cannot find arg {" << columnName << "} in the columns" << std::endl;
        continue;
      }

      std::string value = materializedView.toString(columnIndex, rowIndex);
      command.replace(startArg, argLength, value);
      endArg = startArg + value.length() + 1;
    }

    // http request

    httplib::Client client(m_url.c_str());

    if(!client.is_valid()) {
      std::cout << "HTTPLIB ERROR: client not valid" << std::endl;
      std::cout << "while fetching from url:" << m_url << command << std::endl;
      return false;
    }

    std::string responseBody;

    if(auto result = client.Get(command.c_str())) {
      if(result->status == 200) {
        responseBody = result->body;
      }
    } else {
      switch(result.error()) {
      case httplib::Error::Unknown: {
        std::cerr << "HTTPLIB ERROR: Unknown" << std::endl;
      } break;
      case httplib::Error::Connection: {
        std::cerr << "HTTPLIB ERROR: Connection" << std::endl;
      } break;
      case httplib::Error::BindIPAddress: {
        std::cerr << "HTTPLIB ERROR: BindIPAddress" << std::endl;
      } break;
      case httplib::Error::Read: {
        std::cerr << "HTTPLIB ERROR: Read" << std::endl;
      } break;
      case httplib::Error::Write: {
        std::cerr << "HTTPLIB ERROR: Write" << std::endl;
      } break;
      case httplib::Error::ExceedRedirectCount: {
        std::cerr << "HTTPLIB ERROR: ExceedRedirectCount" << std::endl;
      } break;
      case httplib::Error::Canceled: {
        std::cerr << "HTTPLIB ERROR: Canceled" << std::endl;
      } break;
      case httplib::Error::SSLConnection: {
        std::cerr << "HTTPLIB ERROR: SSLConnection" << std::endl;
      } break;
      case httplib::Error::SSLLoadingCerts: {
        std::cerr << "HTTPLIB ERROR: SSLLoadingCerts" << std::endl;
      } break;
      case httplib::Error::SSLServerVerification: {
        std::cerr << "HTTPLIB ERROR: SSLServerVerification" << std::endl;
      } break;
      case httplib::Error::UnsupportedMultipartBoundaryChars: {
        std::cerr << "HTTPLIB ERROR: UnsupportedMultipartBoundaryChars" << std::endl;
      } break;
      default: {
        std::cerr << "HTTPLIB ERROR: unknown error" << std::endl;
      } break;
      }
      std::cerr << "while fetching from url:" << m_url << command << std::endl;
      return false;
    }

    if(responseBody.empty()) {
      std::cerr << "ERROR: empty body" << std::endl;
      std::cerr << "while reading result from url:" << m_url << command << std::endl;
      return false;
    }

    bool parsed = false;

    if(responseBody.substr(0, 5) == "<?xml") {
      parsed = parseXML(responseBody);
    } else {
      parsed = parseJSON(responseBody);
    }

    if(!parsed) {
      std::cerr << "while reading result from url:" << m_url << command << std::endl;
      return false;
    }

    this->m_missingValue = false;
    return true;
  }

private:
  std::string m_url;
  std::string m_command;
  std::string m_field;

  bool parseJSON(std::string const& jsonBody) {
    rapidjson::Document document;
    rapidjson::ParseResult jsonResult = document.Parse(jsonBody.c_str());
    if(!jsonResult) {
      std::cerr << "JSON parse error: " << rapidjson::GetParseError_En(jsonResult.Code());
      std::cerr << " (" << jsonResult.Offset() << ")" << std::endl;
      return false;
    }

    std::istringstream fieldStream(m_field);

    rapidjson::Value* jsonValue = &document;

    std::string nextValueName;
    while(std::getline(fieldStream, nextValueName, '.')) {
      if(nextValueName[0] == '#') {
        // treat as an array index
        int index = std::stoi(nextValueName.substr(1));
        auto iterator = jsonValue->Begin() + index;
        jsonValue = &(*iterator);
      } else {
        jsonValue = &(*jsonValue)[nextValueName.c_str()];
      }
    }

    if(jsonValue->IsString()) {
      // special case for strings, make a deep copy
      if constexpr(std::is_same<std::string, T>::value) {
        this->m_value = jsonValue->GetString();
      } else if constexpr(std::is_same<char const*, T>::value) {
        this->m_value = UniqueString::MakeUnique(jsonValue->GetString());
      } else {
        // this is a special case of type mismatch
        // try to convert from string to any type
        this->m_value = static_cast<T>(std::stof(jsonValue->GetString()));
      }
    } else {
      this->m_value = jsonValue->Get<T>();
    }

    return true;
  }

  bool parseXML(std::string const& xmlBody) {

    std::istringstream fieldStream(m_field);

    size_t startPos = 0;
    size_t endPos = std::string::npos;

    std::string nextValueName;
    while(std::getline(fieldStream, nextValueName, '.')) {
      int index = 0;
      if(nextValueName[0] == '#') {
        // treat as an array index
        index = std::stoi(nextValueName.substr(1));

        // find the start of the tag we are interested in
        startPos = xmlBody.find("<", startPos);
        if(startPos == std::string::npos) {
          std::cerr << "XML parse error: no array of elements found" << std::endl;
          return false;
        }

        // extract the tag
        endPos = xmlBody.find(">", startPos);
        nextValueName = xmlBody.substr(startPos + 1, endPos - (startPos + 1));
      }

      // usually only one, but generalise to the case where we search for an array element
      for(int i = 0; i <= index; ++i) {
        startPos = xmlBody.find("<" + nextValueName + ">", startPos);

        if(startPos == std::string::npos) {
          std::cerr << "XML parse error: '" << nextValueName << "' start tag not found"
                    << std::endl;
          return false;
        }

        startPos += nextValueName.length() + 2;
        endPos = xmlBody.find("</" + nextValueName + ">", startPos);

        if(endPos == std::string::npos) {
          std::cerr << "XML parse error: '" << nextValueName << "' end tag not found" << std::endl;
          return false;
        }
      }
    }

    std::string strValue = xmlBody.substr(startPos, endPos - startPos);

    if constexpr(std::is_same<std::string, T>::value) {
      this->m_value = strValue;
    } else if constexpr(std::is_same<char const*, T>::value) {
      this->m_value = UniqueString::MakeUnique(strValue.c_str());
    } else {
      this->m_value = static_cast<T>(std::stof(strValue));
    }

    return true;
  }
};

} // namespace boss::engines::bulk
