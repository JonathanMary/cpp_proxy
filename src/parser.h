#ifndef H_src_parser
#define H_src_parser

#include <string>
#include <string_view>
#include <unordered_map>


enum class ParseState : std::uint8_t { kStartLine, kHeaders, kBody, kEnd };

class HttpReq {
public:
  void Parser(std::string_view message);
  const std::string& StatusCode() const noexcept;
  const std::string& StatusMessage() const noexcept;
  const std::string& ContentLength() const;
  const std::string& Body() const noexcept;

private:
  // status line
  std::string status_code;
  std::string status_message;
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  ParseState parsing_state = ParseState::kStartLine;
};
#endif
