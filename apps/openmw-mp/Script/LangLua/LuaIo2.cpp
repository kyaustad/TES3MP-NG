#include "LuaIo2.hpp"

/*
    Built-in replacement for TES3MP's Lua-io2 module (https://github.com/TES3MP/Lua-io2), which CoreScripts uses
    on Windows because Lua's own io library can't open Unicode filenames there. Embedding it removes the need
    to ship a separately compiled io2.dll that matches the server's Lua runtime.

    Supported: io2.open(path, mode) -> file with read/write/lines/seek/close, plus io2.mkdir, io2.rm, io2.rmall,
    io2.isdir and io2.dir. All paths are UTF-8.
*/

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include <components/files/conversion.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace
{
    constexpr const char *sFileMetatable = "tes3mp.io2.file";

    struct FileHandle
    {
        FILE *file = nullptr;
    };

    FILE *openFile(const char *path, const char *mode)
    {
#ifdef _WIN32
        std::wstring wpath = Files::pathFromUnicodeString(path).wstring();
        std::wstring wmode(mode, mode + std::strlen(mode));
        return _wfopen(wpath.c_str(), wmode.c_str());
#else
        return std::fopen(path, mode);
#endif
    }

    FileHandle *checkFile(lua_State *L)
    {
        auto *handle = static_cast<FileHandle *>(luaL_checkudata(L, 1, sFileMetatable));
        if (handle->file == nullptr)
            luaL_error(L, "attempt to use a closed file");
        return handle;
    }

    bool readLine(lua_State *L, FILE *file)
    {
        luaL_Buffer buffer;
        luaL_buffinit(L, &buffer);
        int c;
        bool readAnything = false;
        while ((c = std::fgetc(file)) != EOF)
        {
            readAnything = true;
            if (c == '\n')
                break;
            luaL_addchar(&buffer, static_cast<char>(c));
        }
        luaL_pushresult(&buffer);
        return readAnything;
    }

    void readAll(lua_State *L, FILE *file)
    {
        luaL_Buffer buffer;
        luaL_buffinit(L, &buffer);
        char chunk[LUAL_BUFFERSIZE];
        size_t n;
        while ((n = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
            luaL_addlstring(&buffer, chunk, n);
        luaL_pushresult(&buffer);
    }

    int fileRead(lua_State *L)
    {
        FILE *file = checkFile(L)->file;
        const int nargs = lua_gettop(L) - 1;
        if (nargs == 0)
        {
            if (!readLine(L, file))
            {
                lua_pop(L, 1);
                lua_pushnil(L);
            }
            return 1;
        }
        for (int i = 2; i <= nargs + 1; ++i)
        {
            if (lua_type(L, i) == LUA_TNUMBER)
            {
                size_t count = static_cast<size_t>(lua_tointeger(L, i));
                std::string data(count, '\0');
                size_t read = std::fread(data.data(), 1, count, file);
                if (read == 0 && count > 0)
                    lua_pushnil(L);
                else
                    lua_pushlstring(L, data.data(), read);
                continue;
            }
            const char *format = luaL_checkstring(L, i);
            if (*format == '*')
                ++format;
            switch (*format)
            {
                case 'a':
                    readAll(L, file);
                    break;
                case 'l':
                    if (!readLine(L, file))
                    {
                        lua_pop(L, 1);
                        lua_pushnil(L);
                    }
                    break;
                case 'n':
                {
                    double number;
                    if (std::fscanf(file, "%lf", &number) == 1)
                        lua_pushnumber(L, number);
                    else
                        lua_pushnil(L);
                    break;
                }
                default:
                    return luaL_argerror(L, i, "invalid format");
            }
        }
        return nargs;
    }

    int fileWrite(lua_State *L)
    {
        FILE *file = checkFile(L)->file;
        const int top = lua_gettop(L);
        for (int i = 2; i <= top; ++i)
        {
            size_t len;
            const char *data = luaL_checklstring(L, i, &len);
            if (std::fwrite(data, 1, len, file) != len)
            {
                lua_pushnil(L);
                lua_pushstring(L, "write failed");
                return 2;
            }
        }
        lua_pushvalue(L, 1);
        return 1;
    }

    int fileSeek(lua_State *L)
    {
        FILE *file = checkFile(L)->file;
        const char *whence = luaL_optstring(L, 2, "cur");
        const long offset = static_cast<long>(luaL_optinteger(L, 3, 0));
        int origin = SEEK_CUR;
        if (std::strcmp(whence, "set") == 0)
            origin = SEEK_SET;
        else if (std::strcmp(whence, "end") == 0)
            origin = SEEK_END;
        if (std::fseek(file, offset, origin) != 0)
        {
            lua_pushnil(L);
            lua_pushstring(L, "seek failed");
            return 2;
        }
        lua_pushinteger(L, static_cast<lua_Integer>(std::ftell(file)));
        return 1;
    }

    int fileClose(lua_State *L)
    {
        auto *handle = static_cast<FileHandle *>(luaL_checkudata(L, 1, sFileMetatable));
        if (handle->file != nullptr)
        {
            std::fclose(handle->file);
            handle->file = nullptr;
        }
        lua_pushboolean(L, 1);
        return 1;
    }

    int linesIterator(lua_State *L)
    {
        auto *handle = static_cast<FileHandle *>(lua_touserdata(L, lua_upvalueindex(1)));
        if (handle->file == nullptr || !readLine(L, handle->file))
        {
            lua_pushnil(L);
            return 1;
        }
        return 1;
    }

    int fileLines(lua_State *L)
    {
        checkFile(L);
        lua_pushvalue(L, 1);
        lua_pushcclosure(L, linesIterator, 1);
        return 1;
    }

    int ioOpen(lua_State *L)
    {
        const char *path = luaL_checkstring(L, 1);
        const char *mode = luaL_optstring(L, 2, "r");
        FILE *file = openFile(path, mode);
        if (file == nullptr)
        {
            lua_pushnil(L);
            lua_pushfstring(L, "%s: could not open file", path);
            return 2;
        }
        auto *handle = static_cast<FileHandle *>(lua_newuserdata(L, sizeof(FileHandle)));
        handle->file = file;
        luaL_getmetatable(L, sFileMetatable);
        lua_setmetatable(L, -2);
        return 1;
    }

    int ioClose(lua_State *L)
    {
        return fileClose(L);
    }

    int ioMkdir(lua_State *L)
    {
        std::error_code ec;
        lua_pushboolean(L, std::filesystem::create_directories(Files::pathFromUnicodeString(luaL_checkstring(L, 1)), ec));
        return 1;
    }

    int ioRm(lua_State *L)
    {
        std::error_code ec;
        lua_pushboolean(L, std::filesystem::remove(Files::pathFromUnicodeString(luaL_checkstring(L, 1)), ec));
        return 1;
    }

    int ioRmall(lua_State *L)
    {
        std::error_code ec;
        auto count = std::filesystem::remove_all(Files::pathFromUnicodeString(luaL_checkstring(L, 1)), ec);
        lua_pushinteger(L, ec ? 0 : static_cast<lua_Integer>(count));
        return 1;
    }

    int ioIsdir(lua_State *L)
    {
        std::error_code ec;
        lua_pushboolean(L, std::filesystem::is_directory(Files::pathFromUnicodeString(luaL_checkstring(L, 1)), ec));
        return 1;
    }

    int ioDir(lua_State *L)
    {
        std::error_code ec;
        const std::filesystem::path dir = Files::pathFromUnicodeString(luaL_optstring(L, 1, "."));
        lua_newtable(L);
        int index = 1;
        for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
        {
            const std::string name = Files::pathToUnicodeString(entry.path().filename());
            lua_pushlstring(L, name.data(), name.size());
            lua_rawseti(L, -2, index++);
        }
        return 1;
    }

    int luaopenIo2(lua_State *L)
    {
        if (luaL_newmetatable(L, sFileMetatable))
        {
            static const luaL_Reg methods[] = {
                { "read", fileRead },
                { "write", fileWrite },
                { "seek", fileSeek },
                { "lines", fileLines },
                { "close", fileClose },
                { nullptr, nullptr },
            };
            lua_newtable(L);
            luaL_register(L, nullptr, methods);
            lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, fileClose);
            lua_setfield(L, -2, "__gc");
        }
        lua_pop(L, 1);

        static const luaL_Reg functions[] = {
            { "open", ioOpen },
            { "close", ioClose },
            { "mkdir", ioMkdir },
            { "rm", ioRm },
            { "rmall", ioRmall },
            { "isdir", ioIsdir },
            { "dir", ioDir },
            { nullptr, nullptr },
        };
        lua_newtable(L);
        luaL_register(L, nullptr, functions);
        return 1;
    }
}

void LuaIo2::registerPreload(lua_State *L)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, luaopenIo2);
    lua_setfield(L, -2, "io2");
    lua_pop(L, 2);
}
