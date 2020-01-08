#pragma once

#include <string>

void clrNewlines(std::string &str);
bool split(std::string &orig, std::string &left, std::string &right, const char delimiter);
void lower(std::string &str);
int hideInput(int fd, bool hide);
