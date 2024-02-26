#include "PerSystemConf.h"
#include <iostream>
#include <fstream>
#include "Log.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"
#include "Settings.h"
#include "Paths.h"
#include "Paths.h"
#include "FileData.h"

#include <set>
#include <regex>
#include <string>
#include <iostream>
#include <SDL_timer.h>

static std::set<std::string> dontRemoveValue
{
};

static std::map<std::string, std::string> defaults =
{
};

std::map<std::string, PerSystemConf*> PerSystemConf::instanceMap;

PerSystemConf *PerSystemConf::getInstance(SystemData *sysData) 
{
		std::string name = sysData->getName();
		
    if (instanceMap.count(name) == 0) {
			std::string path = sysData->getRootFolder()->getPath();
			instanceMap[name] = new PerSystemConf(path + "/system.cfg");
		}

    return instanceMap[name];
}

bool PerSystemConf::saveAllSystemConf()
{
	for (auto& it : instanceMap)
	{
		it.second->saveSystemConf();
	}
	return true;
}

PerSystemConf::PerSystemConf(): mWasChanged(false), mSystemConfFile(""), confMap()
{
	
}

PerSystemConf::PerSystemConf(std::string filepath): mWasChanged(false), mSystemConfFile(filepath), confMap()
{
	if (filepath.empty()) {
			return;
	}

	//mSystemConfFile = filepath;
	mSystemConfFileTmp = filepath + ".tmp";

	LOG(LogWarning) << "mSystemConfFile:" << mSystemConfFile;

	if (!Utils::FileSystem::exists(mSystemConfFile)) {
		Utils::FileSystem::writeAllText(mSystemConfFile, "");
		return;
	}

	if (Utils::FileSystem::readAllText(mSystemConfFile).empty()) {
		LOG(LogWarning) << "mSystemConfFile:" << mSystemConfFile << "No text skipping.";
		return;
	}

	LOG(LogWarning) << "loadSystemConf()";
	loadSystemConf();	
}

PerSystemConf::~PerSystemConf()
{
}

bool PerSystemConf::loadSystemConf()
{
	if (mSystemConfFile.empty())
		return true;

	mWasChanged = false;

	std::string line;
	std::ifstream systemConf(mSystemConfFile);
	if (systemConf && systemConf.is_open()) 
	{
		if (systemConf.eof())
			return true;

		while (std::getline(systemConf, line)) 
		{
			LOG(LogWarning) << "systemConf line: " << line;
			int idx = line.find("=");
			if (idx == std::string::npos || line.find("#") == 0 || line.find(";") == 0)
				continue;

			std::string key = line.substr(0, idx);
			std::string value = line.substr(idx + 1);
			if (!key.empty() && !value.empty())
				confMap[key] = value;

		}
		systemConf.close();
	}
	else
	{
		LOG(LogError) << "Unable to open " << mSystemConfFile;
		return false;
	}

	return true;
}

bool PerSystemConf::saveSystemConf()
{
	if (mSystemConfFile.empty())
		return false;	

	if (!mWasChanged)
		return false;

	std::ifstream filein(mSystemConfFile); //File to read from

#ifndef WIN32
	if (!filein)
	{
		LOG(LogError) << "Unable to open for saving :  " << mSystemConfFile << "\n";
		return false;
	}
#endif

	/* Read all lines in a vector */
	std::vector<std::string> fileLines;
	std::string line;

	if (filein)
	{
		while (std::getline(filein, line))
			fileLines.push_back(line);

		filein.close();
	}

	static std::string removeID = "$^é(p$^mpv$êrpver$^vper$vper$^vper$vper$vper$^vperv^pervncvizn";

	int lastTime = SDL_GetTicks();

	/* Save new value if exists */
	for (auto& it : confMap)
	{
		std::string key = it.first + "=";		
		char key0 = key[0];

		bool lineFound = false;

		for (auto& currentLine : fileLines)
		{
			if (currentLine.size() < 3)
				continue;

			char fc = currentLine[0];
			if (fc != key0 && currentLine[1] != key0)
				continue;

			int idx = currentLine.find(key);
			if (idx == std::string::npos)
				continue;

			if (idx == 0 || (idx == 1 && (fc == ';' || fc == '#')))
			{
				std::string val = it.second;
				if ((!val.empty() && val != "auto") || dontRemoveValue.find(it.first) != dontRemoveValue.cend())
				{
					auto defaultValue = defaults.find(key);
					if (defaultValue != defaults.cend() && defaultValue->second == val)
						currentLine = removeID;
					else
						currentLine = key + val;
				}
				else 
					currentLine = removeID;

				lineFound = true;
			}
		}

		if (!lineFound)
		{
			std::string val = it.second;
			if (!val.empty() && val != "auto")
				fileLines.push_back(key + val);
		}
	}

	lastTime = SDL_GetTicks() - lastTime;

	LOG(LogDebug) << "saveSystemConf :  " << lastTime;

	std::ofstream fileout(mSystemConfFileTmp); //Temporary file
	if (!fileout)
	{
		LOG(LogError) << "Unable to open for saving :  " << mSystemConfFileTmp << "\n";
		return false;
	}
	for (int i = 0; i < fileLines.size(); i++) 
	{
		if (fileLines[i] != removeID)
			fileout << fileLines[i] << "\n";
	}

	fileout.close();
	
	std::ifstream  src(mSystemConfFileTmp, std::ios::binary);
	std::ofstream  dst(mSystemConfFile, std::ios::binary);
	dst << src.rdbuf();

	remove(mSystemConfFileTmp.c_str());
	mWasChanged = false;

	return true;
}

std::string PerSystemConf::get(const std::string &name) 
{	
	auto it = confMap.find(name);
	if (it != confMap.cend())
		return it->second;

	auto dit = defaults.find(name);
	if (dit != defaults.cend())
		return dit->second;

    return "";
}

bool PerSystemConf::set(const std::string &name, const std::string &value) 
{
	if (confMap.count(name) == 0 || confMap[name] != value)
	{
		confMap[name] = value;
		mWasChanged = true;
		return true;
	}

	return false;
}

bool PerSystemConf::getBool(const std::string &name, bool defaultValue)
{
	if (defaultValue)
		return get(name) != "0";

	return get(name) == "1";
}

bool PerSystemConf::setBool(const std::string &name, bool value)
{
	return set(name, value  ? "1" : "0");
}
