#include "parser.h"

#include <string>

#include <gtest/gtest.h>
#include <string_view>


TEST(Parser, ExtractStatusLine)
{ 
  HttpRes test;
  std::string http =
      "HTTP/1.0 200 OK\r\nServer: SimpleHTTP/0.6 Python/3.12.1\r\nDate: Mon,\r\n 02 Mar 2026 11:08:55 GMT\r\nContent-type: text/html\r\n\n";
  test.Parser(http);
  EXPECT_EQ(test.StatusCode(), "200");
  EXPECT_EQ(test.StatusMessage(), "OK");
}


TEST(Parser, ReqWithBody) {
  HttpRes test;
  std::string http = 
    "HTTP/1.0 200 OK\n"
    "Server: SimpleHTTP/0.6 Python/3.12.1\n"
    "Date: Mon, 02 Mar 2026 11:08:55 GMT\n"
    "Content-type: text/html\n"
    "Content-Length: 145\n"
    "Last-Modified: Tue, 21 Feb 2023 02:05:00 GMT\n"
    "\n"
    "<html>"
    "<head>"
    "<link rel=\"stylesheet\" href=\"styles.css\">"
    "</head>"
    "<body>"
    "<img src=\"marc.jpeg\">"
    "<br>"
    "<marquee>Welcome to my website</marquee>"
    "</body>"
    "</html>";
  std::string expected = 
    "HTTP/1.1 200 Ok\r\n"
    "Server: SimpleHTTP/0.6 Python/3.12.1\r\n"
    "Date: Mon, 02 Mar 2026 11:08:55 GMT\r\n"
    "Content-type: text/html\r\n"
    "Content-Length: 145\r\n"
    "Last-Modified: Tue, 21 Feb 2023 02:05:00 GMT\r\n"
    "\r\n"
    "<html>"
    "<head>"
    "<link rel=\"stylesheet\" href=\"styles.css\">"
    "</head>"
    "<body>"
    "<img src=\"marc.jpeg\">"
    "<br>"
    "<marquee>Welcome to my website</marquee>"
    "</body>"
    "</html>";
  test.Parser(http);
  EXPECT_EQ(test.Response(), expected);
}


TEST(Parser, BrokenReqWithBody) {
  HttpRes test;
  std::string http_part_1 = 
    "HTTP/1.0 200 OK\n"
    "Server: SimpleHTTP/0.6 Python/3.12.1\n"
    "Date: Mon, 02 Mar 2026 11:08:55 GMT\n"
    "Content-type: text/html\n"
    "Content-Length: 145\n";
  std::string http_part_2 =
    "Last-Modified: Tue, 21 Feb 2023 02:05:00 GMT\n"
    "\n"
    "<html>"
    "<head>"
    "<link rel=\"stylesheet\" href=\"styles.css\">"
    "</head>"
    "<body>"
    "<img src=\"marc.jpeg\">"
    "<br>"
    "<marquee>Welcome to my website</marquee>"
    "</body>"
    "</html>";
  std::string expected = 
    "HTTP/1.1 200 Ok\r\n"
    "Server: SimpleHTTP/0.6 Python/3.12.1\r\n"
    "Date: Mon, 02 Mar 2026 11:08:55 GMT\r\n"
    "Content-type: text/html\r\n"
    "Content-Length: 145\r\n"
    "Last-Modified: Tue, 21 Feb 2023 02:05:00 GMT\r\n"
    "\r\n"
    "<html>"
    "<head>"
    "<link rel=\"stylesheet\" href=\"styles.css\">"
    "</head>"
    "<body>"
    "<img src=\"marc.jpeg\">"
    "<br>"
    "<marquee>Welcome to my website</marquee>"
    "</body>"
    "</html>";
  EXPECT_ANY_THROW(test.Parser(std::string_view(http_part_1.data(), http_part_1.size())));
  std::string full_res;
  full_res.append(test.Response());
  EXPECT_EQ(test.StatusCode(), "200");
  test.Clear();
  test.Parser(std::string_view(http_part_2.data(), http_part_2.size()));
  full_res.append(test.Response());
  EXPECT_EQ(full_res, expected);
}
