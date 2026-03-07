#include "parser.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <iostream>
#include <cassert>
#include <stdexcept>
#include <string>
#include <string_view>



void HttpReq::Parser(std::string_view message) {
  // 1. Get Status-Line

  auto get_line = [&message]() -> std::string_view {
      auto line_end = message.find("\r\n");
      if (line_end == std::string_view::npos) {
          // handles nc that only sends a \n
          line_end = message.find('\n');
          if (line_end == std::string_view::npos) {
              std::cerr << "Incomplete line" << std::endl;
              return "";
          }
          auto line = message.substr(0, line_end);
          message.remove_prefix(line_end + 1);
          return line;
      }

      auto line = message.substr(0, line_end);
      message.remove_prefix(line_end + 2);
      return line;
  };

  if (parsing_state == ParseState::kStartLine) {
    auto status_line = get_line();
    auto first_space = status_line.find(' ');
    auto second_space = status_line.find(' ', first_space + 1);
    status_code = status_line.substr(first_space + 1, second_space - first_space - 1);
    status_message = status_line.substr(second_space + 1);
    parsing_state = ParseState::kHeaders;
  }

  // 2. Get message-headers
  if (parsing_state == ParseState::kHeaders) {
    std::string_view header;
    std::string key;
    while(!(header = get_line()).empty()) {
        auto separator = header.find(": ");
        std::string value;
        if (separator == std::string_view::npos) {
            if (header[0] != ' ') {
              throw std::runtime_error("Wrong header format for multiline headers");
            }
            auto prev_value = headers.at(key);
            value = prev_value.append(header);
  
        } else {
            key = header.substr(0, separator);
            value = header.substr(separator + 2);
        }
        headers.insert({key, value});
    }
    auto content_length = headers.find("Content-Length");
    if (content_length != headers.end()) {
      if (std::stoi(content_length->second) > 0) {
        parsing_state = ParseState::kBody;
      } else {
        parsing_state = ParseState::kEnd;
      }
    }
  }

  // 3. Get message-body
  if (parsing_state == ParseState::kBody) {
    body = message;

    parsing_state = ParseState::kEnd;
  }
}

const std::string& HttpReq::ContentLength() const {
  return headers.at("Content-Length");
}
const std::string& HttpReq::StatusCode() const noexcept {
  return status_code;
}
const std::string& HttpReq::StatusMessage() const noexcept {
  return status_message;
}
const std::string& HttpReq::Body() const noexcept {
  return body;
}
