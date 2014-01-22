/*------------------------------ ConfigParser.h -----------------------------*/

#ifndef CONFIGINFO_H_
#define CONFIGINFO_H_

#include <string>
#include <iostream>
#include <fstream>
#include <map>

using namespace std;

class ConfigInfo {
public:
	~ConfigInfo();
	void Parse(string file_name);
	map<string, string> GetParamSet();
	string GetValue(string param);
	static ConfigInfo* GetInstance();

protected:
	ConfigInfo();

private:
	static ConfigInfo* ptr_instance;

	map<string, string> param_set;
	void ParseLine(string line, char delimiter);
	bool IsComment(string line, string delimiter);
	bool IsValidParam(string param);
};

#endif
