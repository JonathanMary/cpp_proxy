#include <gtest/gtest.h>
#include <string>
#include "parser.h"


TEST(Parser, ReqWithoutBody)
{ 
  HttpReq test;
  std::string http =
      "HTTP/1.0 200 OK\r\nServer: SimpleHTTP/0.6 Python/3.12.1\r\nDate: Mon,\r\n 02 Mar 2026 11:08:55 GMT\r\nContent-type: text/html\r\n\n";
  test.Parser(http);
  EXPECT_EQ(test.StatusCode(), "200");
  EXPECT_EQ(test.StatusMessage(), "OK");
}

TEST(Parser, ReqWithBody) {
  HttpReq test2;
  std::string http2 = 
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
  std::string body =
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
  test2.Parser(http2);
  int content_length = stoi(test2.ContentLength());
  EXPECT_EQ(content_length, body.length());
  EXPECT_EQ(content_length, test2.Body().size());
  EXPECT_EQ(test2.Body(), body);
}
