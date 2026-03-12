#ifndef H_src_parser
#define H_src_parser

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

enum class ParseState : std::uint8_t { kStartLine, kHeaders, kBody, kEnd };

class Http {
public:
  void Parser(std::string_view message);
  void ClearMessage() noexcept;
  std::array<std::string, 3> StatusLine() const noexcept;
  const std::string& Response() const noexcept;;
  ParseState GetState() const noexcept;
  bool KeepAlive() noexcept;
  
  private:
  std::string send_message;
  size_t content_length = 0;
  std::string connection;
  ParseState parsing_state = ParseState::kStartLine;
  std::array<std::string, 3>parsed_status_line{};
};
#endif
