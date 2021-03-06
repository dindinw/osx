/*
 * Copyright (c) 2012-2014 Apple Inc. All Rights Reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */


#include <utilities/SecCFError.h>
#include <utilities/SecCFRelease.h>
#include <utilities/debugging.h>
#include <notify.h>


//
// OSStatus values we magically know
//
enum {
    parameterError                = -50,     /* One or more parameters passed to a function were not valid. */
    allocationError               = -108,    /* Failed to allocate memory. */
};

bool SecKernError(kern_return_t result, CFErrorRef *error, CFStringRef format, ...) {
    if (!result) return true;
    if (error) {
        va_list args;
        CFIndex code = result;
        CFErrorRef previousError = *error;

        *error = NULL;
        va_start(args, format);
        SecCFCreateErrorWithFormatAndArguments(code, kSecKernDomain, previousError, error, NULL, format, args);
        va_end(args);
    }
    return false;
}

bool SecCheckErrno(int result, CFErrorRef *error, CFStringRef format, ...) {
    if (result == 0) return true;
    if (error) {
        va_list args;
        int errnum = errno;
        CFIndex code = errnum;
        CFErrorRef previousError = *error;

        *error = NULL;
        va_start(args, format);
        CFStringRef message = CFStringCreateWithFormatAndArguments(kCFAllocatorDefault, NULL, format, args);
        va_end(args);
        SecCFCreateErrorWithFormat(code, kSecErrnoDomain, previousError, error, NULL, CFSTR("%@: [%d] %s"), message, errnum, strerror(errnum));
        CFReleaseSafe(message);
    }
    return false;
}

bool SecPOSIXError(int error, CFErrorRef *cferror, CFStringRef format, ...)
{
	if (error == 0) return true;
	if (error) {
		va_list args;
		CFIndex code = error;
		CFErrorRef previousError = *cferror;

		*cferror = NULL;
		va_start(args, format);
		SecCFCreateErrorWithFormatAndArguments(code, kSecErrnoDomain, previousError, cferror, NULL, format, args);
		va_end(args);
	}
	return false;
}

bool SecCoreCryptoError(int error, CFErrorRef *cferror, CFStringRef format, ...)
{
	if (error == 0) return true;
	if (error) {
		va_list args;
		CFIndex code = error;
		CFErrorRef previousError = *cferror;

		*cferror = NULL;
		va_start(args, format);
		SecCFCreateErrorWithFormatAndArguments(code, kSecCoreCryptoDomain, previousError, cferror, NULL, format, args);
		va_end(args);
	}
	return false;
}


bool SecNotifyError(uint32_t result, CFErrorRef *error, CFStringRef format, ...) {
    if (result == NOTIFY_STATUS_OK) return true;
    if (error) {
        va_list args;
        CFIndex code = result;
        CFErrorRef previousError = *error;

        *error = NULL;
        va_start(args, format);
        SecCFCreateErrorWithFormatAndArguments(code, kSecNotifyDomain, previousError, error, NULL, format, args);
        va_end(args);
    }
    return false;
}

bool SecError(OSStatus status, CFErrorRef *error, CFStringRef format, ...) {
    if (status == 0) return true;
    if (error) {
        va_list args;
        CFIndex code = status;
        CFErrorRef previousError = *error;

        *error = NULL;
        va_start(args, format);
        SecCFCreateErrorWithFormatAndArguments(code, kSecErrorDomain, previousError, error, NULL, format, args);
        va_end(args);
    }
    return false;
}

// Parameter error
bool SecRequirementError(bool requirement, CFErrorRef *error, CFStringRef format, ...) {
    if (requirement) return true;
    if (error) {
        va_list args;
        CFErrorRef previousError = *error;

        *error = NULL;
        va_start(args, format);
        SecCFCreateErrorWithFormatAndArguments(parameterError, kSecErrorDomain, previousError, error, NULL, format, args);
        va_end(args);
    }
    return false;
}

// Allocation failure
bool SecAllocationError(const void *allocated, CFErrorRef *error, CFStringRef format, ...) {
    if (allocated) return true;
    if (error) {
        va_list args;
        CFErrorRef previousError = *error;

        *error = NULL;
        va_start(args, format);
        SecCFCreateErrorWithFormatAndArguments(allocationError, kSecErrorDomain, previousError, error, NULL, format, args);
        va_end(args);
    }
    return false;
}

bool SecCFCreateErrorWithFormat(CFIndex errorCode, CFStringRef domain, CFErrorRef previousError, CFErrorRef *newError,
                                CFDictionaryRef formatoptions, CFStringRef format, ...)
{
    va_list args;
    va_start(args, format);

    bool result = SecCFCreateErrorWithFormatAndArguments(errorCode, domain, previousError, newError, formatoptions, format, args);

    va_end(args);

    return result;
}

bool SecCFCreateErrorWithFormatAndArguments(CFIndex errorCode, CFStringRef domain,
                                            CF_CONSUMED CFErrorRef previousError, CFErrorRef *newError,
                                            CFDictionaryRef formatoptions, CFStringRef format, va_list args)
{
    if (newError && !(*newError)) {
        CFStringRef formattedString = CFStringCreateWithFormatAndArguments(NULL, formatoptions, format, args);
        
        const void* keys[2] =   { kCFErrorDescriptionKey,   kCFErrorUnderlyingErrorKey};
        const void* values[2] = { formattedString,          previousError };
        const CFIndex numEntriesToUse = (previousError != NULL) ? 2 : 1;

        *newError = CFErrorCreateWithUserInfoKeysAndValues(kCFAllocatorDefault, domain, errorCode,
                                                           keys, values, numEntriesToUse);

        CFReleaseNull(formattedString);
        if (previousError)
            secdebug("error_thee_well", "encapsulated %@ with new error: %@", previousError, *newError);
        CFReleaseNull(previousError);
    } else {
        if (previousError && newError && (previousError != *newError)) {
            secdebug("error_thee_well", "dropping %@", previousError);
            CFRelease(previousError);
        }
    }

    if (newError)
        secdebug("error_thee_well", "SecError: %@", *newError);


    return false;
}
