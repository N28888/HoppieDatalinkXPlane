#include "hoppie/credentials.hpp"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace hoppie {
namespace {

NSDictionary* baseQuery() {
    return @{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
             (__bridge id)kSecAttrService: @"com.hoppiedatalinkxp.plugin",
             (__bridge id)kSecAttrAccount: @"hoppie-logon"};
}

}  // namespace

std::optional<std::string> loadLogonCredential() {
    @autoreleasepool {
        NSMutableDictionary* query = [baseQuery() mutableCopy];
        query[(__bridge id)kSecReturnData] = @YES;
        query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        CFTypeRef output = nullptr;
        if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &output) != errSecSuccess)
            return std::nullopt;
        NSData* data = (__bridge_transfer NSData*)output;
        return std::string(static_cast<const char*>(data.bytes), data.length);
    }
}

bool saveLogonCredential(std::string_view value) {
    @autoreleasepool {
        NSData* data = [NSData dataWithBytes:value.data() length:value.size()];
        NSDictionary* update = @{(__bridge id)kSecValueData: data};
        const OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)baseQuery(),
                                              (__bridge CFDictionaryRef)update);
        if (status == errSecSuccess) return true;
        if (status != errSecItemNotFound) return false;
        NSMutableDictionary* add = [baseQuery() mutableCopy];
        add[(__bridge id)kSecValueData] = data;
        return SecItemAdd((__bridge CFDictionaryRef)add, nullptr) == errSecSuccess;
    }
}

bool deleteLogonCredential() {
    const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)baseQuery());
    return status == errSecSuccess || status == errSecItemNotFound;
}

}  // namespace hoppie
