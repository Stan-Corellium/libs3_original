/** **************************************************************************
 * s3.c
 * 
 * Copyright 2008 Bryan Ischo <bryan@ischo.com>
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the
 *
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 ************************************************************************** **/

/**
 * This is a 'driver' program that simply converts command-line input into
 * calls to libs3 functions, and prints the results.
 **/

#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "libs3.h"


// Command-line options, saved as globals ------------------------------------

static int showResponseHeadersG = 0;
static S3Protocol protocolG = S3ProtocolHTTPS;
static S3UriStyle uriStyleG = S3UriStyleVirtualHost;
// request headers stuff
// acl stuff


// Environment variables, saved as globals ----------------------------------

static const char *accessKeyIdG = 0;
static const char *secretAccessKeyG = 0;


// Request results, saved as globals -----------------------------------------

static int statusG = 0, httpResponseCodeG = 0;
static const S3ErrorDetails *errorG = 0;


// Option prefixes -----------------------------------------------------------

#define LOCATION_CONSTRAINT_PREFIX "locationConstraint="
#define LOCATION_CONSTRAINT_PREFIX_LEN (sizeof(LOCATION_CONSTRAINT_PREFIX) - 1)
#define CANNED_ACL_PREFIX "cannedAcl="
#define CANNED_ACL_PREFIX_LEN (sizeof(CANNED_ACL_PREFIX) - 1)
#define PREFIX_PREFIX "prefix="
#define PREFIX_PREFIX_LEN (sizeof(PREFIX_PREFIX) - 1)
#define MARKER_PREFIX "marker="
#define MARKER_PREFIX_LEN (sizeof(MARKER_PREFIX) - 1)
#define DELIMITER_PREFIX "delimiter="
#define DELIMITER_PREFIX_LEN (sizeof(DELIMITER_PREFIX) - 1)
#define MAXKEYS_PREFIX "maxkeys="
#define MAXKEYS_PREFIX_LEN (sizeof(MAXKEYS_PREFIX) - 1)
#define FILENAME_PREFIX "filename="
#define FILENAME_PREFIX_LEN (sizeof(FILENAME_PREFIX) - 1)
#define CONTENT_LENGTH_PREFIX "contentLength="
#define CONTENT_LENGTH_PREFIX_LEN (sizeof(CONTENT_LENGTH_PREFIX) - 1)
#define CACHE_CONTROL_PREFIX "cacheControl="
#define CACHE_CONTROL_PREFIX_LEN (sizeof(CACHE_CONTROL_PREFIX) - 1)
#define CONTENT_TYPE_PREFIX "contentType="
#define CONTENT_TYPE_PREFIX_LEN (sizeof(CONTENT_TYPE_PREFIX) - 1)
#define MD5_PREFIX "md5="
#define MD5_PREFIX_LEN (sizeof(MD5_PREFIX) - 1)
#define CONTENT_DISPOSITION_FILENAME_PREFIX "contentDispositionFilename="
#define CONTENT_DISPOSITION_FILENAME_PREFIX_LEN \
    (sizeof(CONTENT_DISPOSITION_FILENAME_PREFIX) - 1)
#define CONTENT_ENCODING_PREFIX "contentEncoding="
#define CONTENT_ENCODING_PREFIX_LEN (sizeof(CONTENT_ENCODING_PREFIX) - 1)
#define VALID_DURATION_PREFIX "validDuration="
#define VALID_DURATION_PREFIX_LEN (sizeof(VALID_DURATION_PREFIX) - 1)
#define X_AMZ_META_PREFIX "x-amz-meta-"
#define X_AMZ_META_PREFIX_LEN (sizeof(X_AMZ_META_PREFIX) - 1)


// libs3 mutex stuff ---------------------------------------------------------

struct S3Mutex
{
    pthread_mutex_t m;
};


static unsigned long threadSelfCallback()
{
    return pthread_self();
}


static struct S3Mutex *mutexCreateCallback()
{
    struct S3Mutex *mutex = (struct S3Mutex *) malloc(sizeof(struct S3Mutex));
    
    pthread_mutex_init(&(mutex->m), NULL);

    return mutex;
}


static void mutexLockCallback(struct S3Mutex *mutex)
{
    pthread_mutex_lock(&(mutex->m));
}


static void mutexUnlockCallback(struct S3Mutex *mutex)
{
    pthread_mutex_unlock(&(mutex->m));
}


static void mutexDestroyCallback(struct S3Mutex *mutex)
{
    pthread_mutex_destroy(&(mutex->m));
    free(mutex);
}


// util ----------------------------------------------------------------------

static void S3_init()
{
    S3Status status;
    if ((status = S3_initialize("s3", &threadSelfCallback, 
                                &mutexCreateCallback,
                                &mutexLockCallback, &mutexUnlockCallback,
                                &mutexDestroyCallback)) != S3StatusOK) {
        fprintf(stderr, "Failed to initialize libs3: %d\n", status);
        exit(-1);
    }
}


static void printError()
{
    if (statusG < S3StatusErrorAccessDenied) {
        fprintf(stderr, "ERROR: %s\n", S3_get_status_name(statusG));
    }
    else {
        fprintf(stderr, "ERROR: S3 returned an unexpected error:\n");
        fprintf(stderr, "  HTTP Code: %d\n", httpResponseCodeG);
        fprintf(stderr, "  S3 Error: %s\n", S3_get_status_name(statusG));
        if (errorG) {
            if (errorG->message) {
                fprintf(stderr, "  Message: %s\n", errorG->message);
            }
            if (errorG->resource) {
                fprintf(stderr, "  Resource: %s\n", errorG->resource);
            }
            if (errorG->furtherDetails) {
                fprintf(stderr, "  Further Details: %s\n", 
                        errorG->furtherDetails);
            }
            if (errorG->extraDetailsCount) {
                printf("  Extra Details:\n");
                int i;
                for (i = 0; i < errorG->extraDetailsCount; i++) {
                    printf("    %s: %s\n", errorG->extraDetails[i].name,
                           errorG->extraDetails[i].value);
                }
            }
        }
    }
}


static void usageExit(FILE *out)
{
    fprintf(out,
" Options:\n"
"\n"
"   Command Line:\n"
"\n"   
"   -p : use path-style URIs (--path-style)\n"
"   -u : unencrypted (use HTTP instead of HTTPS) (--https)\n"
"   -s : show response headers (--show-headers)\n"
"\n"
"   Environment:\n"
"\n"
"   S3_ACCESS_KEY_ID : S3 access key ID\n"
"   S3_SECRET_ACCESS_KEY : S3 secret access key\n"
"\n" 
" Commands:\n"
"\n"
"   help\n"            
"   list\n"
"   test <bucket>\n"
"   create <bucket> [cannedAcl, locationConstraint]\n"
"   delete <bucket>\n"
"   list <bucket> [prefix, marker, delimiter, maxkeys]\n"
"   put <bucket>/<key> [filename, contentLength, cacheControl, contentType,\n"
"                       md5, contentDispositionFilename, contentEncoding,\n"
"                       validDuration, cannedAcl, [x-amz-meta-...]]\n"
"   copy <sourcebucket>/<sourcekey> <destbucket>/<destkey> [headers]\n"
"   get <buckey>/<key> [filename (required if -r is used), if-modified-since,\n"
"                       if-unmodified-since, if-match, if-not-match,\n"
"                       range-start, range-end]\n"
"   head <bucket>/<key>\n"
"   delete <bucket>/<key>\n"
"   todo : acl stuff\n"
"\n");

    exit(-1);
}


static uint64_t convertInt(const char *str, const char *paramName)
{
    uint64_t ret = 0;

    while (*str) {
        if (!isdigit(*str)) {
            fprintf(stderr, "ERROR: Nondigit in %s parameter: %c\n", 
                    paramName, *str);
            usageExit(stderr);
        }
        ret *= 10;
        ret += (*str++ - '0');
    }

    return ret;
}


typedef struct growbuffer
{
    // The total number of bytes, and the start byte
    int size;
    // The start byte
    int start;
    // The blocks
    char data[64 * 1024];
    struct growbuffer *prev, *next;
} growbuffer;


// returns nonzero on success, zero on out of memory
static int growbuffer_append(growbuffer **gb, const char *data, int dataLen)
{
    while (dataLen) {
        growbuffer *buf = *gb ? (*gb)->prev : 0;
        if (!buf || (buf->size == sizeof(buf->data))) {
            buf = (growbuffer *) malloc(sizeof(growbuffer));
            if (!buf) {
                return 0;
            }
            buf->size = 0;
            buf->start = 0;
            if (*gb) {
                buf->prev = (*gb)->prev;
                buf->next = *gb;
                (*gb)->prev->next = buf;
                (*gb)->prev = buf;
            }
            else {
                buf->prev = buf->next = buf;
                *gb = buf;
            }
        }

        int toCopy = (sizeof(buf->data) - buf->size);
        if (toCopy > dataLen) {
            toCopy = dataLen;
        }

        memcpy(&(buf->data[buf->size]), data, toCopy);
        
        buf->size += toCopy, data += toCopy, dataLen -= toCopy;
    }

    return 1;
}


static void growbuffer_read(growbuffer **gb, int amt, int *amtReturn, 
                            char *buffer)
{
    *amtReturn = 0;

    growbuffer *buf = *gb;

    if (!buf) {
        return;
    }

    *amtReturn = (buf->size > amt) ? amt : buf->size;

    memcpy(buffer, &(buf->data[buf->start]), *amtReturn);
    
    buf->start += *amtReturn, buf->size -= *amtReturn;

    if (buf->size == 0) {
        if (buf->next == buf) {
            *gb = 0;
        }
        else {
            *gb = buf->next;
        }
        free(buf);
    }
}


static void growbuffer_destroy(growbuffer *gb)
{
    growbuffer *start = gb;

    while (gb) {
        growbuffer *next = gb->next;
        free(gb);
        gb = (next == start) ? 0 : next;
    }
}


static struct option longOptionsG[] =
{
    { "path-style",           no_argument      ,  0,  'p' },
    { "unencrypted",          no_argument      ,  0,  'u' },
    { "show-headers",         no_argument,        0,  's' },
    { 0,                      0,                  0,   0  }
};


// response header callback --------------------------------------------------

// This callback does the same thing for every request type: prints out the
// headers if the user has requested them to be so
static S3Status responseHeadersCallback(const S3ResponseHeaders *headers,
                                        void *callbackData)
{
    if (!showResponseHeadersG) {
        return S3StatusOK;
    }

#define print_nonnull(name, field)                              \
    do {                                                        \
        if (headers-> field) {                                  \
            printf("%s: %s\n", name, headers-> field);          \
        }                                                       \
    } while (0)
    
    print_nonnull("Request-Id", requestId);
    print_nonnull("Request-Id-2", requestId2);
    if (headers->contentLength > 0) {
        printf("Content-Length: %lld\n", headers->contentLength);
    }
    print_nonnull("Server", server);
    print_nonnull("ETag", eTag);
    if (headers->lastModified > 0) {
        char timebuf[256];
        // localtime is not thread-safe but we don't care here.  xxx note -
        // localtime doesn't seem to actually do anything, 0 locatime of 0
        // returns EST Unix epoch, it should return the NZST equivalent ...
        strftime(timebuf, sizeof(timebuf), "%Y/%m/%d %H:%M:%S %Z",
                 localtime(&(headers->lastModified)));
        printf("Last-Modified: %s\n", timebuf);
    }
    int i;
    for (i = 0; i < headers->metaHeadersCount; i++) {
        printf("x-amz-meta-%s: %s\n", headers->metaHeaders[i].name,
               headers->metaHeaders[i].value);
    }

    return S3StatusOK;
}


// response complete callback ------------------------------------------------

// This callback does the same thing for every request type: saves the status
// and error stuff in global variables
static void responseCompleteCallback(S3Status status, int httpResponseCode,
                                     const S3ErrorDetails *error, 
                                     void *callbackData)
{
    statusG = status;
    httpResponseCodeG = httpResponseCode;
    errorG = error;
}


// list service --------------------------------------------------------------

static S3Status listServiceCallback(const char *ownerId, 
                                    const char *ownerDisplayName,
                                    const char *bucketName,
                                    time_t creationDate, void *callbackData)
{
    static int ownerPrinted = 0;

    if (!ownerPrinted) {
        printf("Owner ID: %s\n", ownerId);
        printf("Owner Display Name: %s\n", ownerDisplayName);
        ownerPrinted = 1;
    }

    printf("Bucket Name: %s\n", bucketName);
    if (creationDate >= 0) {
        char timebuf[256];
        // localtime is not thread-safe but we don't care here.  xxx note -
        // localtime doesn't seem to actually do anything, 0 locatime of 0
        // returns EST Unix epoch, it should return the NZST equivalent ...
        strftime(timebuf, sizeof(timebuf), "%Y/%m/%d %H:%M:%S %Z",
                 localtime(&creationDate));
        printf("Creation Date: %s\n", timebuf);
    }

    return S3StatusOK;
}


static void list_service()
{
    S3_init();

    S3ListServiceHandler listServiceHandler =
    {
        { &responseHeadersCallback, &responseCompleteCallback },
        &listServiceCallback
    };

    S3_list_service(protocolG, accessKeyIdG, secretAccessKeyG, 0, 
                    &listServiceHandler, 0);

    if (statusG != S3StatusOK) {
        printError();
    }

    S3_deinitialize();
}


// test bucket ---------------------------------------------------------------

static void test_bucket(int argc, char **argv, int optind)
{
    // test bucket
    if (optind == argc) {
        fprintf(stderr, "ERROR: Missing parameter: bucket\n");
        usageExit(stderr);
    }

    const char *bucketName = argv[optind++];

    if (optind != argc) {
        fprintf(stderr, "ERROR: Extraneous parameter: %s\n", argv[optind]);
        usageExit(stderr);
    }

    S3_init();

    S3ResponseHandler responseHandler =
    {
        &responseHeadersCallback, &responseCompleteCallback
    };

    char locationConstraint[64];
    S3_test_bucket(protocolG, uriStyleG, accessKeyIdG, secretAccessKeyG,
                   bucketName, sizeof(locationConstraint), locationConstraint,
                   0, &responseHandler, 0);

    switch (statusG) {
    case S3StatusOK:
        // bucket exists
        printf("Bucket '%s' exists", bucketName);
        if (locationConstraint[0]) {
            printf(" in location %s\n", locationConstraint);
        }
        else {
            printf(".\n");
        }
        break;
    case S3StatusErrorNoSuchBucket:
        // bucket does not exist
        printf("Bucket '%s' does not exist.\n", bucketName);
        break;
    case S3StatusErrorAccessDenied:
        // bucket exists, but no access
        printf("Bucket '%s' exists, but is not accessible.\n", bucketName);
        break;
    default:
        printError();
        break;
    }

    S3_deinitialize();
}


// create bucket -------------------------------------------------------------

static void create_bucket(int argc, char **argv, int optind)
{
    if (optind == argc) {
        fprintf(stderr, "ERROR: Missing parameter: bucket\n");
        usageExit(stderr);
    }

    const char *bucketName = argv[optind++];

    const char *locationConstraint = 0;
    S3CannedAcl cannedAcl = S3CannedAclPrivate;
    while (optind < argc) {
        char *param = argv[optind++];
        if (!strncmp(param, LOCATION_CONSTRAINT_PREFIX, 
                     LOCATION_CONSTRAINT_PREFIX_LEN)) {
            locationConstraint = &(param[LOCATION_CONSTRAINT_PREFIX_LEN]);
        }
        else if (!strncmp(param, CANNED_ACL_PREFIX, CANNED_ACL_PREFIX_LEN)) {
            char *val = &(param[CANNED_ACL_PREFIX_LEN]);
            if (!strcmp(val, "private")) {
                cannedAcl = S3CannedAclPrivate;
            }
            else if (!strcmp(val, "public-read")) {
                cannedAcl = S3CannedAclPublicRead;
            }
            else if (!strcmp(val, "public-read-write")) {
                cannedAcl = S3CannedAclPublicReadWrite;
            }
            else if (!strcmp(val, "authenticated-read")) {
                cannedAcl = S3CannedAclAuthenticatedRead;
            }
            else {
                fprintf(stderr, "ERROR: Unknown canned ACL: %s\n", val);
                usageExit(stderr);
            }
        }
        else {
            fprintf(stderr, "ERROR: Unknown param: %s\n", param);
            usageExit(stderr);
        }
    }

    S3_init();

    S3ResponseHandler responseHandler =
    {
        &responseHeadersCallback, &responseCompleteCallback
    };

    S3_create_bucket(protocolG, accessKeyIdG, secretAccessKeyG, bucketName,
                     cannedAcl, locationConstraint, 0, &responseHandler, 0);

    if (statusG != S3StatusOK) {
        printError();
    }
    
    S3_deinitialize();
}


// delete bucket -------------------------------------------------------------

static void delete_bucket(int argc, char **argv, int optind)
{
    if (optind == argc) {
        fprintf(stderr, "ERROR: Missing parameter: bucket\n");
        usageExit(stderr);
    }

    const char *bucketName = argv[optind++];

    S3_init();

    S3ResponseHandler responseHandler =
    {
        &responseHeadersCallback, &responseCompleteCallback
    };

    S3_delete_bucket(protocolG, uriStyleG, accessKeyIdG, secretAccessKeyG,
                     bucketName, 0, &responseHandler, 0);

    if (statusG != S3StatusOK) {
        printError();
    }

    S3_deinitialize();
}


// list bucket ---------------------------------------------------------------

typedef struct list_bucket_callback_data
{
    int isTruncated;
    char nextMarker[1024];
} list_bucket_callback_data;


static S3Status listBucketCallback(int isTruncated, const char *nextMarker,
                                   int contentsCount, 
                                   const S3ListBucketContent *contents,
                                   int commonPrefixesCount,
                                   const char **commonPrefixes,
                                   void *callbackData)
{
    list_bucket_callback_data *data = 
        (list_bucket_callback_data *) callbackData;

    data->isTruncated = isTruncated;
    // This is tricky.  S3 doesn't return the NextMarker if there is no
    // delimiter.  Why, I don't know, since it's still useful for paging
    // through results.  We want NextMarker to be the last content in the
    // list, so set it to that if necessary.
    if (!nextMarker && contentsCount) {
        nextMarker = contents[contentsCount - 1].key;
    }
    if (nextMarker) {
        snprintf(data->nextMarker, sizeof(data->nextMarker), "%s", nextMarker);
    }
    else {
        data->nextMarker[0] = 0;
    }

    int i;
    for (i = 0; i < contentsCount; i++) {
        const S3ListBucketContent *content = &(contents[i]);
        printf("\nKey: %s\n", content->key);
        char timebuf[256];
        // localtime is not thread-safe but we don't care here.  xxx note -
        // localtime doesn't seem to actually do anything, 0 locatime of 0
        // returns EST Unix epoch, it should return the NZST equivalent ...
        strftime(timebuf, sizeof(timebuf), "%Y/%m/%d %H:%M:%S %Z",
                 localtime(&(content->lastModified)));
        printf("Last Modified: %s\n", timebuf);
        printf("ETag: %s\n", content->eTag);
        printf("Size: %llu\n", content->size);
        if (content->ownerId) {
            printf("Owner ID: %s\n", content->ownerId);
        }
        if (content->ownerDisplayName) {
            printf("Owner Display Name: %s\n", content->ownerDisplayName);
        }
    }

    for (i = 0; i < commonPrefixesCount; i++) {
        printf("\nCommon Prefix: %s\n", commonPrefixes[i]);
    }

    return S3StatusOK;
}


static void list_bucket(int argc, char **argv, int optind)
{
    if (optind == argc) {
        fprintf(stderr, "ERROR: Missing parameter: bucket\n");
        usageExit(stderr);
    }

    const char *bucketName = argv[optind++];

    const char *prefix = 0, *marker = 0, *delimiter = 0;
    int maxkeys = 0;
    while (optind < argc) {
        char *param = argv[optind++];
        if (!strncmp(param, PREFIX_PREFIX, PREFIX_PREFIX_LEN)) {
            prefix = &(param[PREFIX_PREFIX_LEN]);
        }
        else if (!strncmp(param, MARKER_PREFIX, MARKER_PREFIX_LEN)) {
            marker = &(param[MARKER_PREFIX_LEN]);
        }
        else if (!strncmp(param, DELIMITER_PREFIX, DELIMITER_PREFIX_LEN)) {
            delimiter = &(param[DELIMITER_PREFIX_LEN]);
        }
        else if (!strncmp(param, MAXKEYS_PREFIX, MAXKEYS_PREFIX_LEN)) {
            maxkeys = convertInt(&(param[MAXKEYS_PREFIX_LEN]), "maxkeys");
        }
        else {
            fprintf(stderr, "ERROR: Unknown param: %s\n", param);
            usageExit(stderr);
        }
    }
    
    S3_init();
    
    S3BucketContext bucketContext =
    {
        bucketName,
        protocolG,
        uriStyleG,
        accessKeyIdG,
        secretAccessKeyG
    };

    S3ListBucketHandler listBucketHandler =
    {
        { &responseHeadersCallback, &responseCompleteCallback },
        &listBucketCallback
    };

    list_bucket_callback_data data;

    do {
        data.isTruncated = 0;
        S3_list_bucket(&bucketContext, prefix, marker, delimiter, maxkeys,
                       0, &listBucketHandler, &data);
        if (statusG != S3StatusOK) {
            printError();
            break;
        }
        marker = data.nextMarker;
    } while (data.isTruncated);

    S3_deinitialize();
}


// put object -----------------------------------------------------------------

typedef struct put_object_callback_data
{
    FILE *infile;
    growbuffer *gb;
    uint64_t contentLength;
    char readBuffer[64 * 1024];
} put_object_callback_data;


static int putObjectCallback(int bufferSize, char *buffer, void *callbackData)
{
    put_object_callback_data *data = (put_object_callback_data *) callbackData;
    
    int ret = 0;

    if (data->contentLength) {
        int toRead = ((data->contentLength > bufferSize) ?
                      bufferSize : data->contentLength);
        if (data->infile) {
            ret = fread(data->readBuffer, 1, toRead, data->infile);
        }
        else if (data->gb) {
            growbuffer_read(&(data->gb), data->contentLength, &ret,
                            data->readBuffer);
        }
    }

    data->contentLength -= ret;

    return ret;
}


static void put_object(int argc, char **argv, int optind)
{
    if (optind == argc) {
        fprintf(stderr, "ERROR: Missing parameter: bucket/key\n");
        usageExit(stderr);
    }

    // Split bucket/key
    char *slash = argv[optind];
    while (*slash && (*slash != '/')) {
        slash++;
    }
    if (!*slash || !*(slash + 1)) {
        fprintf(stderr, "ERROR: Invalid bucket/key name: %s\n", argv[optind]);
        usageExit(stderr);
    }
    *slash++ = 0;

    const char *bucketName = argv[optind++];
    const char *key = slash;

    const char *filename = 0;
    uint64_t contentLength = 0;
    const char *cacheControl = 0, *contentType = 0, *md5 = 0;
    const char *contentDispositionFilename = 0, *contentEncoding = 0;
    time_t expires = -1;
    S3CannedAcl cannedAcl = S3CannedAclPrivate;
    int metaHeadersCount = 0;
    S3NameValue metaHeaders[S3_MAX_META_HEADER_COUNT];

    while (optind < argc) {
        char *param = argv[optind++];
        if (!strncmp(param, FILENAME_PREFIX, FILENAME_PREFIX_LEN)) {
            filename = &(param[FILENAME_PREFIX_LEN]);
        }
        else if (!strncmp(param, CONTENT_LENGTH_PREFIX, 
                          CONTENT_LENGTH_PREFIX_LEN)) {
            contentLength = convertInt(&(param[CONTENT_LENGTH_PREFIX_LEN]),
                                       "contentLength");
            if (contentLength > (5LL * 1024 * 1024 * 1024)) {
                fprintf(stderr, "ERROR: contentLength must be no greater "
                        "than 5 GB\n");
                usageExit(stderr);
            }
        }
        else if (!strncmp(param, CACHE_CONTROL_PREFIX, 
                          CACHE_CONTROL_PREFIX_LEN)) {
            cacheControl = &(param[CACHE_CONTROL_PREFIX_LEN]);
        }
        else if (!strncmp(param, CONTENT_TYPE_PREFIX, 
                          CONTENT_TYPE_PREFIX_LEN)) {
            contentType = &(param[CONTENT_TYPE_PREFIX_LEN]);
        }
        else if (!strncmp(param, MD5_PREFIX, MD5_PREFIX_LEN)) {
            md5 = &(param[MD5_PREFIX_LEN]);
        }
        else if (!strncmp(param, CONTENT_DISPOSITION_FILENAME_PREFIX, 
                          CONTENT_DISPOSITION_FILENAME_PREFIX_LEN)) {
            contentDispositionFilename = 
                &(param[CONTENT_DISPOSITION_FILENAME_PREFIX_LEN]);
        }
        else if (!strncmp(param, CONTENT_ENCODING_PREFIX, 
                          CONTENT_ENCODING_PREFIX_LEN)) {
            contentEncoding = &(param[CONTENT_ENCODING_PREFIX_LEN]);
        }
        else if (!strncmp(param, VALID_DURATION_PREFIX, 
                          VALID_DURATION_PREFIX_LEN)) {
            expires = (time(NULL) + 
                       convertInt(&(param[VALID_DURATION_PREFIX_LEN]), 
                                  "validDuration"));
        }
        else if (!strncmp(param, X_AMZ_META_PREFIX, X_AMZ_META_PREFIX_LEN)) {
            if (metaHeadersCount == S3_MAX_META_HEADER_COUNT) {
                fprintf(stderr, "ERROR: Too many x-amz-meta- headers, "
                        "limit %d: %s\n", S3_MAX_META_HEADER_COUNT, param);
                usageExit(stderr);
            }
            char *name = &(param[X_AMZ_META_PREFIX_LEN]);
            char *value = name;
            while (*value && (*value != '=')) {
                value++;
            }
            if (!*value || !*(value + 1)) {
                fprintf(stderr, "ERROR: Invalid parameter: %s\n", param);
                usageExit(stderr);
            }
            *value++ = 0;
            metaHeaders[metaHeadersCount].name = name;
            metaHeaders[metaHeadersCount++].value = value;
        }
        else if (!strncmp(param, CANNED_ACL_PREFIX, CANNED_ACL_PREFIX_LEN)) {
            char *val = &(param[CANNED_ACL_PREFIX_LEN]);
            if (!strcmp(val, "private")) {
                cannedAcl = S3CannedAclPrivate;
            }
            else if (!strcmp(val, "public-read")) {
                cannedAcl = S3CannedAclPublicRead;
            }
            else if (!strcmp(val, "public-read-write")) {
                cannedAcl = S3CannedAclPublicReadWrite;
            }
            else if (!strcmp(val, "authenticated-read")) {
                cannedAcl = S3CannedAclAuthenticatedRead;
            }
            else {
                fprintf(stderr, "ERROR: Unknown canned ACL: %s\n", val);
                usageExit(stderr);
            }
        }
        else {
            fprintf(stderr, "ERROR: Unknown param: %s\n", param);
            usageExit(stderr);
        }
    }

    S3_init();
    
    S3BucketContext bucketContext =
    {
        bucketName,
        protocolG,
        uriStyleG,
        accessKeyIdG,
        secretAccessKeyG
    };

    S3RequestHeaders requestHeaders =
    {
        contentType,
        md5,
        cacheControl,
        contentDispositionFilename,
        contentEncoding,
        expires,
        cannedAcl,
        0,
        0,
        metaHeadersCount,
        metaHeaders
    };

    S3PutObjectHandler listBucketHandler =
    {
        { &responseHeadersCallback, &responseCompleteCallback },
        &putObjectCallback
    };

    put_object_callback_data data;

    data.infile = 0;
    data.gb = 0;

    if (filename) {
        if (!contentLength) {
            struct stat statbuf;
            // Stat the file to get its length
            if (stat(filename, &statbuf) == -1) {
                fprintf(stderr, "ERROR: Failed to stat file %s: ", filename);
                perror(0);
                exit(-1);
            }
            contentLength = statbuf.st_size;
        }
        // Open the file
        if (!(data.infile = fopen(filename, "r"))) {
            fprintf(stderr, "ERROR: Failed to open input file %s: ", filename);
            perror(0);
            exit(-1);
        }
    }
    else {
        // Read from stdin.  If contentLength is not provided, we have
        // to read it all in to get contentLength.
        if (!contentLength) {
            // Read all if stdin to get the data
            char buffer[64 * 1024];
            while (1) {
                int amtRead = fread(buffer, 1, sizeof(buffer), stdin);
                if (amtRead == 0) {
                    break;
                }
                if (!growbuffer_append(&(data.gb), buffer, amtRead)) {
                    fprintf(stderr, "ERROR: Out of memory while reading "
                            "stdin\n");
                    exit(-1);
                }
                contentLength += amtRead;
                if (amtRead < sizeof(buffer)) {
                    break;
                }
            }
        }
        else {
            data.infile = stdin;
        }
    }

    data.contentLength = contentLength;

    S3_put_object(&bucketContext, key, contentLength, &requestHeaders, 0,
                  &listBucketHandler, &data);

    if (data.infile) {
        fclose(data.infile);
    }
    else if (data.gb) {
        growbuffer_destroy(data.gb);
    }

    if (statusG != S3StatusOK) {
        printError();
    }
    else if (data.contentLength) {
        fprintf(stderr, "ERROR: Failed to read remaining %llu bytes from "
                "input\n", data.contentLength);
    }

    S3_deinitialize();
}


// main -----------------------------------------------------------------------

int main(int argc, char **argv)
{
    // Parse args
    while (1) {
        int index = 0;
        int c = getopt_long(argc, argv, "pus", longOptionsG, &index);

        if (c == -1) {
            // End of options
            break;
        }

        switch (c) {
        case 'p':
            uriStyleG = S3UriStylePath;
            break;
        case 'u':
            protocolG = S3ProtocolHTTP;
            break;
        case 's':
            showResponseHeadersG = 1;
            break;
        default:
            fprintf(stderr, "ERROR: Unknown options: -%c\n", c);
            // Usage exit
            usageExit(stderr);
        }
    }

    accessKeyIdG = getenv("S3_ACCESS_KEY_ID");
    if (!accessKeyIdG) {
        fprintf(stderr, "Missing environment variable: S3_ACCESS_KEY_ID\n");
        return -1;
    }
    secretAccessKeyG = getenv("S3_SECRET_ACCESS_KEY");
    if (!secretAccessKeyG) {
        fprintf(stderr, "Missing environment variable: S3_SECRET_ACCESS_KEY\n");
        return -1;
    }

    // The first non-option argument gives the operation to perform
    if (optind == argc) {
        fprintf(stderr, "\nERROR: Missing argument: command\n\n");
        usageExit(stderr);
    }

    const char *command = argv[optind++];
    
    if (!strcmp(command, "help")) {
        usageExit(stdout);
    }
    else if (!strcmp(command, "list")) {
        if (optind == argc) {
            list_service();
        }
        else {
            list_bucket(argc, argv, optind);
        }
    }
    else if (!strcmp(command, "test")) {
        test_bucket(argc, argv, optind);
    }
    else if (!strcmp(command, "create")) {
        create_bucket(argc, argv, optind);
    }
    else if (!strcmp(command, "delete")) {
        delete_bucket(argc, argv, optind);
    }
    else if (!strcmp(command, "put")) {
        put_object(argc, argv, optind);
    }
    else if (!strcmp(command, "copy")) {
    }
    else if (!strcmp(command, "get")) {
    }
    else if (!strcmp(command, "head")) {
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return -1;
    }

    return 0;
}
