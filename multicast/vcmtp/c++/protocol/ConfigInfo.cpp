/*
 * ConfigParser.cpp
 *
 *  Created on: Jun 25, 2011
 *      Author: jie
 */

#include "ConfigInfo.h"


ConfigInfo* ConfigInfo::ptr_instance = NULL;

ConfigInfo::ConfigInfo() {

}

ConfigInfo::~ConfigInfo() {
	if (ptr_instance != NULL) {
		delete ptr_instance;
		ptr_instance = NULL;
	}
}

ConfigInfo* ConfigInfo::GetInstance() {
	if (ptr_instance == NULL) {
		ptr_instance = new ConfigInfo();
	}

	return ptr_instance;
}

void ConfigInfo::Parse(string fileName) {
	string line;
	ifstream fs(fileName.c_str());
	if (fs.is_open()) {
		param_set.clear();
		while (fs.good()) {
			getline(fs, line);
			ParseLine(line, '=');
		}
		fs.close();
	} else {
		cout << "Cannot open config file to read." << endl;
	}
}


void ConfigInfo::ParseLine(string line, char delimiter) {
	if (!IsComment(line, "#")) {
		int length = line.length();
		int index = line.find(delimiter, 0);
		if (index > 0) {
			string param = line.substr(0, index);
			string value = line.substr(index + 1, length - 1 - index);
			if (IsValidParam(param)) {
				param_set.insert(pair<string, string>(param, value));
			}
		}
	}
}


bool ConfigInfo::IsComment(string line, string delimiter) {
	if (line.find(delimiter, 0) == 0)
		return true;
	else
		return false;
}


bool ConfigInfo::IsValidParam(string param) {
	return true;
}


map<string, string> ConfigInfo::GetParamSet() {
	return param_set;
}

string ConfigInfo::GetValue(string param) {
	return param_set[param];
}



