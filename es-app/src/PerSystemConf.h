#ifndef EMULATIONSTATION_PER_SYSTEMCONF_H
#define EMULATIONSTATION_PER_SYSTEMCONF_H

#include "SystemData.h"

#include <string>
#include <map>

class PerSystemConf 
{
public:
		static PerSystemConf* getInstance(SystemData* sysData);
		static bool saveAllSystemConf();

		PerSystemConf();
		PerSystemConf(std::string filepath);
		~PerSystemConf();

    bool loadSystemConf();
    bool saveSystemConf();

    std::string get(const std::string &name);
    bool set(const std::string &name, const std::string &value);

	bool getBool(const std::string &name, bool defaultValue = false);
	bool setBool(const std::string &name, bool value);

private:
	std::map<std::string, std::string> confMap;
	bool mWasChanged;

	std::string mSystemConfFile;
	std::string mSystemConfFileTmp;

	static std::map<std::string, PerSystemConf*> instanceMap;
};


#endif //EMULATIONSTATION_PER_SYSTEMCONF_H
