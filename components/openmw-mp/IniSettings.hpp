#ifndef OPENMW_MP_INISETTINGS_HPP
#define OPENMW_MP_INISETTINGS_HPP

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include <components/files/configurationmanager.hpp>
#include <components/settings/parser.hpp>

namespace mwmp
{
    /*
        OpenMW 0.49 replaced the instance-based Settings::Manager with static typed settings for the game itself,
        so TES3MP's own config files (tes3mp-client.cfg, tes3mp-server.cfg) are read with the shared settings file
        parser instead
    */
    class IniSettings
    {
    public:
        void load(const std::filesystem::path& file, bool overrideExisting)
        {
            Settings::SettingsFileParser parser;
            parser.loadSettingsFile(file, mValues, false, overrideExisting);
        }

        // Loads <name>-default.cfg from the local or global config path
        void loadDefault(const Files::ConfigurationManager& cfgMgr, const std::string& name)
        {
            const std::filesystem::path localDefault = cfgMgr.getLocalPath() / (name + "-default.cfg");
            const std::filesystem::path globalDefault = cfgMgr.getGlobalPath() / (name + "-default.cfg");

            if (std::filesystem::exists(localDefault))
                load(localDefault, false);
            else if (std::filesystem::exists(globalDefault))
                load(globalDefault, false);
            else
                throw std::runtime_error("No default settings file found! Make sure the file \"" + name
                    + "-default.cfg\" was properly installed.");
        }

        // Loads <name>-default.cfg from the local or global config path, then <name>.cfg from the user config
        // path if it exists. Returns the path of the user config file.
        std::filesystem::path loadDefaultAndUser(const Files::ConfigurationManager& cfgMgr, const std::string& name)
        {
            loadDefault(cfgMgr, name);

            const std::filesystem::path userPath = cfgMgr.getUserConfigPath() / (name + ".cfg");
            if (std::filesystem::exists(userPath))
                load(userPath, true);

            return userPath;
        }

        bool has(std::string_view setting, std::string_view category) const
        {
            return mValues.find(std::make_pair(std::string(category), std::string(setting))) != mValues.end();
        }

        std::string getString(std::string_view setting, std::string_view category) const
        {
            auto it = mValues.find(std::make_pair(std::string(category), std::string(setting)));
            if (it == mValues.end())
                throw std::runtime_error(
                    "Missing setting [" + std::string(category) + "] " + std::string(setting));
            return it->second;
        }

        int getInt(std::string_view setting, std::string_view category) const
        {
            return std::stoi(getString(setting, category));
        }

        float getFloat(std::string_view setting, std::string_view category) const
        {
            return std::stof(getString(setting, category));
        }

        bool getBool(std::string_view setting, std::string_view category) const
        {
            const std::string value = getString(setting, category);
            return value == "true" || value == "1";
        }

        void set(std::string_view setting, std::string_view category, std::string value)
        {
            mValues[std::make_pair(std::string(category), std::string(setting))] = std::move(value);
        }

        // Writes these values to a file, keeping its comments. Settings of the file that aren't loaded here are
        // commented out, so load the file before changing it
        void save(const std::filesystem::path& file) const
        {
            Settings::SettingsFileParser parser;
            parser.saveSettingsFile(file, mValues);
        }

    private:
        Settings::CategorySettingValueMap mValues;
    };
}

#endif // OPENMW_MP_INISETTINGS_HPP
