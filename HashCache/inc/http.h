#ifndef _HTTP_H_
#define _HTTP_H_

#include "hcfs.h"
#include "dirty.h"

struct DNSRep;
//#define PRINT_HEADER_DEBUG 100
//#define PRINT_HTTP_DEBUG 100
//#define PRINT_DEBUG 100
#define USE_TOKENS 100

#define REQUEST_LINE 'r'
#define HTTP_HEADER 'h'
#define ENTITY_FETCH 'e'
#define RESPONSE_LINE 'r'

#define BUFF_SIZE HTTP_ACT_BLOCK_SIZE
#define ADDL_HEAD_SIZE 200
#define SMALL_BUFF_SIZE 4095

#define REQUEST 1
#define RESPONSE 2

#define GET 'g'
#define HEAD 'h'
#define POST 'p'
#define PUT 'u'
#define WAPROX_GET 'e'
#define WAPROX_PUT 'x'

#define BAD_REQUEST 1
#define COME_BACK 3
#define ENTITY_INCOMPLETE 4
#define REQUEST_DONE 5
#define GOOD_REQUEST_LINE 6
#define ENTITY_COMPLETE 2
#define BAD_RESPONSE 7
#define RESPONSE_DONE 9
#define CLOSED 10
#define GOOD_RESPONSE_LINE 11
#define BUFF_FULL 12
#define BUFFER_WAIT 13

#define INIT 1
#define HEADER 2
#define RAW 3
#define LINE 4
#define ENTITY 5
#define LINEEND 6

#define TOKEN 1
#define LF 2
#define CR 3
#define CRLF 4
#define SP 5
#define LWS 6
#define CTRL 7
#define BUFEND 8
#define NONE 9
#define ERROR 10
#define BODY 11

#define CHAR 'c'
#define LFEED '\n'
#define CRETURN '\r'
#define SPACE ' '
#define TAB '\t'
#define EQUALS '='
#define COLON ':'
#define SEMICOLON ';'
#define QUOTE '"'
#define COMMA ','
#define MINUX '-'

#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <error.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include "hashtab.h"
#include "list.h"

//extern int errno;
//extern int h_errno;
extern int writable;
extern int logFd;
extern int presentBufCount;

typedef struct timeval asynckey;
typedef list messageHeaders;

#define NO_HTTP 'n'
#define GOOD_INFO 'g'

#define SNF 16
#define BRQ 11

typedef struct URLInfo
{
  char *server;
  int port;
  char *absPath;
  char status;
} URLInfo;

#define QUEUED 'q'
#define PROCESSED 'p'

typedef struct entity
{
  char status;
  char *data;
} entity;

#define PRESENT 1
#define ABSENT 2

#define CACHE_CONTROL 1
#define ACCEPT 2
#define ACCEPT_CHARSET 3
#define ACCEPT_ENCODING 4
#define ACCEPT_LANGUAGE 5
#define ACCEPT_RANGES 6
#define AGE 7
#define ALLOW 8
#define AUTHORIZATION 9
#define CONNECTION 10
#define CONTENT_ENCODING 11
#define CONTENT_LANGUAGE 12
#define CONTENT_LENGTH 13
#define CONTENT_LOCATION 14
#define CONTENT_MD5 15
#define CONTENT_RANGE 16
#define CONTENT_TYPE 17
#define DATE 18
#define ETAG 19
#define EXPECT 21
#define EXPIRES 21
#define FROM 22
#define HOST 23
#define IF_MATCH 24
#define IF_MODIFIED_SINCE 25
#define IF_NONE_MATCH 26
#define IF_RANGE 27
#define IF_UNMODIFIED_SINCE 28
#define LAST_MODIFIED 29
#define LOCATION 30
#define MAX_FORWARDS 31
#define PRAGMA 32
#define PROXY_AUTHENTICATE 33
#define PROXY_AUTHORIZATION 34
#define RANGE 35
#define REFERER 36
#define RETRY_AFTER 37
#define SERVER 38
#define TE 39
#define TRAILER 40
#define TRANSFER_ENCODING 41
#define UPGRADE 42
#define USER_AGENT 43
#define VARY 44
#define VIA 45
#define WARNING 46
#define WWW_AUTHENTICATE 47
#define RESPONSE_TIME 48
#define REQUEST_TIME 49
#define PROXY_CONNECTION 50
#define KEEP_ALIVE 51

#define NO_CACHE 1
#define NO_STORE 2
#define MAX_AGE 3
#define MAX_STALE 4
#define MIN_FRESH 5
#define NO_TRANSFORM 6
#define ONLY_IF_CACHED 7
#define PUBLIC 8
#define PRIVATE 9
#define MUST_REVALIDATE 10
#define PROXY_REVALIDATE 11
#define S_MAXAGE 12

#define CLEN 1
#define CHUN 2
#define CONN 3

#define DOWN '1'
#define CACHE '2'
#define FOLLOW '3'
#define WEB '4'

#define SENDING 2
#define START 3
#define FIRST 4
#define MATCH 5
#define COMPATIBILITY 6
#define SECOND 7
#define REM 8
#define LATER 9

#define NEWCONN 'n'
#define OLDCONN 'o'

//#define NO_ACCEPT_PREFS

typedef struct bufBlock
{
  char *buf;
  int start;
  int rstart;
  int wstart;
  int end;
  char del;
  char *orig;
} bufBlock;

typedef struct cache_control_request
{
  char no_cache;
  char no_store;
  int max_age;
  int max_stale;
  int min_fresh;
  char no_transform;
  char only_if_cached;
} cache_control_request;

typedef struct httpRequest
{
  /*lexical parameters*/
  char buf[ SMALL_BUFF_SIZE + 1 ];
  char stage;
  int start;
  int state;
  int rewind;

  /*proxy parameters*/
#ifdef USE_WAPROX_HEADER
  char connType;
#endif

  int prevUsed;
  int used;
  int status;
  int pstage;
  list *blocks;
  list *toSend;
  asynckey key;
  int fd;
  unsigned int hash;
  int sent;
  int rcvd;
  int len;
  char lDel;
  struct in_addr clientIP;
  struct DNSRep* dnsInfo;

  /*request line parameters*/
  char method;
  char *URL;
  URLInfo info;
  int majVersion;
  int minVersion;
  
  /*http header parameters*/
  messageHeaders *headers;
  cache_control_request *cc;
  time_t age_value;
  time_t date_value;
  time_t request_time;
  char *conn;
  char *pconn;
  char* kl;
  int con_len;
  char *te;
  time_t if_mod_since_value;
  time_t if_unmod_since_value;

  /*entity body parameters*/
  list *entityBody; 
  int entityLen;
  int etype;
} httpRequest;

typedef struct cache_control_response
{
  char public;
  char private;
  char no_cache;
  char no_store;
  char no_transform;
  char must_revalidate;
  char proxy_revalidate;
  int max_age;
  int s_maxage;
} cache_control_response;

typedef struct httpResponse
{
  /*lexical paramaters*/
  char buf[ HTTP_BLOCK_SIZE + 1 ];
  char stage;
  int start;
  int state;
  int rewind;
  int len;

  /*proxy parameters*/
  char miss;
  dirtyObject *dob;
  int serverHash;
  int totalLen;
  char cacheType;
  char batchAdd;
  char method;
  int flowEntry;
  int status;
  int unWritten;
  list *blocks;
  list *toSend;
  list *toWrite;
  list *allBlocks;
  int deficit;
  int offset;
  asynckey key;
  int fd;
  int tfd;
  httpRequest *presReq; 
  unsigned int hash;
  int block;
  int downloaded;
  int written;
  int sent;
  int match;
  char *URL;
  char type;
  char pstage;
  char cacheCheck;
  char lDel;
  char map;
  char connWait;
  int used;
  int retries;
  int headSent;
  int reqHead;
  char reqHeadSent;
  bufBlock *prev;
  /*response line parameters*/
  int majVersion;
  int minVersion;
  int code;
  char *message;
  int remOffset;
  int version;
  char logType;
  struct in_addr clientIP;

  /*http header parameters*/
  int headerLen;
  char* contentType;
  messageHeaders *headers;
  cache_control_response *cc;
  time_t last_modified;
  time_t age_value;
  time_t date_value;
  time_t expires_value;
  time_t response_time;
  time_t request_time;
  time_t if_mod_since_value;
  time_t if_unmod_since_value;
  char cacheable;
  char *conn;
  char *pconn;
  int con_len;
  char *te;
  char *redir_location;
  char *warning;
  char sendHead;
  
  /*entity body parameters*/
  list *entityBody;
  int entityLen;
  int rcvd;
  char etype;

  struct timeval startTime;
} httpResponse;


int parseRequest( int fd, httpRequest* presentRequestPtr );
int parseRequestLine( httpRequest *presentRequestPtr );

#ifdef USE_TOKENS
int getNextToken();
int eat( int type, int len );
#endif

int parseURL( URLInfo *ptr, char *URL );

int addHeader( messageHeaders *headers, char *name, int nameLen, char *value );

int determineLength( httpRequest *presentRequestPtr );

int httpInit();
int initHTTPRequest( httpRequest* request );
int initHTTPResponse( httpResponse* response, httpRequest *request );
int reInitHTTPRequest( httpRequest* request );
char isCtrl( char present );
int parseResponse( int fd, char *buf, int len, httpResponse* presentResponsePtr );
int parseResponseLine( httpResponse *presentResponsePtr );

int parseHTTPHeader( httpRequest *presentRequestPtr, httpResponse *presentResponsePtr, int what );
int parseRequestCacheControl( httpRequest *presentRequestPtr );
int parseResponseCacheControl( httpResponse *presentResponsePtr );
int parseResponseAge( httpResponse *ptr );
int parseRequestAge( httpRequest *ptr );
int parseResponseDate( httpResponse *ptr );
int parseDate( time_t *t );
int parseResponseExpires( httpResponse *ptr );
int parseResponseLM( httpResponse *ptr );
int parseRequestAccept( httpRequest* presentRequestPtr );
int parseRequestAcceptCharset( httpRequest *presentRequestPtr );
int parseRequestAcceptEncoding( httpRequest *presentRequestPtr );
int parseRequestAcceptLanguage( httpRequest *presentRequestPtr );
int parseRequestAcceptRanges( httpRequest *presentRequestPtr );
int parseRequestConnection( httpRequest *presentRequestPtr );
int parseRequestContentEncoding( httpRequest *presentRequestPtr );
int parseRequestContentLanguage( httpRequest *presentRequestPtr );
int parseRequestContentLength( httpRequest *presentRequestPtr );
int parseRequestContentLocation( httpRequest *presentRequestPtr );
int parseRequestContentMD5( httpRequest *presentRequestPtr );
int parseRequestContentRange( httpRequest *presentRequestPtr );
int parseRequestContentType( httpRequest *presentRequestPtr );
int parseRequestTE( httpRequest *presentRequestPtr );
int parseUnknownRequestHeader( httpRequest *parseRequestPtr );
int parseRequestIfModifiedSince( httpRequest *parseRequestPtr );
int parseRequestIfUnmodifiedSince( httpRequest *parseRequestPtr );
int parseRequestProxyConnection( httpRequest *parseRequestPtr );
int parseRequestKeepAlive( httpRequest *parseRequestPtr );

int parseResponseConnection( httpResponse *presentResponsePtr );
int parseResponseContentEncoding( httpResponse *presentResponsePtr );
int parseResponseContentLanguage( httpResponse *presentResponsePtr );
int parseResponseContentLength( httpResponse *presentResponsePtr );
int parseResponseContentLocation( httpResponse *presentResponsePtr );
int parseResponseContentMD5( httpResponse *presentResponsePtr );
int parseResponseContentRange( httpResponse *presentResponsePtr );
int parseResponseContentType( httpResponse *presentResponsePtr );
int parseResponseTE( httpResponse *presentResponsePtr );
int parseResponseResponseTime( httpResponse *presentResponsePtr );
int parseResponseRequestTime( httpResponse *presentResponsePtr );
int parseUnknownResponseHeader( httpResponse *presentResponsePtr );
int parseResponseLocation( httpResponse *presentResponsePtr );
int parseResponseProxyConnection( httpResponse *presentResponsPtr );

int checkCachability( httpResponse *presentResPtr );
int checkExpiry( httpResponse *presentResPtr );
int checkIfAcceptsCached( httpRequest *req );
int checkCompatibility( httpRequest *req, httpResponse *res );

char* getSNF();
char* getBReq();
char* getOK();
char* getNF();
char* getPutOK();

/*
int parseAge( httpRequest *presentRequestPtr );
int parseAllow( httpRequest *presentRequestPtr );
int parseAuthorization( httpRequest *presentRequestPtr );
int parseDate( httpRequest *presentRequestPtr );
int parseETag( httpRequest *presentRequestPtr );
int parseExpect( httpRequest *presentRequestPtr );
int parseExpires( httpRequest *presentRequestPtr );
int parseFrom( httpRequest *presentRequestPtr );
int parseHost( httpRequest *presentRequestPtr );
int parseIfMatch( httpRequest *presentRequestPtr );
int parseIfModifiedSince( httpRequest *presentRequestPtr );
int parseIfNoneMatch( httpRequest *presentRequestPtr );
int parseUnmodifiedSince( httpRequest *presentRequestPtr );
int parseLastModified( httpRequest *presentRequestPtr );
int parseLocation( httpRequest *presentRequestPtr );
int parseMaxForwards( httpRequest *presentRequestPtr );
int parsePragma( httpRequest *presentRequestPtr );
int parseProxyAuthenticate( httpRequest *presentRequestPtr );
int parseProxyAuthorization( httpRequest *presentRequestPtr );
int parseRange( httpRequest *presentRequestPtr );
int parseReferer( httpRequest *presentRequestPtr );
int parseRetryAfter( httpRequest *presentRequestPtr );
int parseServer( httpRequest *presentRequestPtr );
int parseTE( httpRequest *presentRequestPtr );
int parseTrailer( httpRequest *presentRequestPtr );
int parseTransferEncoding( httpRequest *presentRequestPtr );
int parseUpgrade( httpRequest *presentRequestPtr );
int parseUserAgent( httpRequest *presentRequestPtr );
int parseVary( httpRequest *presentRequestPtr );
int parseVia( httpRequest *presentRequestPtr );
int parseWarning( httpRequest *presentRequestPtr );
int parseWWWAuthenticate( httpRequest *presentRequestPtr );
*/
#endif
