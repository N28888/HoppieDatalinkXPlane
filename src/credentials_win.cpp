#include "hoppie/credentials.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include <optional>
#include <string>

namespace hoppie {
namespace {

constexpr wchar_t target[] = L"com.hoppiedatalinkxp.plugin/hoppie-logon";

}  // namespace

std::optional<std::string> loadLogonCredential() {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &credential)) return std::nullopt;
    std::string value(reinterpret_cast<const char*>(credential->CredentialBlob),
                      credential->CredentialBlobSize);
    CredFree(credential);
    return value;
}

bool saveLogonCredential(std::string_view value) {
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(target);
    credential.CredentialBlobSize = static_cast<DWORD>(value.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"HoppieDatalinkXP");
    return CredWriteW(&credential, 0) != FALSE;
}

bool deleteLogonCredential() {
    return CredDeleteW(target, CRED_TYPE_GENERIC, 0) != FALSE ||
           GetLastError() == ERROR_NOT_FOUND;
}

}  // namespace hoppie
