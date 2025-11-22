// date:2025-11-19 16:32:07
#include<iostream>
#include"Logger.h"
using namespace yazi::utility;
using namespace std;
int main(){
	Logger::instance()->open("./text.log");
	Logger::instance()->max(400);

	debug("19");
	info("info message");
	warn("warn message");
	return 0;
 }
