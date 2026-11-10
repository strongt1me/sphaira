#include "game_lang.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "title_info.hpp"

#include <minIni.h>

#include <strings.h>
#include <algorithm>
#include <cctype>
#include <vector>

namespace sphaira::game_lang {
namespace {

constexpr const char* INI_SECTION = "override_config";
constexpr const char* INI_KEY = "override_language";
constexpr const char* INI_REGION_KEY = "override_region";

// Do not confuse I/O errors (or a directory) with an absent config file.
Result FileExists(fs::FsNativeSd& sd, const fs::FsPath& path, bool& exists) {
    FsDirEntryType type{};
    const auto rc = sd.GetEntryType(path, &type);
    exists = false;
    if (rc == FsError_PathNotFound) R_SUCCEED();
    R_TRY(rc);
    R_UNLESS(type == FsDirEntryType_File, Result_FsInvalidType);
    exists = true;
    R_SUCCEED();
}

} // namespace

s64 Get(u64 app_id) {
    const auto path = fs::AppendPath(title::GetContentsPath(app_id), "config.ini");

    char buf[32]{};
    if (ini_gets(INI_SECTION, INI_KEY, "", buf, sizeof(buf), path) < 0) {
        return 0;
    }

    // empty value or unknown code -> system default.
    // atmosphere ignores invalid codes too.
    for (size_t i = 0; i < LANGUAGE_COUNT; i++) {
        if (!strcasecmp(buf, LANGUAGES[i].code)) {
            return static_cast<s64>(i) + 1;
        }
    }

    return 0;
}

Result Set(u64 app_id, s64 index) {
    R_UNLESS(index >= 0 && index <= static_cast<s64>(LANGUAGE_COUNT), Result_GameLangBadIndex);
    if (index) {
        // zero means unknown, in which case any language can be set.
        u32 supported_languages{};
        if (R_SUCCEEDED(title::GetSupportedLanguageFlag(app_id, &supported_languages)) && supported_languages) {
            R_UNLESS(supported_languages & LANGUAGES[index - 1].supported_language_flag, Result_GameLangUnsupported);
        }
    }

    const auto dir = title::GetContentsPath(app_id);
    const auto path = fs::AppendPath(dir, "config.ini");
    fs::FsNativeSd sd;
    bool exists{};
    R_TRY(FileExists(sd, path, exists));
    if (!index && !exists) R_SUCCEED();
    if (index) R_TRY(sd.CreateDirectoryRecursively(dir));

    // minIni keeps unrelated sections and keys intact.
    const auto* language = index ? &LANGUAGES[index - 1] : nullptr;
    R_UNLESS(ini_puts(INI_SECTION, INI_KEY, language ? language->code : nullptr, path), Result_FsStdioFailedToWrite);
    R_UNLESS(ini_puts(INI_SECTION, INI_REGION_KEY, language ? language->region_code : nullptr, path), Result_FsStdioFailedToWrite);

    if (!index) {
        // Remove the empty override section without touching other settings.
        bool still_exists{};
        R_TRY(FileExists(sd, path, still_exists));
        if (!still_exists) R_SUCCEED();

        char key[2]{};
        if (!ini_getkey(INI_SECTION, 0, key, sizeof(key), path)) {
            R_UNLESS(ini_puts(INI_SECTION, nullptr, nullptr, path), Result_FsStdioFailedToWrite);
        }

        std::vector<u8> data;
        R_TRY(sd.read_entire_file(path, data));
        if (std::ranges::all_of(data, [](u8 c) { return std::isspace(c); })) {
            R_TRY(sd.DeleteFile(path));
        }
    }

    log_write("[GAMELANG] saved language/region override for %016lX: %s / %s\n", app_id, language ? language->code : "default", language ? language->region_code : "default");
    R_SUCCEED();
}

} // namespace sphaira::game_lang
