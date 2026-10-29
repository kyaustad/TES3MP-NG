#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <sstream>
#include <string_view>
#include <vector>

#include <components/esm/refid.hpp>

namespace ESM
{
    struct ActiveEffect;
}

#if (defined __WIN32__ || defined _WIN32 || defined WIN32)
#define __WINDOWS
#endif

#ifdef __WINDOWS
int setenv(const char *name, const char *value, int overwrite);
#endif

namespace Utils
{
    std::string convertPath(std::string str);

    /*
        Conversions between ESM::RefId and the textual ids used on the network and by server scripts.
        Plain string ids stay as they are; other RefId kinds use ESM::RefId::serializeText().
    */
    ESM::RefId toRefId(std::string_view text);

    // Compares a RefId with its network text form. Comparing a RefId with a string directly only works for string
    // ids: empty, generated and other ids never compare equal to their text
    bool refIdEquals(const ESM::RefId& refId, std::string_view text);
    std::string refIdToString(const ESM::RefId &refId);
    // The returned pointer stays valid forever for string ids and until the next call on this thread otherwise
    const char *refIdToCString(const ESM::RefId &refId);

    /*
        Records created at runtime (enchanted items, custom spells and potions) have ids that are only meaningful on
        the client that created them: "$dynamic" ids in OpenMW 0.47, generated RefIds in OpenMW 0.49+
    */
    bool isDynamicRefId(const ESM::RefId &refId);
    bool isDynamicRefId(std::string_view refId);


    /*
        TES3MP's server API exposes magic effects, skills and attributes by their integer index, while
        OpenMW 0.49+ identifies them by RefId. An active effect's argument is the index of the affected
        skill or attribute (depending on the effect), or -1.
    */
    int getEffectIndex(const ESM::RefId &effectId);
    ESM::RefId getEffectId(int effectIndex);
    int getActiveEffectArg(const ESM::ActiveEffect &effect);
    void setActiveEffectArg(ESM::ActiveEffect &effect, int arg);
    bool effectTargetsSkill(const ESM::RefId &effectId);
    bool effectTargetsAttribute(const ESM::RefId &effectId);
    // Skill and attribute names in their usual capitalization ("Block", "Strength"), or nullptr for bad indexes
    const char *getSkillName(int skillIndex);
    const char *getAttributeName(int attributeIndex);
    // Attribute ids as the typed StringRefId OpenMW's mechanics use for skill increase tracking
    ESM::StringRefId getAttributeId(int attributeIndex);
    bool doesFileHaveChecksum(std::string filePath, unsigned int requiredChecksum);

    void timestamp();

    int progressFunc(double TotalToDownload, double NowDownloaded);

    unsigned int getNumberOfDigits(int integer);
    bool compareDoubles(double a, double b, double epsilon = 0.005);
    bool compareFloats(float a, float b, float epsilon = 0.005f);

    template <class Type, class Type2>
    bool vectorContains(const std::vector<Type> &vectorChecked, const Type2 &value)
    {
        return std::find(vectorChecked.begin(), vectorChecked.end(), value) != vectorChecked.end();
    }

    template <class Type>
    uint32_t getVectorSize(const std::vector<Type> &vectorChecked)
    {
        return static_cast<uint32_t>(vectorChecked.size());
    }

    template <class Type>
    void resetVector(std::vector<Type> &vectorInput, uint32_t newSize)
    {
        vectorInput.clear();
        vectorInput.resize(newSize);
    }

    std::string replaceString(const std::string &source, const char *find, const char *replace);

    std::string toString(int num);

    std::string &removeExtension(std::string &file);

    long int getFileLength(const char *file);

    unsigned int crc32Checksum(const std::string &file);

    std::string getOperatingSystemType();
    std::string getArchitectureType();

    std::string getVersionInfo(std::string appName, std::string version, std::string commitHash, int protocol);

    void printWithWidth(std::ostringstream &sstr, std::string str, size_t width);

    std::string intToHexStr(unsigned val);
    unsigned int hexStrToInt(std::string hexString);
}
#endif //UTILS_HPP
