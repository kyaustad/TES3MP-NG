#include "LuaData.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace mwmp
{
    std::string NetObjectId::toString() const
    {
        switch (kind)
        {
            case CONTENT:
                return "content(" + std::to_string(refNumIndex) + ", " + std::to_string(contentFile) + ")";
            case MP:
                return "mp(" + std::to_string(mpNum) + ")";
            case PLAYER:
                return "player(" + std::to_string(guid) + ")";
            default:
                return "none";
        }
    }
}

namespace mwmp::LuaData
{
    namespace
    {
        // Mirrors components/lua/serialization.cpp
        constexpr unsigned char FORMAT_VERSION = 0;
        enum class SerializedType : unsigned char
        {
            NUMBER = 0x0,
            LONG_STRING = 0x1,
            BOOLEAN = 0x2,
            TABLE_START = 0x3,
            TABLE_END = 0x4,
            VEC2 = 0x10,
            VEC3 = 0x11,
            TRANSFORM_M = 0x12,
            TRANSFORM_Q = 0x13,
            VEC4 = 0x14,
            COLOR = 0x15,
        };
        constexpr unsigned char SHORT_STRING_FLAG = 0x20;
        constexpr unsigned char CUSTOM_FULL_FLAG = 0x40;
        constexpr unsigned char CUSTOM_COMPACT_FLAG = 0x80;

        constexpr std::string_view sLocalObjectTypeName = "o";
        constexpr std::string_view sLocalObjectListTypeName = "objlist";
        constexpr int sMaxDepth = 64;

        struct Value
        {
            enum Type
            {
                Number,
                Boolean,
                String,
                Table,
                Vec2,
                Vec3,
                Vec4,
                Color,
                TransformM,
                TransformQ,
                Custom
            };

            Type type = Number;
            double number = 0;
            bool boolean = false;
            std::string string; // String value, or the type name of a Custom value
            std::string payload; // Custom payload
            std::vector<double> components; // Vectors, colors and transforms
            std::vector<std::pair<Value, Value>> entries; // Table entries
        };

        class Reader
        {
        public:
            explicit Reader(std::string_view data)
                : mData(data)
            {
            }

            Value readRoot()
            {
                if (mData.empty() || static_cast<unsigned char>(mData[0]) != FORMAT_VERSION)
                    throw FormatError("Unsupported Lua serialization format version");
                mData.remove_prefix(1);
                Value v = read(0);
                if (!mData.empty())
                    throw FormatError("Unexpected data after serialized Lua value");
                return v;
            }

        private:
            template <class T>
            T get()
            {
                if (mData.size() < sizeof(T))
                    throw FormatError("Unexpected end of serialized Lua data");
                T v;
                std::memcpy(&v, mData.data(), sizeof(T));
                mData.remove_prefix(sizeof(T));
                return v;
            }

            std::string_view take(size_t size)
            {
                if (mData.size() < size)
                    throw FormatError("Unexpected end of serialized Lua data");
                std::string_view res = mData.substr(0, size);
                mData.remove_prefix(size);
                return res;
            }

            Value read(int depth)
            {
                if (depth > sMaxDepth)
                    throw FormatError("Lua data is nested too deeply");
                Value v;
                const unsigned char type = get<unsigned char>();
                if (type & (CUSTOM_COMPACT_FLAG | CUSTOM_FULL_FLAG))
                {
                    size_t typeNameSize, dataSize;
                    if (type & CUSTOM_COMPACT_FLAG)
                    {
                        typeNameSize = (type & 7) + 1;
                        dataSize = (type >> 3) & 15;
                    }
                    else
                    {
                        typeNameSize = (type & 63) + 1;
                        dataSize = get<uint32_t>();
                    }
                    v.type = Value::Custom;
                    v.string = std::string(take(typeNameSize));
                    v.payload = std::string(take(dataSize));
                    return v;
                }
                if (type & SHORT_STRING_FLAG)
                {
                    v.type = Value::String;
                    v.string = std::string(take(type & 0x1f));
                    return v;
                }
                auto readDoubles = [&](Value::Type t, int count) {
                    v.type = t;
                    for (int i = 0; i < count; ++i)
                        v.components.push_back(get<double>());
                };
                switch (static_cast<SerializedType>(type))
                {
                    case SerializedType::NUMBER:
                        v.type = Value::Number;
                        v.number = get<double>();
                        return v;
                    case SerializedType::BOOLEAN:
                        v.type = Value::Boolean;
                        v.boolean = get<char>() != 0;
                        return v;
                    case SerializedType::LONG_STRING:
                        v.type = Value::String;
                        v.string = std::string(take(get<uint32_t>()));
                        return v;
                    case SerializedType::TABLE_START:
                        v.type = Value::Table;
                        while (true)
                        {
                            if (mData.empty())
                                throw FormatError("Unexpected end of serialized Lua table");
                            if (static_cast<unsigned char>(mData[0]) == static_cast<unsigned char>(SerializedType::TABLE_END))
                            {
                                mData.remove_prefix(1);
                                break;
                            }
                            Value key = read(depth + 1);
                            Value value = read(depth + 1);
                            v.entries.emplace_back(std::move(key), std::move(value));
                        }
                        return v;
                    case SerializedType::VEC2:
                        readDoubles(Value::Vec2, 2);
                        return v;
                    case SerializedType::VEC3:
                        readDoubles(Value::Vec3, 3);
                        return v;
                    case SerializedType::VEC4:
                        readDoubles(Value::Vec4, 4);
                        return v;
                    case SerializedType::TRANSFORM_M:
                        readDoubles(Value::TransformM, 16);
                        return v;
                    case SerializedType::TRANSFORM_Q:
                        readDoubles(Value::TransformQ, 4);
                        return v;
                    case SerializedType::COLOR:
                        v.type = Value::Color;
                        for (int i = 0; i < 4; ++i)
                            v.components.push_back(get<float>());
                        return v;
                    default:
                        throw FormatError("Unknown type in serialized Lua data: " + std::to_string(type));
                }
            }

            std::string_view mData;
        };

        class Writer
        {
        public:
            std::string writeRoot(const Value& v)
            {
                mOut.clear();
                mOut.push_back(static_cast<char>(FORMAT_VERSION));
                write(v);
                return std::move(mOut);
            }

        private:
            template <class T>
            void put(T v)
            {
                mOut.append(reinterpret_cast<const char*>(&v), sizeof(T));
            }

            void putType(SerializedType t) { mOut.push_back(static_cast<char>(t)); }

            void write(const Value& v)
            {
                switch (v.type)
                {
                    case Value::Number:
                        putType(SerializedType::NUMBER);
                        put<double>(v.number);
                        break;
                    case Value::Boolean:
                        putType(SerializedType::BOOLEAN);
                        put<char>(v.boolean ? 1 : 0);
                        break;
                    case Value::String:
                        if (v.string.size() < SHORT_STRING_FLAG)
                            mOut.push_back(static_cast<char>(SHORT_STRING_FLAG | v.string.size()));
                        else
                        {
                            putType(SerializedType::LONG_STRING);
                            put<uint32_t>(static_cast<uint32_t>(v.string.size()));
                        }
                        mOut.append(v.string);
                        break;
                    case Value::Table:
                        putType(SerializedType::TABLE_START);
                        for (const auto& [key, value] : v.entries)
                        {
                            write(key);
                            write(value);
                        }
                        putType(SerializedType::TABLE_END);
                        break;
                    case Value::Vec2:
                        putType(SerializedType::VEC2);
                        putDoubles(v, 2);
                        break;
                    case Value::Vec3:
                        putType(SerializedType::VEC3);
                        putDoubles(v, 3);
                        break;
                    case Value::Vec4:
                        putType(SerializedType::VEC4);
                        putDoubles(v, 4);
                        break;
                    case Value::TransformM:
                        putType(SerializedType::TRANSFORM_M);
                        putDoubles(v, 16);
                        break;
                    case Value::TransformQ:
                        putType(SerializedType::TRANSFORM_Q);
                        putDoubles(v, 4);
                        break;
                    case Value::Color:
                        putType(SerializedType::COLOR);
                        for (size_t i = 0; i < 4; ++i)
                            put<float>(static_cast<float>(i < v.components.size() ? v.components[i] : 0.0));
                        break;
                    case Value::Custom:
                        if (v.string.empty() || v.string.size() > 64)
                            throw FormatError("Invalid custom type name in Lua data");
                        if (v.string.size() <= 8 && v.payload.size() < 16)
                            mOut.push_back(static_cast<char>(
                                CUSTOM_COMPACT_FLAG | (v.payload.size() << 3) | (v.string.size() - 1)));
                        else
                        {
                            mOut.push_back(static_cast<char>(CUSTOM_FULL_FLAG | (v.string.size() - 1)));
                            put<uint32_t>(static_cast<uint32_t>(v.payload.size()));
                        }
                        mOut.append(v.string);
                        mOut.append(v.payload);
                        break;
                }
            }

            void putDoubles(const Value& v, size_t count)
            {
                for (size_t i = 0; i < count; ++i)
                    put<double>(i < v.components.size() ? v.components[i] : 0.0);
            }

            std::string mOut;
        };

        // ---- object id encoding ----

        LocalRefNum decodeLocalRefNum(std::string_view payload)
        {
            if (payload.size() != 8)
                throw FormatError("Invalid RefNum in Lua data");
            LocalRefNum r;
            std::memcpy(&r.index, payload.data(), 4);
            std::memcpy(&r.contentFile, payload.data() + 4, 4);
            return r;
        }

        void encodeLocalRefNum(std::string& out, LocalRefNum r)
        {
            out.append(reinterpret_cast<const char*>(&r.index), 4);
            out.append(reinterpret_cast<const char*>(&r.contentFile), 4);
        }

        void encodeNetObject(std::string& out, const NetObjectId& id)
        {
            out.push_back(static_cast<char>(id.kind));
            switch (id.kind)
            {
                case NetObjectId::CONTENT:
                    out.append(reinterpret_cast<const char*>(&id.refNumIndex), 4);
                    out.append(reinterpret_cast<const char*>(&id.contentFile), 4);
                    break;
                case NetObjectId::MP:
                    out.append(reinterpret_cast<const char*>(&id.mpNum), 4);
                    break;
                case NetObjectId::PLAYER:
                    out.append(reinterpret_cast<const char*>(&id.guid), 8);
                    break;
                default:
                    break;
            }
        }

        NetObjectId decodeNetObject(std::string_view& payload)
        {
            auto take = [&](void* dst, size_t n) {
                if (payload.size() < n)
                    throw FormatError("Invalid network object id in Lua data");
                std::memcpy(dst, payload.data(), n);
                payload.remove_prefix(n);
            };
            NetObjectId id;
            uint8_t kind;
            take(&kind, 1);
            id.kind = static_cast<NetObjectId::Kind>(kind);
            switch (id.kind)
            {
                case NetObjectId::CONTENT:
                    take(&id.refNumIndex, 4);
                    take(&id.contentFile, 4);
                    break;
                case NetObjectId::MP:
                    take(&id.mpNum, 4);
                    break;
                case NetObjectId::PLAYER:
                    take(&id.guid, 8);
                    break;
                case NetObjectId::NONE:
                    break;
                default:
                    throw FormatError("Unknown network object id kind in Lua data");
            }
            return id;
        }

        void transformLocalToNet(Value& v, const ToNetObject& toNet)
        {
            if (v.type == Value::Table)
            {
                for (auto& [key, value] : v.entries)
                {
                    transformLocalToNet(key, toNet);
                    transformLocalToNet(value, toNet);
                }
            }
            else if (v.type == Value::Custom && v.string == sLocalObjectTypeName)
            {
                std::string payload;
                encodeNetObject(payload, toNet(decodeLocalRefNum(v.payload)));
                v.string = std::string(sNetObjectTypeName);
                v.payload = std::move(payload);
            }
            else if (v.type == Value::Custom && v.string == sLocalObjectListTypeName)
            {
                if (v.payload.size() % 8 != 0)
                    throw FormatError("Invalid object list in Lua data");
                const uint32_t count = static_cast<uint32_t>(v.payload.size() / 8);
                std::string payload;
                payload.append(reinterpret_cast<const char*>(&count), 4);
                for (uint32_t i = 0; i < count; ++i)
                    encodeNetObject(payload, toNet(decodeLocalRefNum(std::string_view(v.payload).substr(i * 8, 8))));
                v.string = std::string(sNetObjectListTypeName);
                v.payload = std::move(payload);
            }
        }

        void transformNetToLocal(Value& v, const ToLocalObject& toLocal)
        {
            if (v.type == Value::Table)
            {
                for (auto& [key, value] : v.entries)
                {
                    transformNetToLocal(key, toLocal);
                    transformNetToLocal(value, toLocal);
                }
            }
            else if (v.type == Value::Custom && v.string == sNetObjectTypeName)
            {
                std::string_view payload = v.payload;
                std::string out;
                encodeLocalRefNum(out, toLocal(decodeNetObject(payload)));
                v.string = std::string(sLocalObjectTypeName);
                v.payload = std::move(out);
            }
            else if (v.type == Value::Custom && v.string == sNetObjectListTypeName)
            {
                std::string_view payload = v.payload;
                uint32_t count;
                if (payload.size() < 4)
                    throw FormatError("Invalid network object list in Lua data");
                std::memcpy(&count, payload.data(), 4);
                payload.remove_prefix(4);
                std::string out;
                for (uint32_t i = 0; i < count; ++i)
                    encodeLocalRefNum(out, toLocal(decodeNetObject(payload)));
                v.string = std::string(sLocalObjectListTypeName);
                v.payload = std::move(out);
            }
        }

        // ---- JSON ----

        void jsonEscape(std::string& out, std::string_view s)
        {
            out.push_back('"');
            for (unsigned char c : s)
            {
                switch (c)
                {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    default:
                        if (c < 0x20)
                        {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                            out += buf;
                        }
                        else
                            out.push_back(static_cast<char>(c));
                }
            }
            out.push_back('"');
        }

        void jsonNumber(std::string& out, double d)
        {
            if (!std::isfinite(d))
            {
                // JSON has no representation for these; keep them recognizable
                out += std::isnan(d) ? "{\"__type\":\"nan\"}" : (d > 0 ? "{\"__type\":\"inf\"}" : "{\"__type\":\"-inf\"}");
                return;
            }
            char buf[64];
            auto res = std::to_chars(buf, buf + sizeof(buf), d);
            out.append(buf, res.ptr);
        }

        const char* typeNameOf(Value::Type t)
        {
            switch (t)
            {
                case Value::Vec2:
                    return "vec2";
                case Value::Vec3:
                    return "vec3";
                case Value::Vec4:
                    return "vec4";
                case Value::Color:
                    return "color";
                case Value::TransformM:
                    return "transformM";
                case Value::TransformQ:
                    return "transformQ";
                default:
                    return "";
            }
        }

        void writeJsonObjectId(std::string& out, const NetObjectId& id)
        {
            out += "{\"__type\":\"object\",\"kind\":";
            switch (id.kind)
            {
                case NetObjectId::CONTENT:
                    out += "\"content\",\"refNum\":" + std::to_string(id.refNumIndex)
                        + ",\"contentFile\":" + std::to_string(id.contentFile);
                    break;
                case NetObjectId::MP:
                    out += "\"mp\",\"mpNum\":" + std::to_string(id.mpNum);
                    break;
                case NetObjectId::PLAYER:
                    // As a string, because doubles can't hold every 64-bit GUID
                    out += "\"player\",\"guid\":\"" + std::to_string(id.guid) + "\"";
                    break;
                default:
                    out += "\"none\"";
            }
            out += "}";
        }

        void writeJson(std::string& out, const Value& v)
        {
            switch (v.type)
            {
                case Value::Number:
                    jsonNumber(out, v.number);
                    return;
                case Value::Boolean:
                    out += v.boolean ? "true" : "false";
                    return;
                case Value::String:
                    jsonEscape(out, v.string);
                    return;
                case Value::Table:
                {
                    bool allStrings = !v.entries.empty();
                    bool isArray = true;
                    for (size_t i = 0; i < v.entries.size(); ++i)
                    {
                        const Value& key = v.entries[i].first;
                        if (key.type != Value::String)
                            allStrings = false;
                        if (key.type != Value::Number)
                            isArray = false;
                    }
                    if (isArray)
                    {
                        // Lua sequences are serialized in arbitrary order; place values by index
                        std::vector<const Value*> items(v.entries.size(), nullptr);
                        for (const auto& [key, value] : v.entries)
                        {
                            double idx = key.number;
                            if (idx < 1 || idx > static_cast<double>(items.size()) || std::floor(idx) != idx
                                || items[static_cast<size_t>(idx) - 1] != nullptr)
                            {
                                isArray = false;
                                break;
                            }
                            items[static_cast<size_t>(idx) - 1] = &value;
                        }
                        if (isArray)
                        {
                            out.push_back('[');
                            for (size_t i = 0; i < items.size(); ++i)
                            {
                                if (i)
                                    out.push_back(',');
                                writeJson(out, *items[i]);
                            }
                            out.push_back(']');
                            return;
                        }
                    }
                    if (allStrings)
                    {
                        out.push_back('{');
                        bool first = true;
                        for (const auto& [key, value] : v.entries)
                        {
                            if (!first)
                                out.push_back(',');
                            first = false;
                            jsonEscape(out, key.string);
                            out.push_back(':');
                            writeJson(out, value);
                        }
                        out.push_back('}');
                        return;
                    }
                    out += "{\"__pairs\":[";
                    bool first = true;
                    for (const auto& [key, value] : v.entries)
                    {
                        if (!first)
                            out.push_back(',');
                        first = false;
                        out.push_back('[');
                        writeJson(out, key);
                        out.push_back(',');
                        writeJson(out, value);
                        out.push_back(']');
                    }
                    out += "]}";
                    return;
                }
                case Value::Custom:
                {
                    if (v.string == sNetObjectTypeName)
                    {
                        std::string_view payload = v.payload;
                        writeJsonObjectId(out, decodeNetObject(payload));
                        return;
                    }
                    if (v.string == sNetObjectListTypeName)
                    {
                        std::string_view payload = v.payload;
                        uint32_t count = 0;
                        if (payload.size() >= 4)
                            std::memcpy(&count, payload.data(), 4);
                        payload.remove_prefix(std::min<size_t>(4, payload.size()));
                        out += "{\"__type\":\"objectList\",\"objects\":[";
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            if (i)
                                out.push_back(',');
                            writeJsonObjectId(out, decodeNetObject(payload));
                        }
                        out += "]}";
                        return;
                    }
                    throw FormatError("Lua data contains a value that can't be converted to JSON: " + v.string);
                }
                default:
                {
                    out += "{\"__type\":\"";
                    out += typeNameOf(v.type);
                    out += "\",\"v\":[";
                    for (size_t i = 0; i < v.components.size(); ++i)
                    {
                        if (i)
                            out.push_back(',');
                        jsonNumber(out, v.components[i]);
                    }
                    out += "]}";
                    return;
                }
            }
        }

        // Minimal JSON reader producing Values
        class JsonReader
        {
        public:
            explicit JsonReader(std::string_view s)
                : mS(s)
            {
            }

            Value readRoot()
            {
                Value v = read(0);
                skipWs();
                if (mPos != mS.size())
                    fail("trailing characters");
                return v;
            }

        private:
            [[noreturn]] void fail(const std::string& what)
            {
                throw FormatError("Invalid JSON for Lua data at offset " + std::to_string(mPos) + ": " + what);
            }

            void skipWs()
            {
                while (mPos < mS.size() && (mS[mPos] == ' ' || mS[mPos] == '\n' || mS[mPos] == '\r' || mS[mPos] == '\t'))
                    ++mPos;
            }

            bool consume(char c)
            {
                skipWs();
                if (mPos < mS.size() && mS[mPos] == c)
                {
                    ++mPos;
                    return true;
                }
                return false;
            }

            void expect(char c)
            {
                if (!consume(c))
                    fail(std::string("expected '") + c + "'");
            }

            static void appendUtf8(std::string& out, uint32_t cp)
            {
                if (cp < 0x80)
                    out.push_back(static_cast<char>(cp));
                else if (cp < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else if (cp < 0x10000)
                {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }

            uint32_t readHex4()
            {
                if (mPos + 4 > mS.size())
                    fail("bad unicode escape");
                uint32_t cp = 0;
                for (int i = 0; i < 4; ++i)
                {
                    char c = mS[mPos++];
                    cp <<= 4;
                    if (c >= '0' && c <= '9')
                        cp |= c - '0';
                    else if (c >= 'a' && c <= 'f')
                        cp |= c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F')
                        cp |= c - 'A' + 10;
                    else
                        fail("bad unicode escape");
                }
                return cp;
            }

            std::string readString()
            {
                expect('"');
                std::string out;
                while (true)
                {
                    if (mPos >= mS.size())
                        fail("unterminated string");
                    char c = mS[mPos++];
                    if (c == '"')
                        break;
                    if (c != '\\')
                    {
                        out.push_back(c);
                        continue;
                    }
                    if (mPos >= mS.size())
                        fail("unterminated escape");
                    char e = mS[mPos++];
                    switch (e)
                    {
                        case '"':
                        case '\\':
                        case '/':
                            out.push_back(e);
                            break;
                        case 'n':
                            out.push_back('\n');
                            break;
                        case 'r':
                            out.push_back('\r');
                            break;
                        case 't':
                            out.push_back('\t');
                            break;
                        case 'b':
                            out.push_back('\b');
                            break;
                        case 'f':
                            out.push_back('\f');
                            break;
                        case 'u':
                        {
                            uint32_t cp = readHex4();
                            if (cp >= 0xD800 && cp < 0xDC00 && mPos + 6 <= mS.size() && mS[mPos] == '\\'
                                && mS[mPos + 1] == 'u')
                            {
                                mPos += 2;
                                uint32_t low = readHex4();
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            }
                            appendUtf8(out, cp);
                            break;
                        }
                        default:
                            fail("bad escape");
                    }
                }
                return out;
            }

            double readNumber()
            {
                skipWs();
                size_t start = mPos;
                while (mPos < mS.size() && (std::strchr("+-0123456789.eE", mS[mPos]) != nullptr))
                    ++mPos;
                double d = 0;
                auto res = std::from_chars(mS.data() + start, mS.data() + mPos, d);
                if (res.ec != std::errc() || res.ptr != mS.data() + mPos)
                    fail("bad number");
                return d;
            }

            static Value makeString(std::string s)
            {
                Value v;
                v.type = Value::String;
                v.string = std::move(s);
                return v;
            }

            static Value makeNumber(double d)
            {
                Value v;
                v.type = Value::Number;
                v.number = d;
                return v;
            }

            NetObjectId objectFromFields(const std::vector<std::pair<std::string, Value>>& fields)
            {
                NetObjectId id;
                std::string kind;
                for (const auto& [k, v] : fields)
                {
                    if (k == "kind" && v.type == Value::String)
                        kind = v.string;
                    else if (k == "refNum" && v.type == Value::Number)
                        id.refNumIndex = static_cast<uint32_t>(v.number);
                    else if (k == "contentFile" && v.type == Value::Number)
                        id.contentFile = static_cast<int32_t>(v.number);
                    else if (k == "mpNum" && v.type == Value::Number)
                        id.mpNum = static_cast<uint32_t>(v.number);
                    else if (k == "guid" && v.type == Value::String)
                        id.guid = std::stoull(v.string);
                    else if (k == "guid" && v.type == Value::Number)
                        id.guid = static_cast<uint64_t>(v.number);
                }
                if (kind == "content")
                    id.kind = NetObjectId::CONTENT;
                else if (kind == "mp")
                    id.kind = NetObjectId::MP;
                else if (kind == "player")
                    id.kind = NetObjectId::PLAYER;
                else
                    id.kind = NetObjectId::NONE;
                return id;
            }

            Value typedValue(const std::string& type, const std::vector<std::pair<std::string, Value>>& fields)
            {
                Value v;
                v.type = Value::Custom;
                if (type == "object")
                {
                    v.string = std::string(sNetObjectTypeName);
                    encodeNetObject(v.payload, objectFromFields(fields));
                    return v;
                }
                if (type == "objectList")
                {
                    v.string = std::string(sNetObjectListTypeName);
                    for (const auto& [k, list] : fields)
                    {
                        if (k != "objects" || list.type != Value::Table)
                            continue;
                        uint32_t count = static_cast<uint32_t>(list.entries.size());
                        v.payload.append(reinterpret_cast<const char*>(&count), 4);
                        for (const auto& entry : list.entries)
                            v.payload.append(entry.second.payload); // already encoded by the nested "object"
                    }
                    if (v.payload.empty())
                        v.payload.assign(4, '\0');
                    return v;
                }
                if (type == "nan" || type == "inf" || type == "-inf")
                    return makeNumber(type == "nan" ? std::nan("") : (type == "inf" ? INFINITY : -INFINITY));

                static const std::pair<const char*, std::pair<Value::Type, size_t>> types[] = {
                    { "vec2", { Value::Vec2, 2 } },
                    { "vec3", { Value::Vec3, 3 } },
                    { "vec4", { Value::Vec4, 4 } },
                    { "color", { Value::Color, 4 } },
                    { "transformM", { Value::TransformM, 16 } },
                    { "transformQ", { Value::TransformQ, 4 } },
                };
                for (const auto& [name, info] : types)
                {
                    if (type != name)
                        continue;
                    v.type = info.first;
                    for (const auto& [k, comps] : fields)
                    {
                        if (k != "v" || comps.type != Value::Table)
                            continue;
                        std::vector<double> c(info.second, 0.0);
                        for (const auto& [idx, num] : comps.entries)
                        {
                            size_t i = static_cast<size_t>(idx.number) - 1;
                            if (i < c.size() && num.type == Value::Number)
                                c[i] = num.number;
                        }
                        v.components = std::move(c);
                    }
                    if (v.components.empty())
                        v.components.assign(info.second, 0.0);
                    return v;
                }
                fail("unknown __type " + type);
            }

            Value read(int depth)
            {
                if (depth > sMaxDepth)
                    fail("nested too deeply");
                skipWs();
                if (mPos >= mS.size())
                    fail("unexpected end");
                char c = mS[mPos];
                if (c == '"')
                    return makeString(readString());
                if (c == '[')
                {
                    ++mPos;
                    Value v;
                    v.type = Value::Table;
                    if (consume(']'))
                        return v;
                    double index = 1;
                    do
                    {
                        v.entries.emplace_back(makeNumber(index++), read(depth + 1));
                    } while (consume(','));
                    expect(']');
                    return v;
                }
                if (c == '{')
                {
                    ++mPos;
                    std::vector<std::pair<std::string, Value>> fields;
                    if (!consume('}'))
                    {
                        do
                        {
                            skipWs();
                            std::string key = readString();
                            expect(':');
                            fields.emplace_back(std::move(key), read(depth + 1));
                        } while (consume(','));
                        expect('}');
                    }
                    for (const auto& [k, v] : fields)
                    {
                        if (k == "__type" && v.type == Value::String)
                            return typedValue(v.string, fields);
                        if (k == "__pairs" && v.type == Value::Table && fields.size() == 1)
                        {
                            Value table;
                            table.type = Value::Table;
                            for (const auto& [idx, pair] : v.entries)
                            {
                                if (pair.type != Value::Table || pair.entries.size() != 2)
                                    fail("__pairs entries must be [key, value]");
                                const Value* key = nullptr;
                                const Value* value = nullptr;
                                for (const auto& [i, item] : pair.entries)
                                    (i.number == 1 ? key : value) = &item;
                                if (!key || !value)
                                    fail("__pairs entries must be [key, value]");
                                table.entries.emplace_back(*key, *value);
                            }
                            return table;
                        }
                    }
                    Value v;
                    v.type = Value::Table;
                    for (auto& [k, value] : fields)
                        v.entries.emplace_back(makeString(k), std::move(value));
                    return v;
                }
                if (mS.compare(mPos, 4, "true") == 0)
                {
                    mPos += 4;
                    Value v;
                    v.type = Value::Boolean;
                    v.boolean = true;
                    return v;
                }
                if (mS.compare(mPos, 5, "false") == 0)
                {
                    mPos += 5;
                    Value v;
                    v.type = Value::Boolean;
                    return v;
                }
                if (mS.compare(mPos, 4, "null") == 0)
                    fail("null is not representable in Lua data");
                return makeNumber(readNumber());
            }

            std::string_view mS;
            size_t mPos = 0;
        };
    }

    std::string toNetwork(std::string_view localData, const ToNetObject& toNet)
    {
        if (localData.empty())
            return {};
        Value root = Reader(localData).readRoot();
        transformLocalToNet(root, toNet);
        return Writer().writeRoot(root);
    }

    std::string toLocal(std::string_view netData, const ToLocalObject& toLocalFn)
    {
        if (netData.empty())
            return {};
        Value root = Reader(netData).readRoot();
        transformNetToLocal(root, toLocalFn);
        return Writer().writeRoot(root);
    }

    std::string toJson(std::string_view netData)
    {
        if (netData.empty())
            return "null";
        Value root = Reader(netData).readRoot();
        std::string out;
        writeJson(out, root);
        return out;
    }

    std::string fromJson(std::string_view json)
    {
        size_t i = 0;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])))
            ++i;
        if (i == json.size() || json.substr(i, 4) == "null")
            return {};
        Value root = JsonReader(json).readRoot();
        return Writer().writeRoot(root);
    }

    void validate(std::string_view data)
    {
        if (!data.empty())
            Reader(data).readRoot();
    }
}
