#include "http.h"

int fd;
char *token, *buf, *tempBuf, *line;
size_t toklen;
int start, state, end, processed, tempProc;
int tokenType;
//int writable;

#ifdef PRINT_HTTP_DEBUG 
struct timeval now1, now2, now3;
#endif

htab *ccTab;
htab *hTab;

char *hNames1 = "Cache-Control\0";
char *hNames2 = "Accept\0Accept-Charset\0Accept-Encoding\0Accept-Language\0Accept-Ranges\0Age\0Allow\0Authorization\0";
char *hNames3 = "Connection\0Content-Encoding\0Content-Language\0Content-Length\0Content-length\0Content-Location\0Content-MD5\0Content-Range\0Content-Type\0";
char *hNames4 = "Date\0ETag\0Expect\0Expires\0From\0Host\0If-Match\0If-Modified-Since\0If-None-Match\0If-Range\0If-Unmodified-Since\0Last-Modified\0";
char *hNames5 = "Location\0Max-Forwards\0Pragma\0Proxy-Authenticate\0Proxy-Authorization\0Range\0Referer\0";
char *hNames6 = "Retry-After\0Server\0TE\0Trailer\0Transfer-Encoding\0Upgrade\0User-Agent\0Vary\0Via\0Warning\0WWW-Authenticate\0";
char *hNames7 = "Response-Time\0Request-Time\0Proxy-Connection\0Keep-Alive\0";
char *ccNames = "no-cache\0no-store\0max-age\0max-stale\0min-fresh\0no-transform\0only-if-cached\0public\0private\0must-revalidate\0proxy-revalidate\0s-maxage\0";
int numbers[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53};
char *ctrlNames = "=\"\\<>/(){};:@,[]? \t";

char *values[ 53 ];

static char *snf = "Server not found";
static char *breq = "Bad Request";
static char *ok = "OK";
static char *nf = "NOT FOUND";
static char *putOK = "HTTP/0.9 300 OK\r\n";

int (*requestHeaderFunctions[ 60 ])( httpRequest* );
int (*responseHeaderFunctions[ 60 ])( httpResponse* );

int httpInit()
{
  int i;

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Creating a table for Cache-Control Directives\n" );
#endif
  
  ccTab = hcreate( 4 );
  
  if( ccTab == NULL )
    {
      fprintf( stderr, "Error creating a table for Cache-Control\n" );
      return -1;
    }

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Creating a table for headers\n" );
#endif

  hTab = hcreate( 6 );
  
  if( hTab == NULL )
    {
      fprintf( stderr, "Error creating a table for headers\n" );
      return -1;
    }

  for( i = 0; i < 53; i++ )
    values[ i ] = (char*)(&(numbers[ i ]));
      
  addToTab( ccTab, ccNames, values, 12 );
  addToTab( hTab, hNames1, values + 0, 1 );
  addToTab( hTab, hNames2, values + 1, 8 );
  addToTab( hTab, hNames3, values + 9, 9 );
  addToTab( hTab, hNames4, values + 18, 12 );
  addToTab( hTab, hNames5, values + 30, 7 );
  addToTab( hTab, hNames6, values + 37, 11 );
  addToTab( hTab, hNames7, values + 48, 4 );

  for( i = 0; i < 53; i++ )
    {
      responseHeaderFunctions[ i ] = &parseUnknownResponseHeader;
      requestHeaderFunctions[ i ] = &parseUnknownRequestHeader;
    }

  requestHeaderFunctions[ CACHE_CONTROL ] = &parseRequestCacheControl;
  requestHeaderFunctions[ AGE ] = &parseRequestAge;
  requestHeaderFunctions[ ACCEPT ] = &parseRequestAccept;
  requestHeaderFunctions[ ACCEPT_CHARSET ] = &parseRequestAcceptCharset;
  requestHeaderFunctions[ ACCEPT_ENCODING ] = &parseRequestAcceptEncoding;
  requestHeaderFunctions[ ACCEPT_LANGUAGE ] = &parseRequestAcceptLanguage;
  requestHeaderFunctions[ ACCEPT_RANGES ] = &parseRequestAcceptRanges;
  requestHeaderFunctions[ CONNECTION ] = &parseRequestConnection;
  requestHeaderFunctions[ CONTENT_ENCODING ] = &parseRequestContentEncoding;
  requestHeaderFunctions[ CONTENT_LANGUAGE ] = &parseRequestContentLanguage;
  requestHeaderFunctions[ CONTENT_LENGTH ] = &parseRequestContentLength;
  requestHeaderFunctions[ CONTENT_LOCATION ] = &parseRequestContentLocation;
  requestHeaderFunctions[ CONTENT_MD5 ] = &parseRequestContentMD5;
  requestHeaderFunctions[ CONTENT_RANGE ] = &parseRequestContentRange;
  requestHeaderFunctions[ CONTENT_TYPE ] = &parseRequestContentType;
  requestHeaderFunctions[ TRANSFER_ENCODING ] = &parseRequestTE;
  requestHeaderFunctions[ IF_MODIFIED_SINCE ] = &parseRequestIfModifiedSince;
  requestHeaderFunctions[ IF_UNMODIFIED_SINCE ] = &parseRequestIfUnmodifiedSince;
  requestHeaderFunctions[ PROXY_CONNECTION ] = &parseRequestProxyConnection;
  requestHeaderFunctions[ KEEP_ALIVE ] = &parseRequestKeepAlive;

  responseHeaderFunctions[ CACHE_CONTROL ] = &parseResponseCacheControl;
  responseHeaderFunctions[ AGE ] = &parseResponseAge;
  responseHeaderFunctions[ DATE ] = &parseResponseDate;
  responseHeaderFunctions[ EXPIRES ] = &parseResponseExpires;
  responseHeaderFunctions[ LAST_MODIFIED ] = &parseResponseLM;
  responseHeaderFunctions[ CONNECTION ] = &parseResponseConnection;
  responseHeaderFunctions[ CONTENT_ENCODING ] = &parseResponseContentEncoding;
  responseHeaderFunctions[ CONTENT_LANGUAGE ] = &parseResponseContentLanguage;
  responseHeaderFunctions[ CONTENT_LENGTH ] = &parseResponseContentLength;
  responseHeaderFunctions[ CONTENT_LOCATION ] = &parseResponseContentLocation;
  responseHeaderFunctions[ CONTENT_MD5 ] = &parseResponseContentMD5;
  responseHeaderFunctions[ CONTENT_RANGE ] = &parseResponseContentRange;
  responseHeaderFunctions[ CONTENT_TYPE ] = &parseResponseContentType;
  responseHeaderFunctions[ TRANSFER_ENCODING ] = &parseResponseTE;
  responseHeaderFunctions[ RESPONSE_TIME ] = &parseResponseResponseTime;
  responseHeaderFunctions[ REQUEST_TIME ] = &parseResponseRequestTime;
  responseHeaderFunctions[ PROXY_CONNECTION ] = &parseResponseProxyConnection;

  return 0;
}

int parseRequestLine( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS
  status = getNextToken();
#else
  getTill( SPACE );
#endif
  
  if( status == -1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Cannot get a token\n" );
#endif
      
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }

  #ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Parsing method\n" );
#endif
		  
  if( strncmp( token, "GET", 3 ) == 0 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "its GET\n" );
#endif
      
      presentRequestPtr->method = GET;
    }
  else
    {
      if( strncmp( token, "HEAD", 4 ) == 0 )
	{
	  presentRequestPtr->method = HEAD;
	}
      else
	{
	  if( strncmp( token, "POST", 4 ) == 0 )
	    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "its POST\n" );
#endif
	      presentRequestPtr->method = POST;
	    }
	  else
	    {
	      if( strncmp( token, "PUT", 3 ) == 0 )
		{
		  presentRequestPtr->method = PUT;
		}
	      else
		{
		  if( strncmp( token, "WAPROX-GET", 10 ) == 0 )
		    {
		      presentRequestPtr->method = WAPROX_GET;
		    }
		  else
		    {
		      if( strncmp( token, "WAPROX-PUT", 10 ) == 0 )
			{
			  presentRequestPtr->method = WAPROX_PUT;
			} 
		      else
			{
#ifdef PRINT_HTTP_DEBUG
			  fprintf( stderr, "Cannot recognize method\n" );
#endif
			  
			  presentRequestPtr->status = BAD_REQUEST;
			  return 0;
			}
		    }
		}
	    }
	}
    }

#ifdef USE_TOKENS
  status = eat( SP, 1 );
  
  if( status == -1 )
    {
      fprintf( stderr, "Bad Request\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return 0;
    }
#endif

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Parsing URL\n" );
#endif

#ifdef USE_TOKENS	
  status = getNextToken();
  
  if( status == -1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Cannot get a token\n" );
#endif
      
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }

  if( tokenType != TOKEN )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "No URI found\n" );
#endif
      
      presentRequestPtr->status = BAD_REQUEST;
      return 0;
    }
#else
  getTill( SPACE );
#endif

  presentRequestPtr->URL = token;
#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "URL of length %d\n", toklen );
#endif
  
#ifdef USE_TOKENS
  status = eat( SP, 1 );
  
  if( status == -1 )
    {
      presentRequestPtr->status = BAD_REQUEST;
      
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Expecting one white space after URI, found different\n" );
#endif
      
      return 0;
    }
  
  status = getNextToken();
  
  if( status == -1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Cannot get a token\n" );
#endif
      
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
#else
  getTill( CRETURN );
#endif

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Parsing version\n" );
#endif
  
  if( strncmp( token, "HTTP/0.9", 8 ) == 0 )
    {
      presentRequestPtr->majVersion = 0;
      presentRequestPtr->minVersion = 9;
    }
  else
    {
      if( strncmp( token, "HTTP/1.0", 8 ) == 0 )
	{
	  presentRequestPtr->majVersion = 1;
	  presentRequestPtr->minVersion = 0;
	}
      else
	{
	  if( strncmp( token, "HTTP/1.1", 8 ) == 0 )
	    {
	      presentRequestPtr->majVersion = 1;
	      presentRequestPtr->minVersion = 1;
	    }
	  else
	    {
#ifdef PRINT_HTTP_DEBUG
	      fprintf( stderr, "Bad version %s\n", token );
#endif
	      
	      presentRequestPtr->status = BAD_REQUEST;
	      return 0;
	    }
	}
    }
  
  presentRequestPtr->status = GOOD_REQUEST_LINE;

#ifdef USE_TOKENS
  status = getNextToken();
  
  if( status == -1 )
    {
      presentRequestPtr->status = BAD_REQUEST;
      fprintf( stderr, "Cannot get a token\n" );
      return -1;
    }

  if( tokenType != CRLF )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Expecting a CRLF after version, found different\n" );
#endif
      
      return -1;
    }
#endif

  return 0;
}

int parseHTTPHeader( httpRequest* presentRequestPtr, httpResponse* presentResponsePtr, int what )
{
  int status, hVal;

#ifdef USE_TOKENS
  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get token\n" );

      if( what == REQUEST )
	presentRequestPtr->status = BAD_REQUEST;
      else
	presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }

  if( tokenType != TOKEN )
    {
      fprintf( stderr, "Bad request - field value not a token\n" );

      if( what == REQUEST )
	presentRequestPtr->status = BAD_REQUEST;
      else
	presentResponsePtr->status = BAD_RESPONSE;
      return 0;
    }
#else
  getTill( COLON );
#endif

#ifdef PRINT_HTTP_DEBUG
  char buffer[ 100 ];
  sprintf( buffer, "%s%d%s %s20s\n", "\%.", toklen, "s", "\%." );
  fprintf( stderr, buffer, token, buf );
#endif

  if( buf[ processed ] == COLON && buf[ processed + 1 ] == SPACE && 
      buf[ processed + 2 ] == CRETURN && buf[ processed + 3 ] == LFEED ) {
    if( what == REQUEST )
      {
	addToTail( presentRequestPtr->headers, 0, buf );
      }
    else
      {
	if( what == RESPONSE )
	  {
	    addToTail( presentResponsePtr->headers, 0, buf );
	  }
      }
    return 0;
  }    
  //  strncpy( buffer, token, toklen );
  // buffer[ toklen ] = '\0';
 
  if( hfind( hTab, token, toklen ) == TRUE )
    {
      hVal = *(int*)hstuff( hTab );

      if( hVal > 51 || hVal < 0 )
	{
	  fprintf( stderr, "Unknown header value\n" );
	  return -1;
	}

      if( what == REQUEST && requestHeaderFunctions[ hVal ] != NULL )
	{
	  status = (*(requestHeaderFunctions[ hVal ]))( presentRequestPtr );
	  
	  if( status == -1 )
	    {
	      fprintf( stderr, "Bad header %d\n", hVal );
	      presentRequestPtr->status = BAD_REQUEST;
	      return -1;
	    }
	  
	  if( presentRequestPtr->status == BAD_REQUEST )
	    {
	      fprintf( stderr, "Bad header %d\n", hVal );
	      return 0;
	    }
	}
      else
	{
	  if( what == RESPONSE && responseHeaderFunctions[ hVal ] != NULL )
	    {
	      status = (*(responseHeaderFunctions[ hVal ]))( presentResponsePtr );
	      
	      if( status == -1 )
		{
		  fprintf( stderr, "Bad header %d\n", hVal );
		  presentResponsePtr->status = BAD_RESPONSE;
		  return -1;
		}
	      
	      if( presentResponsePtr->status == BAD_RESPONSE )
		{
		  fprintf( stderr, "Bad header %d\n", hVal );
		  return 0;
		}
	    }
	}
    }
  else
    {
#ifdef PRINT_HTTP_DEBUG
      // fprintf( stderr, "Non standard header\n" );
#endif

      if( what == REQUEST )
	{
	  addToTail( presentRequestPtr->headers, 0, buf );
	}
      else
	{
	  if( what == RESPONSE )
	    {
	      addToTail( presentResponsePtr->headers, 0, buf );
	    }
	}
    }
  
  return 0;
}

int parseRequest( int cli, httpRequest *presentRequestPtr )
{
  if( presentRequestPtr == NULL )
    {
      fprintf( stderr, "Argument is NULL\n" );
      return -1;
    }
  
  int status, stream, total;
  
  fd = cli;
  processed = 0;
  stream = 1;

#ifdef PRINT_HTTP_DEBUG
  gettimeofday( &now1, NULL );
#endif

  if( presentRequestPtr->used >= SMALL_BUFF_SIZE )
    {
      presentRequestPtr->status = BUFF_FULL;
      return 0;
    }

  presentRequestPtr->prevUsed = presentRequestPtr->used;
  int toReadMax = SMALL_BUFF_SIZE - presentRequestPtr->used;
  bufBlock *presentBufBlock = malloc( sizeof( bufBlock ) );
  presentBufCount++;
  int got;
  if( presentRequestPtr->rewind > 0 )
    {
      buf = presentRequestPtr->buf + presentRequestPtr->used - presentRequestPtr->rewind;
      presentRequestPtr->prevUsed -= presentRequestPtr->rewind;
      end = read( fd, buf + presentRequestPtr->rewind, toReadMax ) + presentRequestPtr->rewind;
      got = end - presentRequestPtr->rewind;
    }
  else
    {
      buf = presentRequestPtr->buf + presentRequestPtr->used;
      end = read( fd, buf, toReadMax );
      got = end;
    }

  if( got == -1 && errno == EAGAIN )
    {
      //      fprintf( stderr, "Hit EAGAIN\n" );
      free( presentBufBlock );
      presentRequestPtr->status = COME_BACK;
      return 0;
    }

  if( got < 0 )
    {
      fprintf( stderr, "Error reading from client: %d - %s\n", errno, strerror( errno ) );
      presentRequestPtr->status = BAD_REQUEST;
      free( presentBufBlock );
      return -1;
    }

  presentRequestPtr->used += got;

  buf[ end ] = EOF;
  presentBufBlock->end = end;
  presentBufBlock->buf = buf;
  addToTail( presentRequestPtr->blocks, end - presentRequestPtr->rewind, (char*)presentBufBlock );
  presentBufBlock->rstart = 0;
  presentBufBlock->wstart = 0;

  if( got == 0 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Connection closed by client\n" );
#endif
      
      presentRequestPtr->status = CLOSED;
      return 0;
    }

  presentRequestPtr->len = end;

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "%.1000s\n", buf );
#endif

  /*#ifdef PRINT_HTTP_DEBUG
  if( buf[ end - 1 ] == '\0' )
    {
      fprintf( stderr, "There is a NULL\n" );
    }

  if( buf[ end - 2 ] == '\r' )
    {
      fprintf( stderr, "There is a CRETURN" );
      if( buf[ end - 1 ] == '\n' )
	{
	  fprintf( stderr, " followed by LFEED\n" );
	}
    }
  else
    {
      if( buf[ end - 2 ] == '\n' )
	{
	  fprintf( stderr, "There is a LFEED\n" );
	}
    }
    #endif*/

  presentRequestPtr->rewind = 0;
  start = presentRequestPtr->start;
  state = presentRequestPtr->state;
  
  for(;;)
    {
      presentRequestPtr->start = start;
      presentRequestPtr->state = state;

#ifdef USE_TOKENS
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Bad request\n" );
	  presentRequestPtr->status = BAD_REQUEST;
	  return -1;
	}
      
      if( tokenType == BUFEND )
	{
#ifdef PRINT_HTTP_DEBUG
	  fprintf( stderr, "Have to rewind %d in %d\n", toklen, end );
#endif

	  presentRequestPtr->rewind = toklen;
	  presentRequestPtr->status = COME_BACK;
	  presentRequestPtr->len = end;
	  return 0;
	}
#endif

      switch( presentRequestPtr->stage )
	{
	case REQUEST_LINE:
	  {
#ifdef USE_TOKENS
	    tempBuf = buf;
	    tempProc = processed;
	    buf = token;
	    start = RAW;
	    state = NONE;
	    processed = 0;
#endif

#ifdef PRINT_HTTP_DEBUG
	    fprintf( stderr, "Parsing request line\n" );
#endif

	    status = parseRequestLine( presentRequestPtr );

#ifdef USE_TOKENS
	    buf = tempBuf;
	    processed = tempProc;
	    state = NONE;
	    start = LINE;
#endif

#ifdef PRINT_HTTP_DEBUG
	    fprintf( stderr, "%d\n", presentRequestPtr->status );
#endif

	    if( presentRequestPtr->status == BAD_REQUEST || status == -1 )
	      {
		fprintf( stderr, "Bad request line\n" );
		presentRequestPtr->status = BAD_REQUEST;
		return -1;
	      }

	    presentRequestPtr->stage = HTTP_HEADER;
	    
	    break;
	  }
	case HTTP_HEADER:
	  {
#ifdef PRINT_HTTP_DEBUG
	    fprintf( stderr, "Parsing HTTP headers\n" );
#endif

	    if( tokenType == CRLF )
	      {
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "No more headers\n" );
#endif

		if( stream == 1 )
		  {
		    presentBufBlock->rstart = processed;
		    
		    addToTail( presentRequestPtr->toSend, 0, (char*)presentBufBlock );
		    stream = 0;
		  }

		presentRequestPtr->status = ENTITY_INCOMPLETE;
		presentRequestPtr->stage = ENTITY_FETCH;
		start = BODY;
		state = NONE;
		
		if( presentRequestPtr->method == GET || presentRequestPtr->method == HEAD )
		  {
#ifdef PRINT_HTTP_DEBUG
		    fprintf( stderr, "No Entity Needed\n" );
#endif

		    start = ENTITY;
		    state = NONE;
		    presentRequestPtr->entityLen = 0;
		    presentRequestPtr->status = ENTITY_COMPLETE;

		    presentRequestPtr->rewind = end - processed;
		    return 0;
		  }

		if( presentRequestPtr->con_len >= 0 )
		  {
#ifdef PRINT_HTTP_DEBUG
		    fprintf( stderr, "Content Length based\n" );
#endif
		    presentRequestPtr->etype = CLEN;
		    start = BODY;
		    state = NONE;
		  }
		else
		  {
		    if( presentRequestPtr->conn != NULL && strncmp( presentRequestPtr->conn, "close", 5 ) == 0 )
		      {
#ifdef PRINT_HTTP_DEBUG
			fprintf( stderr, "Connection based\n" );
#endif
			presentRequestPtr->etype = CONN;
			start = BODY;
			state = NONE;
		      }
		    else
		      {
			if( presentRequestPtr->te != NULL && strncmp( presentRequestPtr->te, "chunked", 7 ) == 0 )
			  {
#ifdef PRINT_HTTP_DEBUG
			    fprintf( stderr, "Chunked\n" );
#endif
			    presentRequestPtr->etype = CHUN;
			    start = BODY;
			    state = NONE;
			    presentRequestPtr->entityLen = -1;
			    presentRequestPtr->rcvd = 0;
			  }
		      }
		  }
		
		if( presentRequestPtr->method == WAPROX_PUT )
		  {
		    presentRequestPtr->status = ENTITY_COMPLETE;
		    presentRequestPtr->len = presentRequestPtr->prevUsed + processed;
		    return 0;
		  }

		presentRequestPtr->entityLen = -1;
		presentRequestPtr->rcvd = 0;
		break;
	      }

#ifdef USE_TOKENS
	    tempBuf = buf;
	    tempProc = processed;
	    buf = token;
	    start = INIT;
	    state = NONE;
	    processed = 0;
#endif

	    status = parseHTTPHeader( presentRequestPtr, NULL, REQUEST );

#ifdef USE_TOKENS	    
	    buf = tempBuf;
	    processed = tempProc;
	    state = NONE;
	    start = LINE;
#endif

	    if( presentRequestPtr->status == BAD_REQUEST || status == -1 )
	      {
		fprintf( stderr, "Bad request\n" );
		presentRequestPtr->status = BAD_REQUEST;
		return 0;
	      }

	    break;
	  }
	case ENTITY_FETCH:
	  {
	    if( stream == 1 )
	      {
		presentBufBlock->start = 0;
		addToTail( presentRequestPtr->toSend, 0, (char*)presentBufBlock );
		stream = 0;
	      }

#ifdef PRINT_HTTP_DEBUG
	    fprintf( stderr, "Parsing entity\n" );
#endif

	    switch( presentRequestPtr->etype )
	      {
	      case CONN:
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "Connection based\n" );
#endif

		if( tokenType != BODY )
		  {
		    presentRequestPtr->status = BAD_REQUEST;
		    return 0;
		  }

		total = end - processed;
		presentRequestPtr->rcvd += total;
		presentRequestPtr->status = ENTITY_INCOMPLETE;
		return 0;
	      case CLEN:
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "Content Length based\n" );
#endif
		if( tokenType != BODY )
		  {
		    presentRequestPtr->status = BAD_REQUEST;
		    return 0;
		  }

		total = end - processed;

		if( total >= presentRequestPtr->con_len - presentRequestPtr->rcvd )
		  {
		    presentRequestPtr->rcvd = presentRequestPtr->con_len;
		    presentRequestPtr->status = ENTITY_COMPLETE;
		    return 0;
		  }
		else
		  {
		    presentRequestPtr->rcvd += total;
		    presentRequestPtr->state = NONE;
		    presentRequestPtr->start = BODY;
		    return 0;
		  }

		break;
	      case CHUN:
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "Chunked\n" );
#endif
		if( presentRequestPtr->entityLen == -1 )
		  {
		    start = LINEEND;
		    state = NONE;
		    
#ifdef PRINT_HTTP_DEBUG
		    fprintf( stderr, "%.3s\n", token );
#endif

		    sscanf( token, "%x", &presentRequestPtr->entityLen );

		    start = BODY;
		    state = NONE;
		    
		    presentRequestPtr->rcvd = 0;

#ifdef PRINT_HTTP_DEBUG
		    fprintf( stderr, "New chunk %d\n", presentRequestPtr->entityLen );
#endif

		    if( presentRequestPtr->entityLen == 0 )
		      {
			presentRequestPtr->status = ENTITY_COMPLETE;

#ifdef USE_TOKENS
			do 
			  {
			    status = getNextToken();
			    
			    if( status == -1 )
			      {
				fprintf( stderr, "Cannot get token\n" );
				return -1;
			      }

			  }
			while( tokenType != CRLF );

			presentRequestPtr->rewind = end - processed; 
#endif
			  
			return 0;
		      }
		  }
		
		total = end - processed;

#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "%d available\n", total );
#endif
		
		if( total < presentRequestPtr->entityLen - presentRequestPtr->rcvd )
		  {
		    presentRequestPtr->rcvd += total;
		    return 0;
		  }
		
		if( total >= presentRequestPtr->entityLen - presentRequestPtr->rcvd )
		  {
		    start = LINEEND;
		    state = NONE;
		    processed += 2 + presentRequestPtr->entityLen - presentRequestPtr->rcvd;
		    presentRequestPtr->entityLen = -1;
		  }
		break;
	      default:
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "No Entity\n" );
#endif
		
		presentRequestPtr->status = ENTITY_COMPLETE;
		
		return 0;
	      }
	    
	    return 0;
	    break;
	  }
	case ENTITY_COMPLETE:
	  {
	    presentRequestPtr->status = BAD_REQUEST;
	    return -1;
	  }
	default:
	  {
	    presentRequestPtr->status = BAD_REQUEST;
	    return 0;
	  }
	}
    }
}

#ifdef USE_TOKENS
int getNextToken()
{
#ifdef PRINT_HTTP_DEBUG
  //fprintf( stderr, "....\n" );
#endif

  char present;

  toklen = 0;
  tokenType = LWS;
  token = buf + processed;
  
  //  fprintf( stderr, "processed : %d\n", processed );
  
  for(;;)
    {
      //fprintf( stderr, "." );
      if( processed > end )
	{
	  return BUFEND;
	}

      if( buf[ processed ] > 31 && buf[ processed ] < 127 && isCtrl( buf[ processed ] ) == FALSE )
	{
	  present = CHAR;
	}
      else
	{
	  present = buf[ processed ];
	}

#ifdef PRINT_HTTP_DEBUG
      //fprintf( stderr, "%d %d %c\n", start, state, present );
#endif

      switch( start )
	{
	case INIT:
	  switch( state )
	    {
	    case NONE:
	      switch( present )
		{
		case CHAR:
		  toklen++;
		  processed++;
		  state = TOKEN;
		  break;
		case SPACE: case TAB:
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  state = SP;
		  break;
		case CRETURN:
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  state = CR;
		  break;
		case LFEED:
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  state = LF;
		  break;
		case COLON:
		  toklen++;
		  processed++;
		  tokenType = COLON;
		  return 0;
		case SEMICOLON:
		  toklen++;
		  processed++;
		  tokenType = SEMICOLON;
		  return 0;
		case QUOTE:
		  toklen++;
		  processed++;
		  tokenType = QUOTE;
		  return 0;
		case EQUALS:
		  toklen++;
		  processed++;
		  tokenType = EQUALS;
		  return 0;
		case COMMA:
		  toklen++;
		  processed++;
		  tokenType = COMMA;
		  return 0;
		default:
		  tokenType = ERROR;
		  return 0;
		}
	      break;
	    case TOKEN:
	      switch( present )
		{
		case CHAR:
		  toklen++;
		  processed++;
		  break;
		default:
		  state = NONE;
		  tokenType = TOKEN;
		  return 0;
		}
	      break;
	    case SP:
	      switch( present )
		{
		case SPACE: case TAB:
		  state = SP;
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  break;
		case CRETURN:
		  state = CR;
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  break;
		case LFEED:
		  state = LF;
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  break;
		default:
		  state = NONE;
		  tokenType = SP;
		  return 0;
		}
	      break;
	    case CR:
	      switch( present )
		{
		case LFEED:
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  state = LF;
		  break;
		default:
		  tokenType = ERROR;
		  return -1;
		}
	      break;
	    case LF:
	      switch( present )
		{
		case SPACE: case TAB:
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  state = LWS;
		  break;
		default:
		  tokenType = CRLF;
		  state = NONE;
		  return 0;
		}
	      break;
	    }
	  break;
	case HEADER:
	  switch( state )
	    {
	    case NONE:
	      switch( present )
		{
		case EOF:
		  tokenType = BUFEND;
		  return 0;
		case CRETURN:
		  toklen++;
		  processed++;
		  state = CR;
		  break;
		case LFEED:
		  toklen++;
		  processed++;
		  state = LF;
		  break;
		default:
		  state = TOKEN;
		  toklen++;
		  processed++;
		  break;
		}
	      break;
	    case TOKEN:
	      switch( present )
		{
		case EOF:
		  tokenType = BUFEND;
		  return 0;
		case CRETURN:
		  toklen++;
		  processed++;
		  state = CR;
		  break;
		case LFEED:
		  toklen++;
		  processed++;
		  state = LF;
		  break;
		default:
		  state = TOKEN;
		  toklen++;
		  processed++;
		  break;
		}   
	      break;  
	    case CR:
	      switch( present )
		{
		case LFEED:
		  toklen++;
		  processed++;
		  state = LF;
		  break;
		case EOF:
		  tokenType = BUFEND;
		  return 0;
		default:
		  tokenType = ERROR;
		  return 0;
		}
	      break;
	    case LF:
	      switch( present )
		{
		case SPACE: case TAB:
		  toklen++;
		  processed++;
		  state = TOKEN;
		  break;
		case EOF:
		  tokenType = BUFEND;
		  return 0;
		default:
		  tokenType = TOKEN;
		  start = LINE;
		  state = NONE;
		  return 0;
		}
	      break;
	    }
	  break;
	case RAW:
	  switch( state )
	    {
	    case NONE:
	      switch( present )
		{
		case SPACE:
		  buf[ processed ] = '\0';
		  toklen++;
		  processed++;
		  tokenType = SP;
		  state = NONE;
		  return 0;
		case TAB: 
		  tokenType = ERROR;
		  return -1;
		default:
		  state = TOKEN;
		  toklen++;
		  processed++;
		  break;
		}
	      break;
	    case TOKEN:
	      switch( present )
		{
		case SPACE:
		  state = NONE;
		  tokenType = TOKEN;
		  return 0;
		case CRETURN:
		  state = NONE;
		  start = INIT;
		  tokenType = TOKEN;
		  return 0;
		case TAB:
		  tokenType = ERROR;
		  return -1;
		default:
		  toklen++;
		  processed++;
		  break;
		}
	      break;
	    }
	  break;
	case LINE:
	  switch( state )
	    {
	    case NONE:
	      switch( present )
		{
		case CRETURN:
		  state = CR;
		  toklen++;
		  processed++;
		  break;
		default:
		  state = NONE;
		  start = HEADER;
		  break;
		}
	      break;
	    case CR:
	      switch( present )
		{
		case LFEED:
		  toklen++;
		  processed++;
		  tokenType = CRLF;
		  start = HEADER;
		  state = NONE;
		  return 0;
		default:
		  state = NONE;
		  start = HEADER;
		  break;
		}
	      break;
	    }
	  break;
	case ENTITY:
	  switch( state )
	    { 
	    case NONE:
	      switch( present )
		{
		case CRETURN:
		  state = CR;
		  toklen++;
		  processed++;
		  break;
		case EOF:
		  state = NONE;
		  tokenType = BODY;
		  return 0;
		default:
		  toklen++;
		  processed++;
		  break;
		}
	      break;
	    case CR:
	      switch( present )
		{
		case LFEED:
		  state = LF;
		  toklen++;
		  processed++;
		  break;
		case EOF:
		  state = NONE;
		  tokenType = BUFEND;
		  return 0;
		default:
		  toklen++;
		  processed++;
		  break;
		}
	      break;
	    case LFEED:
	      switch( present )
		{
		case SPACE:
		  state = NONE;
		  toklen++;
		  processed++;
		  break;
		case EOF:
		  state = NONE;
		  tokenType = BUFEND;
		  return 0;
		default:
		  state = NONE;
		  tokenType = BODY;
		  return 0;
		}
	      break;
	    }
	  break;
	case LINEEND:
	  switch( state )
	    {
	    case NONE:
	      switch( present )
		{
		case CRETURN:
		  state = CR;
		  toklen++;
		  processed++;
		  break;
		case EOF:
		  state = NONE;
		  tokenType = BUFEND;
		  return 0;
		default:
		  toklen++;
		  processed++;
		  break;
		}
	      break;
	    case CR:
	      switch( present )
		{
		case LFEED:
		  state = LF;
		  toklen++;
		  processed++;
		  tokenType = TOKEN;
		  return 0;
		case EOF:
		  state = NONE;
		  tokenType = BUFEND;
		  return 0;
		default:
		  toklen++;
		  processed++;
		  break;
		}
	      break;
	    }
	  break;
	case BODY:
	  tokenType = BODY;
	  return 0;
	default:
	  tokenType = ERROR;
	  return 0;
	}
    }
  
  return ERROR;
}

int eat( int type, int len )
{
  int status, tempStart, tempState;
  
  tempStart = start;
  tempState = state;
  status = getNextToken();

  if( status == -1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Bad lex\n" );
#endif

      return -1;
    }

  if( len == -1 && tokenType != type )
    {
      processed -= toklen;
      state = tempState;
      start = tempStart;
      return -1;
    }

  if( tokenType != type )
    {
      processed -= toklen;
      state = tempState;
      start = tempStart;
      return -1;
    }

  if( len != -1 && len != toklen )
    {
      return -1;
    }

  return 0;
}
#endif

int parseURL( URLInfo *presentInfoPtr, char *URL )
{
  //fprintf( stderr, "%s\n", URL );

  if( strncmp( "http://", URL, 7 ) != 0 )
    {
      fprintf( stderr, "Bad HTTP-URL, does not start with http://\n" );
      presentInfoPtr->status = NO_HTTP;
      return -1;
    }

  int i = 7, status;
  int len = strlen( URL );

  while( URL[ i ] != '/' && URL[ i ] != ':' && i <= len )
    {
      i++;
    }

  if( i == len )
    {
      presentInfoPtr->port = 80;
      presentInfoPtr->absPath = NULL;
    }

  presentInfoPtr->server = malloc( i - 6 );

  strncpy( presentInfoPtr->server, URL + 7, i - 7 );
  presentInfoPtr->server[ i - 7 ] = '\0';

  char **endPtr = &URL;

  //fprintf( stderr, "Char : %c\n", URL[ i ] );

  if( URL[ i ] == ':' )
    {
      status = strtol( URL + i + 1, endPtr, 10 );

      if( status <= 0 )
	{
	  fprintf( stderr, "Error, incorrect port specification\n" );
	  return -1;
	}

      presentInfoPtr->port = status;
      
      if( *endPtr[ 0 ] != '/' )
	{
	  presentInfoPtr->absPath = NULL;
	}
      else
	{
	  presentInfoPtr->absPath = *endPtr;

#ifdef PRINT_HTTP_DEBUG
	  fprintf( stderr, "%s\n", presentInfoPtr->absPath );  
#endif

	}
    }
  else
    {
      presentInfoPtr->port = 80;
      
      if( URL[ i ] != '/' )
	{
	  presentInfoPtr->absPath = NULL;
	}
      else
	{
	  presentInfoPtr->absPath = URL + i;
	  //fprintf( stderr, "%s\n", presentInfoPtr->absPath );  
	}
    }

  presentInfoPtr->status = GOOD_INFO;
  
  return 0;
}

int determineLength( httpRequest *ptr )
{
  return 0;
}

int parseResponse( int cli, char *extBuf, int len, httpResponse *presentResponsePtr )
{
  if( presentResponsePtr == NULL )
    {
      fprintf( stderr, "Argument is NULL\n" );
      return -1;
    }
  
  int status, total, stream = 1;
  bufBlock *presentBufBlock;

  fd = cli;
  processed = 0;
  char done = 0;

#ifdef PRINT_HTTP_DEBUG  
  gettimeofday( &now1, NULL );
#endif
  
  if( presentResponsePtr->block == 1 && presentResponsePtr->used >= HTTP_ACT_BLOCK_SIZE - ADDL_HEAD_SIZE )
    {
      presentResponsePtr->status = BUFF_FULL;
      return 0;
    }
  else
    {
      if( presentResponsePtr->block > 1 && presentResponsePtr->used >= HTTP_ACT_BLOCK_SIZE )
	{
	  presentResponsePtr->status = BUFF_FULL;
	  return 0;
	}
    }

  presentBufBlock = (bufBlock*)malloc( sizeof( bufBlock ) );
  presentBufCount++;

  if( len == -1 )
    {
      if( presentResponsePtr->rewind > 0 ) 
	{
	  buf = presentResponsePtr->buf + presentResponsePtr->used - presentResponsePtr->rewind;
	
	  if( presentResponsePtr->block  == 1 )
	    {
	      end = read( fd, buf + presentResponsePtr->rewind, BUFF_SIZE - ADDL_HEAD_SIZE - presentResponsePtr->used ) + presentResponsePtr->rewind;
	    }
	  else
	    {
	      end = read( fd, buf + presentResponsePtr->rewind, BUFF_SIZE - presentResponsePtr->used ) + presentResponsePtr->rewind;
	    }

	  presentBufBlock->start = presentResponsePtr->rewind;
	  presentResponsePtr->used += end;

#ifdef PRINT_HTTP_DEBUG
	  fprintf( stderr, "Read %d\n", end - presentResponsePtr->rewind );
#endif

	  if( end - presentResponsePtr->rewind == -1 && errno == EAGAIN )
	    {
	      //	      fprintf( stderr, "Hit EAGAIN\n" );
	      presentResponsePtr->rewind = 0;
	      presentResponsePtr->status = COME_BACK;
	      presentResponsePtr->used += 1;
	      presentBufBlock->end = 0;
	      presentBufBlock->rstart = 0;
	      presentBufBlock->wstart = 0;
	      presentBufBlock->orig = presentResponsePtr->buf;
	      presentBufBlock->buf = buf;
	      addToTail( presentResponsePtr->blocks, end - presentResponsePtr->rewind, (char*)presentBufBlock );
	      return 0;
	    }

	  if( end - presentResponsePtr->rewind <= 0 )
	    done = 1;
	}
      else
	{
	  buf = presentResponsePtr->buf + presentResponsePtr->used;

	  if( presentResponsePtr->block == 1 )
	    {
	      end = read( fd, buf, BUFF_SIZE - ADDL_HEAD_SIZE - presentResponsePtr->used );
	    }
	  else
	    {
	      end = read( fd, buf, BUFF_SIZE - presentResponsePtr->used );
	    }

	  presentResponsePtr->used += end;

#ifdef PRINT_HTTP_DEBUG
	  fprintf( stderr, "Read %d\n", end );
#endif
	  presentBufBlock->start = 0;
	  
	  if( end == -1 )
	    {
	      if( errno == EAGAIN )
		{
		  //		  fprintf( stderr, "Hit EAGAIN\n" );
		  presentResponsePtr->status = COME_BACK;
		  presentResponsePtr->used += 1;
		  presentBufBlock->end = 0;
		  presentBufBlock->rstart = 0;
		  presentBufBlock->wstart = 0;
		  presentBufBlock->orig = presentResponsePtr->buf;
		  presentBufBlock->buf = buf;
		  
		  addToTail( presentResponsePtr->blocks, end - presentResponsePtr->rewind, (char*)presentBufBlock );
		  return 0;
		} 
	      else 
		{
		  fprintf( stderr, "errno=%d errstr=%s\n", errno, strerror( errno ) );
		  presentResponsePtr->status = BAD_RESPONSE;
		  done = 1; 
		}
	    } 
	  else 
	    {
	      if( end == 0 )
		{
		  done = 1;
		}
	    }
	}
    }
  else
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Given chunk %d Used %d\n", len, presentResponsePtr->used );
#endif

      if( presentResponsePtr->rewind > 0 )
	{
	  buf = presentResponsePtr->buf + presentResponsePtr->used - presentResponsePtr->rewind;
	  memcpy( buf + presentResponsePtr->rewind, extBuf, len );
	  presentResponsePtr->used += len;

	  end = len + presentResponsePtr->rewind;
	  presentBufBlock->start = presentResponsePtr->rewind;
	}
      else
	{
	  buf = presentResponsePtr->buf + presentResponsePtr->used;
	  presentResponsePtr->used += len;

#ifdef PRINT_HTTP_DEBUG
	  gettimeofday( &now3, NULL );
#endif

	  memcpy( buf, extBuf, len );
	  end = len;
	  presentBufBlock->start = 0;
	}
    }
  
  presentResponsePtr->unWritten += end;
  presentBufBlock->end = end;
  presentBufBlock->orig = presentResponsePtr->buf;
  presentBufBlock->buf = buf;

  addToTail( presentResponsePtr->blocks, end - presentResponsePtr->rewind, (char*)presentBufBlock );
  if( end != -1 )
    buf[ end ] = EOF;

  presentResponsePtr->rewind = 0;

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "%.1000s\n", buf );
#endif

  if( done == 1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Connection closed\n" );
#endif
      presentResponsePtr->status = CLOSED;

      if( presentResponsePtr->etype == CONN )
	{
	  if( presentResponsePtr->stage == RESPONSE_LINE )
	    {
	      presentResponsePtr->status = BAD_RESPONSE;
	      return -1;
	    }
	  else
	    {
	      presentResponsePtr->status = ENTITY_COMPLETE;
	      return 0;
	    }
	}
      else
	{
	  presentResponsePtr->status = BAD_RESPONSE;
	  return -1;
	}

      return 0;
    }

  start = presentResponsePtr->start;
  state = presentResponsePtr->state;
  
  //  fprintf( stderr, "hi\n" );

  for(;;)
    {
      presentResponsePtr->start = start;
      presentResponsePtr->state = state;

#ifdef USE_TOKENS
      status = getNextToken();
      
      if( status == -1 )
	{
	  //  fprintf( stderr, "hi1\n" );
	  fprintf( stderr, "Bad responses\n" );
	  presentResponsePtr->status = BAD_RESPONSE;
	  return -1;
	}
      
      if( tokenType == BUFEND )
	{
#ifdef PRINT_HTTP_DEBUG
	  fprintf( stderr, "Have to rewind %d in %d\n", toklen, end );
#endif

	  presentResponsePtr->rewind = toklen;
	  presentResponsePtr->status = COME_BACK;
	  presentResponsePtr->len = end;
	  return 0;
	}
#endif

      //      fprintf( stderr, "hi2 %d\n", presentResponsePtr->stage );
      switch( presentResponsePtr->stage )
	{
	case RESPONSE_LINE:
	  {
#ifdef USE_TOKENS
	    tempBuf = buf;
	    tempProc = processed;
	    buf = token;
	    start = RAW;
	    state = NONE;
	    processed = 0;
#endif

#ifdef PRINT_HTTP_DEBUG
	    fprintf( stderr, "Parsing response line\n" );
#endif

	    status = parseResponseLine( presentResponsePtr );

#ifdef USE_TOKENS
	    buf = tempBuf;
	    processed = tempProc;
	    state = NONE;
	    start = LINE;
#endif

	    if( presentResponsePtr->status == BAD_RESPONSE || status == -1 )
	      {
		fprintf( stderr, "Bad response\n" );
		presentResponsePtr->status = BAD_RESPONSE;
		return 0;
	      }

	    presentResponsePtr->stage = HTTP_HEADER;
	    
#ifdef PRINT_HTTP_DEBUG
	    fprintf( stderr, "Parsing HTTP headers\n" );
#endif

	    break;
	  }
	case HTTP_HEADER:
	  {
	    if( tokenType == CRLF )
	      {
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "No more headers\n" );
#endif

		if( stream == 1 )
		  {
		    presentBufBlock->rstart = processed;
		    presentBufBlock->wstart = processed;

#if PRINT_HTTP_DEBUG > 0 && PRINT_LOOP_DEBUG > 0
		    if( presentResponsePtr->cacheable == TRUE && presentResponsePtr->type == DOWN )
		      writable += presentBufBlock->end - presentBufBlock->rstart;
#endif

		    addToTail( presentResponsePtr->toSend, 0, (char*)presentBufBlock );
		    addToTail( presentResponsePtr->toWrite, 0, (char*)presentBufBlock );
		    stream = 0;
		    presentResponsePtr->downloaded += presentBufBlock->end - presentBufBlock->rstart;
		  }

		presentResponsePtr->status = ENTITY_INCOMPLETE;
		presentResponsePtr->stage = ENTITY_FETCH;
		state = NONE;
		start = BODY;
		presentResponsePtr->etype = CONN;

		if( presentResponsePtr->con_len >= 0 )
		  {
		    presentResponsePtr->etype = CLEN;
		    state = NONE;
		    start = BODY;
		  }
		else
		  {
		    if( presentResponsePtr->conn != NULL && strncmp( presentResponsePtr->conn, "close", 5 ) == 0 )
		      {
			presentResponsePtr->etype = CONN;
			state = NONE;
			start = BODY;
		      }
		    else
		      {
			if( presentResponsePtr->te != NULL && strncmp( presentResponsePtr->te, "chunked", 7 ) == 0 )
			  {
			    presentResponsePtr->etype = CHUN;
			    state = NONE;
			    start = LINEEND;
			  }
		      }
		  }

		presentResponsePtr->entityLen = -1;
		presentResponsePtr->rcvd = 0;

		break;
	      }

	    tempBuf = buf;
	    tempProc = processed;
	    buf = token;
	    start = INIT;
	    state = NONE;
	    processed = 0;
	    
	    status = parseHTTPHeader( NULL, presentResponsePtr, RESPONSE );
	    
	    buf = tempBuf;
	    processed = tempProc;
	    state = NONE;
	    start = LINE;

	    if( presentResponsePtr->status == BAD_RESPONSE || status == -1 )
	      {
		fprintf( stderr, "Bad response\n" );
		presentResponsePtr->status = BAD_RESPONSE;
		return 0;
	      }

	    break;
	  }
	case ENTITY_FETCH:
	  {
	    if( stream == 1 )
	      {
#if PRINT_HTTP_DEBUG > 0 && PRINT_LOOP_DEBUG > 0
		if( presentResponsePtr->cacheable == TRUE && presentResponsePtr->type == DOWN )
		  writable += presentBufBlock->end;
#endif

		presentBufBlock->rstart = 0;
		presentBufBlock->wstart = 0;
		addToTail( presentResponsePtr->toSend, 0, (char*)presentBufBlock );
		addToTail( presentResponsePtr->toWrite, 0, (char*)presentBufBlock );
		stream = 0;
		presentResponsePtr->downloaded += presentBufBlock->end;
	      }

#ifdef PRINT_HTTP_DEBUG
	    //fprintf( stderr, "Parsing entity\n" );
#endif
	    switch( presentResponsePtr->etype )
	      {
	      case CONN:
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "Connection based entity\n" );
#endif

		if( tokenType != BODY )
		  {
		    presentResponsePtr->status = BAD_RESPONSE;
		    return 0;
		  }

		total = end - processed;
		presentResponsePtr->rcvd += total;
		presentResponsePtr->status = ENTITY_INCOMPLETE;
		return 0;
	      case CLEN:
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "Content length based entity\n" );
#endif

		if( tokenType != BODY )
		  {
		    presentResponsePtr->status = BAD_RESPONSE;
		    return 0;
		  }
		
		total = end - processed;

		if( total >= presentResponsePtr->con_len - presentResponsePtr->rcvd )
		  {
		    presentResponsePtr->rcvd = presentResponsePtr->con_len;
		    presentResponsePtr->status = ENTITY_COMPLETE;
		  }
		else
		  {
		    presentResponsePtr->rcvd += total;

#ifdef PRINT_HTTP_DEBUG
		    fprintf( stderr, "Received %d of %d\n", presentResponsePtr->rcvd, presentResponsePtr->con_len );
#endif

		    presentResponsePtr->state = NONE;
		    presentResponsePtr->start = BODY;
		  }

		return 0;
	      case CHUN:
#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "Chunked entity\n" );
#endif

		if( presentResponsePtr->entityLen == -1 )
		  {
		    start = LINEEND;
		    state = NONE;
		    
#ifdef PRINT_HTTP_DEBUG
		    fprintf( stderr, "%.3s\n", token );
#endif

		    sscanf( token, "%x", &presentResponsePtr->entityLen );

		    start = BODY;
		    state = NONE;
		    
		    presentResponsePtr->rcvd = 0;

#ifdef PRINT_HTTP_DEBUG
		    fprintf( stderr, "New chunk %d\n", presentResponsePtr->entityLen );
#endif

		    if( presentResponsePtr->entityLen == 0 )
		      {
			presentResponsePtr->status = ENTITY_COMPLETE;

#ifdef USE_TOKENS
			do 
			  {
			    start = LINE;
			    status = NONE;

			    status = getNextToken();

			    if( status == -1 )
			      {
				fprintf( stderr, "Cannot get next token\n" );
				return -1;
			      }
			  }
			while( tokenType != CRLF );
#endif

			presentResponsePtr->rewind = end - processed;

			return 0;
		      }
		  }
		
		total = end - processed;

#ifdef PRINT_HTTP_DEBUG
		fprintf( stderr, "%d available\n", total );
#endif
		
		if( total < presentResponsePtr->entityLen - presentResponsePtr->rcvd )
		  {
		    presentResponsePtr->rcvd += total;
		    presentResponsePtr->start = BODY;
		    presentResponsePtr->state = NONE;
		    return 0;
		  }
		
		if( total >= presentResponsePtr->entityLen - presentResponsePtr->rcvd )
		  {
		    start = LINEEND;
		    state = NONE;
		    processed += 2 + presentResponsePtr->entityLen - presentResponsePtr->rcvd;
		    presentResponsePtr->entityLen = -1;
		  }
		break;
	      default:
		if( tokenType == CRLF )
		  {
		    presentResponsePtr->status = ENTITY_COMPLETE;
		    return 0;
		  }
	      }
	    
	    break;
	  }
	case BODY:
	  return BODY;
	  break;
	default:
	  {
	    presentResponsePtr->status = BAD_RESPONSE;
	    return 0;
	  }
	}
    }
}
  
int parseResponseLine( httpResponse *presentResponsePtr )
{
  int status;

#ifdef USE_TOKENS
  status = getNextToken();
  
  if( status == -1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Cannot get a token\n" );
#endif
      
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  getTill( SPACE );
#endif

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Parsing version\n" );
#endif
		  
  if( strncmp( token, "HTTP/1.1", 8 ) == 0 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "its 1.1\n" );
#endif
      
      presentResponsePtr->majVersion = 1;
      presentResponsePtr->minVersion = 1;
    }
  else
    {
      if( strncmp( token, "HTTP/1.0", 8 ) == 0 )
	{
	  presentResponsePtr->majVersion = 1;
	  presentResponsePtr->minVersion = 0;
	}
      else
	{
	  if( strncmp( token, "HTTP/0.9", 8 ) == 0 )
	    {
	      presentResponsePtr->majVersion = 0;
	      presentResponsePtr->minVersion = 9;
	    }
	  else
	    {
#ifdef PRINT_HTTP_DEBUG
	      fprintf( stderr, "Cannot recognize version\n" );
#endif
	      
	      presentResponsePtr->status = BAD_RESPONSE;
	      return 0;
	    }
	}
    }

#ifdef USE_TOKENS
  status = eat( SP, 1 );
  
  if( status == -1 )
    {
      fprintf( stderr, "Bad Responses\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return 0;
    }
  
#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Parsing status code\n" );
#endif
	
  status = getNextToken();
  
  if( status == -1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Cannot get a token\n" );
#endif
      
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }
  
  if( tokenType != TOKEN )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "No code found\n" );
#endif
      
      presentResponsePtr->status = BAD_RESPONSE;
      return 0;
    }
#else
  getTill( SPACE );
#endif

  presentResponsePtr->code = atoi( token );

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "Status code %d\n", presentResponsePtr->code );
#endif
  
#ifdef USE_TOKENS
  status = eat( SP, 1 );
  
  if( status == -1 )
    {
      presentResponsePtr->status = BAD_RESPONSE;
      
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Expecting one white space after code, found different\n" );
#endif
      
      return 0;
    }
  
  state = NONE;
  start = HEADER;
  status = getNextToken();
  state = NONE;
  start = LINE;

  if( status == -1 )
    {
#ifdef PRINT_HTTP_DEBUG
      fprintf( stderr, "Cannot get a token\n" );
#endif
      
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }
  
#ifdef PRINT_HTTP_DEBUG
  /*  char buffer[ 100 ];
  fprintf( stderr, "%d", toklen );
  sprintf( buffer, "%s%d%s\n", "\%.", toklen, "s" );
  printf( buffer, token );*/
#endif
#else
  getTill( CRETURN );
#endif

  presentResponsePtr->message = token;
  presentResponsePtr->status = GOOD_RESPONSE_LINE;
  token[ toklen - 2 ] = '\0';

#ifdef PRINT_HTTP_DEBUG
  fprintf( stderr, "%s\n", token );
#endif

  return 0;
}

int checkCachability( httpResponse *presentResPtr )
{
  cache_control_response *cc = presentResPtr->cc;
  int c = presentResPtr->code;

  //return FALSE;

  if( presentResPtr->method == WAPROX_PUT )
    {
      presentResPtr->cacheable = TRUE;
      return 0;
    }

  presentResPtr->cacheable = FALSE;

  if( strlen( presentResPtr->URL ) > URL_SIZE - 1 )
    {
      return 0;
    }

  if( presentResPtr->con_len < 1000000 )
    {
      if( c == 200 || c == 203 || c == 206 || c == 300 || c == 301 )
	{
	  if( cc == NULL )
	    {
	      presentResPtr->cacheable = TRUE;
	      return 0;
	    }
	  
	  if( cc->private == PRESENT || cc->no_cache == PRESENT ||
	      cc->no_store == PRESENT )
	    {
	      presentResPtr->cacheable = FALSE;
	      return 0;
	    }

	  if( cc->public == PRESENT )
	    {
	      presentResPtr->cacheable = TRUE;
	      return 0;
	    }
	  
	  if( cc->max_age != -1 )
	    {
	      presentResPtr->cacheable = TRUE;
	      return 0;
	    }
	  
	  if( cc->s_maxage != -1 )
	    {
	      presentResPtr->cacheable = TRUE;
	      return 0;
	    }
	  
	  if( cc->must_revalidate == PRESENT || cc->proxy_revalidate == PRESENT )
	    {
	      presentResPtr->cacheable = TRUE;
	      return 0;
	    }
	}
    }

  return 0;
}

int checkExpiry( httpResponse *presentRes )
{
  asynckey key;
  cache_control_response *ccs = presentRes->cc;
  
  int age_value, date_value, response_time, request_time, apparent_age;
  int corrected_received_age, response_delay, corrected_initial_age, now;
  int resident_time, current_age, freshness_lifetime, expires_value;

  freshness_lifetime = -1;

  gettimeofday( &key, NULL );
  now = key.tv_sec;

  age_value = presentRes->age_value;
  date_value = presentRes->date_value;
  expires_value = presentRes->expires_value;

  response_time = presentRes->response_time;
  request_time = presentRes->request_time;
  
  apparent_age = max( 0, response_time - date_value );
  corrected_received_age = max( apparent_age, age_value );
  response_delay = response_time - request_time;
  corrected_initial_age = corrected_received_age + response_delay;
  resident_time = now - response_time;
  current_age = corrected_initial_age + resident_time;
  
  presentRes->age_value = current_age;

  //  fprintf( stderr, "Age: %d\n", current_age );

  if( ccs != NULL && ccs->max_age != -1 )
    {
      freshness_lifetime = ccs->max_age;
    }
  else
    {
      if( presentRes->expires_value != -1 )
	{
	  freshness_lifetime = presentRes->expires_value - date_value; 
	}
    }

  if( freshness_lifetime <= 0 && presentRes->last_modified != -1 )
    {
      freshness_lifetime = (now - presentRes->last_modified);
    }

  if( freshness_lifetime <= 0 && presentRes->last_modified == -1 )
    {
      return 0;
    }

  if( freshness_lifetime > current_age )
    return 0;

  return -1;
}

int checkIfAcceptsCached( httpRequest *req )
{
  if( req->cc == NULL )
    {
      return 0;
    }

  if( req->cc->no_cache == PRESENT )
    {
      return -1;
    }

  if( req->cc->max_stale == 0 )
    {
      return 0;
    }

  if( req->cc->only_if_cached == 0 )
    {
      return 0;
    }

  if( req->cc->no_store == PRESENT )
    {
      return -1;
    }
  
  return 0;
}

int checkCompatibility( httpRequest *req, httpResponse *presentRes )
{
  cache_control_response *ccs = presentRes->cc;
  
  asynckey key;
  int age_value, date_value, response_time, request_time, apparent_age;
  int corrected_received_age, response_delay, corrected_initial_age, now;
  int resident_time, current_age, freshness_lifetime, expires_value;

  freshness_lifetime = 0;

  gettimeofday( &key, NULL );
  now = key.tv_sec;

  age_value = presentRes->age_value;
  date_value = presentRes->date_value;
  expires_value = presentRes->expires_value;

  response_time = presentRes->response_time;
  request_time = presentRes->request_time;
  
  apparent_age = max( 0, response_time - date_value );
  corrected_received_age = max( apparent_age, age_value );
  response_delay = response_time - request_time;
  corrected_initial_age = corrected_received_age + response_delay;
  resident_time = now - response_time;
  current_age = corrected_initial_age + resident_time;

  if( req->cc != NULL && req->cc->max_age != -1 && current_age <= req->cc->max_age )
    {
      return 0;
    }

  if( ccs != NULL && ccs->max_age != -1 )
    {
      freshness_lifetime = ccs->max_age;
    }
  else
    {
      if( presentRes->expires_value != -1 )
	{
	  freshness_lifetime = presentRes->expires_value - date_value; 
	}
    }

  if( freshness_lifetime <= 0 )
    {
      freshness_lifetime = (now - presentRes->last_modified)/10;
    }

  if( req->cc != NULL && req->cc->min_fresh != -1 && freshness_lifetime >= current_age + req->cc->min_fresh )
    {
      return 0;
    }

  if( req->cc != NULL && freshness_lifetime <= current_age + req->cc->max_stale )
    {
      return 0;
    }
  else
    {
      if( freshness_lifetime <= current_age )
	{
	  return 0;
	}
    }

  return 0;
}

char* getSNF()
{
  return snf;
}

char* getBReq()
{
  return breq;
}

char* getOK()
{
  return ok;
}

char* getNF()
{
  return nf;
}

char* getPutOK()
{
  return putOK;
}

char isCtrl( char present )
{
  int j;

  for( j = 0; j < 19; j++ )
    {
      if( present == ctrlNames[ j ] )
	return TRUE;
    }

  return FALSE;
}

#ifndef USE_TOKENS
inline void getTillSP()
{
  token = buf [ processed ];
  
  while( buf[ processed++ ] != SPACE )
    toklen++;

  buf[ processed - 1 ] = '\0';
  tokenType = TOKEN;
}

inline void getTillCRLF()
{
  token = buf + processed;

  while( buf[ processed++ ] != CRETURN )
    toklen++;

  buf[ processed - 1 ] = '\0';
  buf[ processed ] = '\0';
  processed++;

  if( token[ 0 ] == CRETURN )
    tokenType = CRLF;
  else
    tokenType = TOKEN;
}

inline void skipTillSP()
{
  while( buf[ processed ] == SPACE || buf[ processed ] == COLON || buf[ processed ] == COMMA || buf[ processed ] == CRETURN || buf[ processed ] == LFEED )
    processed++;
  
  token = buf + processed;
  tokenType = TOKEN;
}

inline void getTillAlphaNum()
{
  token = buf[ processed ];
  
  while( isalnum( buf[ processed++ ] ) )
    toklen++;

  tokenType = TOKEN;
}
#endif

  
