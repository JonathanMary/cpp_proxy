#ifndef H_src_parser
#define H_src_parser

#include <cstddef>
#include <string>
#include <string_view>

enum class ParseState : std::uint8_t { kStartLine, kHeaders, kBody, kEnd };

class HttpRes {
public:
  void Parser(std::string_view message);
  const std::string& StatusCode() const noexcept;
  const std::string& StatusMessage() const noexcept;
  const std::string& Response() const noexcept;;
  void Clear() noexcept;
  
  private:
  std::string res_message;
  size_t content_length = 0;
  ParseState parsing_state = ParseState::kStartLine;
  // status line
  std::string status_code;
  std::string status_message;
};
#endif
