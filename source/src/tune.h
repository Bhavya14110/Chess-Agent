#pragma once
#include <string>

void tune(const std::string& file, int epochs, int threads, double lr, const std::string& out);
void quietfilter(const std::string& infile, const std::string& outfile, int threads);
