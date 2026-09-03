#include "hoppie/network.hpp"

#import <Foundation/Foundation.h>

@interface HoppieHttpsDelegate : NSObject <NSURLSessionTaskDelegate>
@end

@implementation HoppieHttpsDelegate
- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
willPerformHTTPRedirection:(NSHTTPURLResponse*)response
        newRequest:(NSURLRequest*)request
  completionHandler:(void (^)(NSURLRequest* _Nullable))completionHandler {
    (void)session;
    (void)task;
    (void)response;
    completionHandler([[request.URL scheme] isEqualToString:@"https"] ? request : nil);
}
@end

namespace hoppie {

NetworkResult performPlatformHttps(const NetworkTask& task) {
    NetworkResult result;
    result.id = task.id;
    result.purpose = task.purpose;
    @autoreleasepool {
        NSString* urlText = [[NSString alloc] initWithBytes:task.url.data()
                                                    length:task.url.size()
                                                  encoding:NSUTF8StringEncoding];
        NSURL* url = [NSURL URLWithString:urlText];
        if (url == nil || ![[url scheme] isEqualToString:@"https"]) {
            result.errorCategory = "https_required";
            return result;
        }
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url
                                                               cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                           timeoutInterval:task.timeoutSeconds];
        [request setHTTPMethod:task.method == HttpMethod::Post ? @"POST" : @"GET"];
        [request setValue:@"HoppieDatalinkXP/1.0" forHTTPHeaderField:@"User-Agent"];
        if (task.method == HttpMethod::Post) {
            NSString* contentType = [[NSString alloc] initWithBytes:task.contentType.data()
                                                            length:task.contentType.size()
                                                          encoding:NSUTF8StringEncoding];
            [request setValue:contentType forHTTPHeaderField:@"Content-Type"];
            [request setHTTPBody:[NSData dataWithBytes:task.body.data() length:task.body.size()]];
        }

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block NSData* responseData = nil;
        __block NSURLResponse* response = nil;
        __block NSError* requestError = nil;
        HoppieHttpsDelegate* delegate = [[HoppieHttpsDelegate alloc] init];
        NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration
                                                              delegate:delegate
                                                         delegateQueue:nil];
        NSURLSessionDataTask* dataTask = [session
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* reply, NSError* error) {
                responseData = data;
                response = reply;
                requestError = error;
                dispatch_semaphore_signal(done);
              }];
        [dataTask resume];
        const auto deadline = dispatch_time(DISPATCH_TIME_NOW,
            static_cast<int64_t>((task.timeoutSeconds + 1) * NSEC_PER_SEC));
        if (dispatch_semaphore_wait(done, deadline) != 0) {
            [dataTask cancel];
            [session invalidateAndCancel];
            result.errorCategory = "timeout";
            return result;
        }
        [session finishTasksAndInvalidate];
        if (requestError != nil) {
            result.errorCategory = requestError.code == NSURLErrorTimedOut ? "timeout" : "transport";
            return result;
        }
        if ([response isKindOfClass:[NSHTTPURLResponse class]])
            result.httpStatus = static_cast<int>([(NSHTTPURLResponse*)response statusCode]);
        if (![[response.URL scheme] isEqualToString:@"https"]) {
            result.errorCategory = "https_required";
            return result;
        }
        if (responseData.length > 16u * 1024u * 1024u) {
            result.errorCategory = "response_too_large";
            return result;
        }
        NSString* body = [[NSString alloc] initWithData:responseData encoding:NSUTF8StringEncoding];
        if (body != nil) result.body.assign([body UTF8String]);
        result.transportOk = true;
    }
    return result;
}

}  // namespace hoppie
