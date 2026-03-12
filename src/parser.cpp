#include "parser.h"

#include <array>
#include <string>
#include <string_view>

void Http::Parser(std::string_view message) {
  // 1. Get Status-Line
  auto get_line = [&message, this]() -> std::string_view {
    auto line_end = message.find("\r\n");
    if (line_end == std::string_view::npos) {
      // handles nc that only sends a \n
      line_end = message.find('\n');
      if (line_end == std::string_view::npos) {
        send_message.append(message.substr(0, line_end));
        if (parsing_state != ParseState::kBody) {
          // no proper handling if start-line or header-line is cut in the
          // middle
          throw std::runtime_error("Invalid line");
        }
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
    parsed_status_line[0] = status_line.substr(0, first_space);
    parsed_status_line[1] =
        status_line.substr(first_space + 1, second_space - first_space - 1);
    parsed_status_line[2] = status_line.substr(second_space + 1);
    parsing_state = ParseState::kHeaders;

    if (parsed_status_line[0].substr(0, 4) == "HTTP") {
      send_message.append("HTTP/1.1 ")
          .append(parsed_status_line[1])
          .append(" ")
          .append(parsed_status_line[2])
          .append("\r\n");
    } else {
      send_message.append(status_line).append("\r\n");
    }
  }

  // 2. Get message-headers
  if (parsing_state == ParseState::kHeaders) {
    std::string_view header;
    while (!(header = get_line()).empty()) {
      auto separator = header.find(": ");
      std::string key;
      std::string value;
      if (separator == std::string_view::npos) {
        if (header[0] != ' ') {
          throw std::runtime_error("Wrong header format for multiline headers");
        }
        send_message.append(header);
      } else {
        key = header.substr(0, separator);
        value = header.substr(separator + 2);
        send_message.append(key).append(": ").append(value).append("\r\n");
        if (key == "Content-Length") {
          content_length = std::stoi(value);
        } else if (key == "Connection") {
          connection = value;
        }
      }
    }
    send_message.append("\r\n");
    if (content_length > 0) {
      parsing_state = ParseState::kBody;
    } else {
      parsing_state = ParseState::kEnd;
    }
  }

  // 3. Get message-body
  if (parsing_state == ParseState::kBody) {
    send_message.append(message);
    content_length -= message.size();
    if (content_length <= 0) {
      parsing_state = ParseState::kEnd;
    }
  }
}

std::array<std::string, 3> Http::StatusLine() const noexcept {
  return parsed_status_line;
}

const std::string &Http::Response() const noexcept { return send_message; }

void Http::ClearMessage() noexcept { send_message.clear(); }

ParseState Http::GetState() const noexcept { return parsing_state; }

bool Http::KeepAlive() noexcept {
  if (parsed_status_line[2] == "HTTP/1.0") {
    return connection == "keep-alive";
  }
  if (parsed_status_line[2] == "HTTP/1.1") {
    return !(connection == "close");
  }
  return false;
}
