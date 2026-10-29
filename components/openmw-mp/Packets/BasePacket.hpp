#ifndef OPENMW_BASEPACKET_HPP
#define OPENMW_BASEPACKET_HPP

#include <string>
#include <type_traits>
#include <RakNetTypes.h>
#include <BitStream.h>
#include <PacketPriority.h>

#include <components/openmw-mp/WinMacroGuard.hpp>
#include <variant>

#include <components/esm/refid.hpp>
#include <components/esm3/activespells.hpp>
#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadmgef.hpp>


namespace mwmp
{
    namespace detail
    {
        template <class T>
        struct IsVariant : std::false_type
        {
        };
        template <class... Args>
        struct IsVariant<std::variant<Args...>> : std::true_type
        {
        };

        // Types that are trivially copyable but hold pointers into process-local memory
        template <class T>
        constexpr bool isUnsafeForRawCopyImpl = IsVariant<T>::value || std::is_same_v<T, ESM::RefId>
            || std::is_same_v<T, ESM::ActiveEffect> || std::is_same_v<T, ESM::ENAMstruct>
            || std::is_same_v<T, ESM::IndexedENAMstruct>;

        template <class T>
        constexpr bool isUnsafeForRawCopy = isUnsafeForRawCopyImpl<std::remove_cv_t<std::remove_all_extents_t<T>>>;
    }

    class BasePacket
    {
    public:
        explicit BasePacket(RakNet::RakPeerInterface *peer);

        virtual ~BasePacket() = default;

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
        virtual uint32_t Send(bool toOtherPlayers = true);
        virtual uint32_t Send(RakNet::AddressOrGUID destination);
        virtual void Read();

        void setGUID(RakNet::RakNetGUID newGuid);
        RakNet::RakNetGUID getGUID();

        void SetReadStream(RakNet::BitStream *bitStream);
        void SetSendStream(RakNet::BitStream *bitStream);
        void SetStreams(RakNet::BitStream *inStream, RakNet::BitStream *outStream);
        virtual uint32_t RequestData(RakNet::RakNetGUID targetGuid);

        static inline uint32_t headerSize()
        {
            return static_cast<uint32_t>(1 + RakNet::RakNetGUID::size()); // packetID + RakNetGUID (uint64_t)
        }

        uint8_t GetPacketID() const
        {
            return packetID;
        }

        bool isPacketValid() const
        {
            return packetValid;
        }

    protected:
        template<class templateType>
        bool RW(templateType &data, uint32_t size, bool write)
        {
            static_assert(std::is_trivially_copyable_v<std::remove_pointer_t<std::decay_t<templateType>>>,
                "Only trivially copyable types can be written as raw memory");
            if (write)
                bs->Write(data, size);
            else
                return bs->Read(data, size);
            return true;
        }

        template<class templateType>
        bool RW(templateType &data, bool write, bool compress = 0)
        {
            static_assert(std::is_trivially_copyable_v<templateType>,
                "Only trivially copyable types can be written as raw memory; add an RW overload for this type");
            static_assert(!detail::isUnsafeForRawCopy<templateType>,
                "This type contains process-local pointers (e.g. ESM::RefId); serialize its fields explicitly");
            if (write)
            {
                if (compress)
                    bs->WriteCompressed(data);
                else
                    bs->Write(data);
                return true;
            }
            else
            {
                if (compress)
                    return bs->ReadCompressed(data);
                else
                    return bs->Read(data);
            }
        }

        bool RW(RakNet::RakNetGUID &data, bool write, bool compress = false)
        {
            if (write)
                bs->Write(data);
            else
                return bs->Read(data);
            return true;
        }

        bool RW(bool &data, bool write)
        {
            if (write)
                bs->Write(data);
            else
                return bs->Read(data);
            return true;
        }

        const static uint32_t maxStrSize = 64 * 1024; // 64 KiB

        bool RW(std::string &str, bool write, bool compress = false, std::string::size_type maxSize = maxStrSize)
        {
            bool res = true;
            if (write)
            {
                if (compress)
                    RakNet::RakString::SerializeCompressed(str.substr(0, maxSize).c_str(), bs); // todo: remove extra copy of string
                else
                {
                    RakNet::RakString rstr;
                    rstr.AppendBytes(str.c_str(), str.size() > maxSize ? maxSize : str.size());
                    bs->Write(rstr);
                }
            }
            else
            {
                RakNet::RakString rstr;
                if (compress)
                    res = rstr.DeserializeCompressed(bs);
                else
                    res = bs->Read(rstr);

                if (res)
                {
                    rstr.Truncate(rstr.GetLength() > maxSize ? maxSize : rstr.GetLength());
                    str = rstr.C_String();
                }
                else
                    str = std::string();
            }
            return res;
        }

        /*
            Start of tes3mp addition

            OpenMW 0.49+ uses ESM::RefId instead of plain strings for record ids. On the wire we keep
            using the textual form, which round-trips every RefId kind (string, form id, generated, index,
            exterior cell) and stays readable for the server's Lua scripts.
        */
        bool RW(ESM::RefId &refId, bool write, bool compress = false)
        {
            std::string text;
            if (write)
                text = refId.empty() ? std::string() : refId.serializeText();
            bool res = RW(text, write, compress);
            if (!write)
                refId = text.empty() ? ESM::RefId() : ESM::RefId::deserializeText(text);
            return res;
        }

        // RefIds of hardcoded record types (magic effects, skills, attributes) travel as their integer index,
        // which is what TES3MP's server scripts have always used
        template <class ToIndex, class FromIndex>
        bool RWIndexedRefId(ESM::RefId &refId, bool write, ToIndex toIndex, FromIndex fromIndex)
        {
            int16_t index = -1;
            if (write)
                index = static_cast<int16_t>(refId.empty() ? -1 : toIndex(refId));
            bool res = RW(index, write);
            if (!write)
                refId = fromIndex(index);
            return res;
        }

        // Serializes an active magic effect. A summoned creature argument (a local RefNum) has no meaning on other
        // clients and is not transferred.
        bool RWActiveEffect(ESM::ActiveEffect &effect, bool write)
        {
            RWIndexedRefId(effect.mEffectId, write, ESM::MagicEffect::refIdToIndex, ESM::MagicEffect::indexToRefId);

            ESM::RefId arg;
            if (write && std::holds_alternative<ESM::RefId>(effect.mArg))
                arg = std::get<ESM::RefId>(effect.mArg);
            RW(arg, write, true);
            if (!write)
                effect.mArg = arg;

            RW(effect.mMagnitude, write);
            RW(effect.mMinMagnitude, write);
            RW(effect.mMaxMagnitude, write);
            RW(effect.mDuration, write);
            RW(effect.mTimeLeft, write);
            RW(effect.mEffectIndex, write);
            return RW(effect.mFlags, write);
        }

        const static uint32_t maxBinarySize = 1024 * 1024; // 1 MiB

        // Binary-safe variant of RW(std::string&), used for serialized Lua data that can contain NUL bytes
        bool RWBinary(std::string &data, bool write, uint32_t maxSize = maxBinarySize)
        {
            uint32_t size = static_cast<uint32_t>(data.size() > maxSize ? maxSize : data.size());
            if (write)
            {
                bs->Write(size);
                if (size > 0)
                    bs->Write(data.data(), size);
                return true;
            }

            if (!bs->Read(size) || size > maxSize || BITS_TO_BYTES(bs->GetNumberOfUnreadBits()) < size)
            {
                data.clear();
                return false;
            }
            data.resize(size);
            return size == 0 || bs->Read(data.data(), size);
        }
        /*
            End of tes3mp addition
        */

    protected:
        uint8_t packetID;
        PacketReliability reliability;
        PacketPriority priority;
        int8_t orderChannel;
        RakNet::BitStream *bsRead, *bsSend, *bs;
        RakNet::RakPeerInterface *peer;
        RakNet::RakNetGUID guid;
        bool packetValid;
    };
}

#endif //OPENMW_BASEPACKET_HPP
