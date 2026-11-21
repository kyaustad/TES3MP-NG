#ifndef OPENMW_MWMP_LUARECORDS_HPP
#define OPENMW_MWMP_LUARECORDS_HPP

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/esmstore.hpp"

#include "LuaSync.hpp"

namespace mwmp
{
    /*
        tes3mp addition: world.createRecord in multiplayer. A record created by Lua is sent to the server, which keeps
        it and gives it to every client, so objects and items made from it can be synchronized like any other. Its id
        comes from this client's own block of generated ids, so it can't clash with another client's records.
    */
    namespace LuaRecords
    {
        // A record as an ESM file, the way saved games keep records made in game
        template <class T>
        std::string toEsm(const T& record)
        {
            std::ostringstream stream;
            ESM::ESMWriter writer;
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
            writer.setVersion(0);
            writer.setType(0);
            writer.setRecordCount(1);
            writer.save(stream);
            writer.startRecord(T::sRecordId);
            record.save(writer);
            writer.endRecord(T::sRecordId);
            writer.close();
            return stream.str();
        }

        // Same on every client: FNV-1a of the record's type and content
        inline std::string hashContent(std::uint32_t type, const std::string& content)
        {
            std::uint64_t hash = 14695981039346656037ull;
            auto add = [&hash](unsigned char byte) {
                hash ^= byte;
                hash *= 1099511628211ull;
            };
            for (int i = 0; i < 4; ++i)
                add(static_cast<unsigned char>(type >> (8 * i)));
            for (char c : content)
                add(static_cast<unsigned char>(c));
            char text[17];
            std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
            return text;
        }

        // ESMStore::insert for Lua scripts: the existing record when one has the same content, else a new record
        // that is shared with the other players
        template <class T>
        const T* create(const T& draft)
        {
            MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
            LuaSync& sync = LuaSync::get();
            if (!sync.sharesRecords())
                return store.insert(draft);

            T anonymous = draft;
            anonymous.mId = ESM::RefId();
            const std::string hash = hashContent(T::sRecordId, toEsm(anonymous));
            if (const auto existing = sync.findSharedRecord(hash))
            {
                if (const T* record = store.get<T>().search(*existing))
                    return record;
            }

            const T* record = store.insert(draft);
            sync.onRecordCreated(record->mId, hash, toEsm(*record));
            return record;
        }
    }
}

#endif // OPENMW_MWMP_LUARECORDS_HPP
