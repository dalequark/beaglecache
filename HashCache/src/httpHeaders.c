#include "httpHeaders.h"
extern int numCC;

int parseRequestCacheControl( httpRequest *presentRequestPtr )
{
  int status, directive;
#ifdef PRINT_HEADER_DEBUG
  fprintf( stderr, "Parsing cache control headers\n" );
#endif

  presentRequestPtr->cc = (cache_control_request*)malloc( sizeof( cache_control_request ) );
  cache_control_request *cc = presentRequestPtr->cc;
  numCC++;

  cc->no_cache = ABSENT;
  cc->no_store = ABSENT;
  cc->max_age = -1;
  cc->max_stale = -1;
  cc->min_fresh = -1;
  cc->no_transform = ABSENT;
  cc->only_if_cached = ABSENT;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );
  
  if( status == -1 )
    {
      presentRequestPtr->status = BAD_REQUEST;
      return 0;
    }
  
#endif

  for(;;)
    {
#ifdef USE_TOKENS
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot get token\n" );
	  return -1;
	}
      
      if( tokenType == LWS || tokenType == COMMA || tokenType == SP )
	{
	  continue;
	}
      
      if( tokenType == CRLF )
	{
	  presentRequestPtr->status = COME_BACK;
	  
	  state = NONE;
	  start = LINE;
	  return 0;
	}
#else
      getTill( NONALPHA );
#endif
      
      if( hfind( ccTab, token, toklen ) == TRUE )
	{
	  directive = *(int*)(hstuff( ccTab ) );
	  
	  switch( directive )
	    {
	    case NO_CACHE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "No Cache\n" );
#endif
	      cc->no_cache = 1;
	      break;
	    case NO_STORE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "No Store\n" );
#endif
	      cc->no_store = 1;
	      break;
	    case MAX_AGE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "Max Age\n" );
#endif
	      
#ifdef USE_TOKENS
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != EQUALS )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentRequestPtr->status = BAD_REQUEST;
		  return 0;
		}
	      
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != TOKEN )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentRequestPtr->status = BAD_REQUEST;
		  return 0;
		}
#else
	      skipTill( NUMERIC );
#endif

	      cc->max_age = atoi( token );
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, " %d\n", cc->max_age );
#endif
	      
	      break;
	    case MAX_STALE:
#ifdef USE_TOKENS 
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != EQUALS )
		{
		  processed -= toklen;
		  break;
		}
	      
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != TOKEN )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentRequestPtr->status = BAD_REQUEST;
		  return 0;
		}
#else
	      skipTill( NUMERIC );
#endif

	      cc->max_stale = atoi( token );
	      break;
	    case MIN_FRESH:
#ifdef USE_TOKENS
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != EQUALS )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentRequestPtr->status = BAD_REQUEST;
		  return 0;
		}
	      
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != TOKEN )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentRequestPtr->status = BAD_REQUEST;
		  return 0;
		}
#else
	      skipTill( NUMERIC );
#endif

	      cc->min_fresh = atoi( token );
	      break;
	    case NO_TRANSFORM:
	      cc->no_transform = 1;
	      break;
	    case ONLY_IF_CACHED:
	      cc->only_if_cached = 1;
	      break;
	    }
	}
    }
}

int parseResponseCacheControl( httpResponse *presentResponsePtr )
{
  int status, directive;
#ifdef PRINT_HEADER_DEBUG
  fprintf( stderr, "Parsing cache control headers\n" );
#endif
  
  presentResponsePtr->cc = (cache_control_response*)malloc( sizeof( cache_control_response ) );
  cache_control_response *cc = presentResponsePtr->cc;
  numCC++;

  cc->public = ABSENT;
  cc->private = ABSENT;
  cc->no_cache = ABSENT;
  cc->no_store = ABSENT;
  cc->no_transform = ABSENT;
  cc->must_revalidate = ABSENT;
  cc->proxy_revalidate = ABSENT;
  cc->max_age = -1;
  cc->s_maxage = -1;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );
  
  if( status == -1 )
    {
      presentResponsePtr->status = BAD_RESPONSE;
      return 0;
    }
#endif

  for(;;)
    {
#ifdef USE_TOKENS
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot get token\n" );
	  presentResponsePtr->status = BAD_RESPONSE;
	  return -1;
	}
      
      if( tokenType == LWS || tokenType == COMMA || tokenType == SP )
	{
	  continue;
	}
#else
      getTill( NONALPHA );
#endif

      if( tokenType == CRLF )
	{
	  presentResponsePtr->status = COME_BACK;
	  
	  state = NONE;
	  start = LINE;
	  return 0;
	}

      if( hfind( ccTab, token, toklen ) == TRUE )
	{
	  directive = *(int*)(hstuff( ccTab ) );
	  
	  switch( directive )
	    {
	    case PUBLIC:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "Public\n" );
#endif

	      cc->public = 1;
	      break;
	    case PRIVATE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "Private\n" );
#endif
	      cc->private = 1;
	      break;
	    case NO_CACHE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "No Cache\n" );
#endif
	      
	      cc->no_cache = 1;
	      break;
	    case NO_STORE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "No Store\n" );
#endif
	      cc->no_store = 1;
	      break;
	    case NO_TRANSFORM:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "No Transform\n" );
#endif

	      cc->no_transform = 1;
	      break;
	    case MUST_REVALIDATE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "Must Revalidate\n" );
#endif

	      cc->must_revalidate = 1;
	      break;
	    case PROXY_REVALIDATE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "Proxy Revalidate\n" );
#endif

	      cc->proxy_revalidate = 1;
	      break;
	    case MAX_AGE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "Max Age\n" );
#endif

#ifdef USE_TOKENS	      
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != EQUALS )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentResponsePtr->status = BAD_RESPONSE;
		  return 0;
		}
	      
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != TOKEN )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentResponsePtr->status = BAD_RESPONSE;
		  return 0;
		}
#else
	      skipTill( NUMERIC );
#endif

	      cc->max_age = atoi( token );
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, " %d\n", cc->max_age );
#endif
	      
	      break;
	    case S_MAXAGE:
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, "S Maxage" );
#endif
	      
#ifdef USE_TOKENS
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != EQUALS )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentResponsePtr->status = BAD_RESPONSE;
		  return 0;
		}
	      
	      status = getNextToken();
	      
	      if( status == -1 )
		{
#ifdef PRINT_HEADER_DEBUG
		  fprintf( stderr, "Cannot get token\n" );
#endif
		  return -1;
		}
	      
	      if( tokenType != TOKEN )
		{
		  fprintf( stderr, "Bad header\n" );
		  presentResponsePtr->status = BAD_RESPONSE;
		  return 0;
		}
#else
	      skipTill( NUMERIC );
#endif

	      cc->s_maxage = atoi( token );
#ifdef PRINT_HEADER_DEBUG
	      fprintf( stderr, " %d\n", cc->s_maxage );
#endif
	      
	      break;
	    }
	}
    }
}

int parseResponseAge( httpResponse *presentResPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( NUMERIC );
#endif

  presentResPtr->age_value = atoi( token );

  return 0;
}

int parseRequestAge( httpRequest *presentReqPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentReqPtr->status = BAD_REQUEST;
      return -1;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentReqPtr->status = BAD_REQUEST;
    }
#else
  skipTill( NUMERIC );
#endif

  presentReqPtr->age_value = atoi( token );

  return 0;
}

int parseResponseExpires( httpResponse *presentResPtr )
{
  int status;

#ifdef USE_TOKENS  
  status = eat( COLON, 1 );
    
  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }
  
  status = eat( SP, -1 );
    
  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
    }
#endif

  status = parseDate( (&presentResPtr->expires_value) );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse date\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }

  return 0;
}

int parseResponseDate( httpResponse *presentResPtr ) 
{
  int status;
  
#ifdef USE_TOKENS
  status = eat( COLON, 1 );
    
  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }
  
  status = eat( SP, -1 );
    
  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
    }
#endif

  status = parseDate( &presentResPtr->date_value );
  presentResPtr->response_time = presentResPtr->date_value;

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse date\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }

  return 0;
}

int parseDate( time_t *t )
{
  int status;
  struct tm presentTM;

#ifdef USE_TOKENS
  eat( SP, -1 );

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse month\n" );
      return -1;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannor parse COMMA\n" );
      return -1;
    }

  switch( tokenType )
    {
    case COMMA:
      eat( SP, 1 );
      break;
    default:
      break;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse the starting of dates\n" );
      return -1;
    }
#else
  getTill( SPACE );
#endif

  if( toklen == 2 )
    {
#ifdef PRINT_HEADER_DEBUG
      fprintf( stderr, "Date 1\n" );
#endif

      presentTM.tm_mday = atoi( token );

#ifdef USE_TOKENS  
      eat( SP, -1 );

      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse month\n" );
	  return -1;
	}
#else
      getTill( SPACE );
#endif

#ifdef USE_TOKENS
      if( tokenType == COMMA ) {
	eat( SP, -1 );
	status = getNextToken();
      }
#endif

      presentTM.tm_mon = months( token );

#ifdef USE_TOKENS  
      eat( SP, -1 );

      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse year\n" );
	  return -1;
	}
#else
      getTill( SPACE );
#endif

      presentTM.tm_year = atoi( token ) - 1900;

#ifdef USE_TOKENS      
      eat( SP, -1 );
      
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse hour\n" );
	  return -1;
	}
#else
      getTill( SPACE );
#endif

      presentTM.tm_hour = atoi( token );

#ifdef USE_TOKENS      
      status = eat( COLON, 1 );
      
      if( status == -1 )
	{
	  fprintf( stderr, "No colon after hour\n" );
	  return -1;
	}
     
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse minutes\n" );
	  return -1;
	}
#else
      skipTill( NUMERIC );
#endif

      presentTM.tm_min = atoi( token );

#ifdef USE_TOKENS      
      status = eat( COLON, 1 );
      
      if( status == -1 )
	{
	  fprintf( stderr, "No colon after minutes\n" );
	  return -1;
	}
      
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse seconds\n" );
	  return -1;
	}
#else
      getTill( NUMERIC );
#endif

      presentTM.tm_sec = atoi( token );
      
      *t = mktime( &presentTM );
      
      return 0;
    }

  if( toklen == 3 )
    {
#ifdef PRINT_HEADER_DEBUG
      fprintf( stderr, "Date 2\n" );
#endif

      presentTM.tm_mon = months( token );

#ifdef USE_TOKENS  
      eat( SP, -1 );
      
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse day of the month\n" );
	  return -1;
	}
#else
      skipTill( NUMERIC );
#endif

      presentTM.tm_mday = atoi( token );

#ifdef USE_TOKENS  
      eat( SP, -1 );

      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse hours\n" );
	  return -1;
	}
#else
      getTill( NUMERIC );
#endif

      presentTM.tm_hour = atoi( token );

#ifdef USE_TOKENS      
      status = eat( COLON, 1 );
      
      if( status == -1 )
	{
	  fprintf( stderr, "No colon after hours\n" );
	  return -1;
	}
      
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse minutes\n" );
	  return -1;
	}
#else
      getTill( NUMERIC );
#endif

      presentTM.tm_min = atoi( token );

#ifdef USE_TOKENS      
      status = eat( COLON, 1 );
      
      if( status == -1 )
	{
	  fprintf( stderr, "No colon after minutes\n" );
	  return -1;
	}
      
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse seconds\n" );
	  return -1;
	}
#else
      getTill( NUMERIC );
#endif

      presentTM.tm_sec = atoi( token );

#ifdef USE_TOKENS      
      eat( SP, -1 );

      status = getNextToken();

      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse year\n" );
	  return -1;
	}
#else
      getTill( NUMERIC );
#endif

      presentTM.tm_year = atoi( token ) - 1900;

      *t = mktime( &presentTM );
            
      return 0;
    }

  /*  if( toklen == 9 )
    {
#ifdef PRINT_HEADER_DEBUG
      fprintf( stderr, "Date 3\n" );
#endif

      token[ 2 ] = '\0';
      token[ 6 ] = '\0';
      
      presentTM.tm_mday = atoi( token );
      presentTM.tm_mon = months( token + 3 );
      presentTM.tm_year = atoi( token + 7 ) + 100;

      eat( SP, -1 );

      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse hours\n" );
	  return -1;
	}
      
      presentTM.tm_hour = atoi( token );
      
      status = eat( COLON, 1 );
      
      if( status == -1 )
	{
	  fprintf( stderr, "No colon after hours\n" );
	  return -1;
	}
      
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse minutes\n" );
	  return -1;
	}
      
      presentTM.tm_min = atoi( token );
      
      status = eat( COLON, 1 );
      
      if( status == -1 )
	{
	  fprintf( stderr, "No colon after minutes\n" );
	  return -1;
	}
      
      status = getNextToken();
      
      if( status == -1 )
	{
	  fprintf( stderr, "Cannot parse seconds\n" );
	  return -1;
	}
      
      presentTM.tm_sec = atoi( token );
      
      *t = mktime( &presentTM );
            
      return 0;
      }*/

  return 0;
}

int parseResponseLM( httpResponse *presentResPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse response\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }
  
  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
    }
#else
  skipTill( SPACE );
#endif

  status = parseDate( &presentResPtr->last_modified );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse date\n" );
      return -1;
    }

  return 0;
}

int months( char *name )
{
  switch( name[ 0 ] )
    {
    case 'j': case 'J':
      switch( name[ 3 ] )
	{
	case 'e': case 'E':
	  return 5;
	case 'u': case 'U':
	  return 0;
	case 'y': case 'Y':
	  return 6;
	default:
	  return 0;
	}
      break;
    case 'f': case 'F':
      return 1;
    case 'm': case 'M':
      switch( name[ 2 ] )
	{
	case 'r': case 'R':
	  return 2;
	case 'y': case 'Y':
	  return 4;
	default:
	  return 2;
	}
      break;
      return 2;
    case 'a': case 'A':
      switch( name[ 1 ] )
	{
	case 'p': case 'P':
	  return 3;
	case 'u': case 'U':
	  return 7;
	default:
	  return 3;
	}
      break;
    case 's': case 'S':
      return 8;
    case 'o': case 'O':
      return 9;
    case 'n': case 'N':
      return 10;
    case 'd': case 'D':
      return 11;
    default:
      return 0;
    }
}

int parseRequestAccept( httpRequest *presentRequestPtr )
{
  //int status;
#ifndef NO_ACCEPT_PREFS
  addToTail( presentRequestPtr->headers, 0, buf );
#endif

  return 0;
}

int parseRequestAcceptCharset( httpRequest *presentRequestPtr )
{
  //int status;
#ifndef NO_ACCEPT_PREFS
  addToTail( presentRequestPtr->headers, 0, buf );
#endif
  return 0;
}

int parseRequestAcceptEncoding( httpRequest *presentRequestPtr )
{
  //int status;
#ifndef NO_ACCEPT_PREFS
  addToTail( presentRequestPtr->headers, 0, buf );
#endif

  return 0;
}

int parseRequestAcceptLanguage( httpRequest *presentRequestPtr )
{
  //int status;
#ifndef NO_ACCEPT_PREFS
  addToTail( presentRequestPtr->headers, 0, buf );
#endif
  return 0;
}

int parseRequestAcceptRanges( httpRequest *presentRequestPtr )
{
  //int status;
  addToTail( presentRequestPtr->headers, 0, buf );
  return 0;
}

int parseRequestConnection( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, received something else\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentRequestPtr->conn = token;

  return 0;
}

int parseRequestProxyConnection( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, received something else\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentRequestPtr->pconn = token;

  return 0;
}

int parseRequestKeepAlive( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, received something else\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentRequestPtr->kl = token;

  return 0;
}

int parseResponseConnection( httpResponse *presentResponsePtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, received something else\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResponsePtr->status = BAD_RESPONSE;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentResponsePtr->conn = token;

  return 0;
}

int parseResponseProxyConnection( httpResponse *presentResponsePtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, received something else\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResponsePtr->status = BAD_RESPONSE;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentResponsePtr->pconn = token;

  return 0;
}

int parseRequestContentEncoding( httpRequest *presentRequestPtr )
{
  //int status;
  addToTail( presentRequestPtr->headers, 0, buf );
  return 0;
}

int parseRequestContentLanguage( httpRequest *presentRequestPtr )
{
  //int status;
  addToTail( presentRequestPtr->headers, 0, buf );
  return 0;
}

int parseRequestContentLength( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS  
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon but found different\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentRequestPtr->status = BAD_REQUEST;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentRequestPtr->con_len = atoi( token );
  
  return 0;
}

int parseRequestContentLocation( httpRequest *presentRequestPtr )
{
  //int status;
  addToTail( presentRequestPtr->headers, 0, buf );
  return 0;
}

int parseRequestContentMD5( httpRequest *presentRequestPtr )
{
  //int status;
  addToTail( presentRequestPtr->headers, 0, buf );
  return 0;
}

int parseRequestContentRange( httpRequest *presentRequestPtr )
{
  //int status;
  addToTail( presentRequestPtr->headers, 0, buf );
  return 0;
}

int parseRequestContentType( httpRequest *presentRequestPtr )
{
  //int status;
  addToTail( presentRequestPtr->headers, 0, buf );
  return 0;
}

int parseResponseContentEncoding( httpResponse *presentResponsePtr )
{
  //int status;
  addToTail( presentResponsePtr->headers, 0, buf );
  return 0;
}

int parseResponseContentLanguage( httpResponse *presentResponsePtr )
{
  //int status;
  addToTail( presentResponsePtr->headers, 0, buf );
  return 0;
}

int parseResponseContentLength( httpResponse *presentResponsePtr )
{
  int status;

#ifdef USE_TOKENS  
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon but found different\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResponsePtr->status = BAD_RESPONSE;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentResponsePtr->con_len = atoi( token );
  
  return 0;
}

int parseResponseContentLocation( httpResponse *presentResponsePtr )
{
  //int status;
  addToTail( presentResponsePtr->headers, 0, buf );
  return 0;
}

int parseResponseContentMD5( httpResponse *presentResponsePtr )
{
  //int status;
  addToTail( presentResponsePtr->headers, 0, buf );
  return 0;
}

int parseResponseContentRange( httpResponse *presentResponsePtr )
{
  //int status;
  addToTail( presentResponsePtr->headers, 0, buf );
  return 0;
}

int parseResponseContentType( httpResponse *presentResponsePtr )
{
  //int status;
  presentResponsePtr->contentType = buf;
  addToTail( presentResponsePtr->headers, 0, buf );
  return 0;
}

int parseRequestTE( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, found different\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
  
  status = eat( SP, -1 );
  
  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentRequestPtr->status = BAD_REQUEST;
    }
  
  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentRequestPtr->te = token;

  return 0;
}

int parseResponseTE( httpResponse *presentResponsePtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, found different\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResponsePtr->status = BAD_RESPONSE;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResponsePtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentResponsePtr->te = token;

  return 0;
}

int parseResponseResponseTime( httpResponse *presentResPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentResPtr->response_time = atoi( token );

  return 0;
}

int parseResponseRequestTime( httpResponse *presentResPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
    }
  
  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentResPtr->request_time = atoi( token );

  return 0;
}

int parseUnknownResponseHeader( httpResponse *presentResponsePtr )
{
#ifdef PRINT_HEADER_DEBUG
  fprintf( stderr, "Parsing an unknown response header\n" );
#endif

  addToTail( presentResponsePtr->headers, 0, (char*)buf );
  
  return 0;
}

int parseUnknownRequestHeader( httpRequest *presentRequestPtr )
{
#ifdef PRINT_HEADER_DEBUG
  fprintf( stderr, "Parsing an unknown request header\n" );
#endif

  addToTail( presentRequestPtr->headers, 0, (char*)buf );
  
  return 0;
}

int parseRequestIfModifiedSince( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse request\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentRequestPtr->status = BAD_REQUEST;
    }
#else
  skillTill( ALPHANUM );
#endif

  status = parseDate( &presentRequestPtr->if_mod_since_value );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse date\n" );
      return -1;
    }

  return 0;
}

int parseRequestIfUnmodifiedSince( httpRequest *presentRequestPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse request\n" );
      presentRequestPtr->status = BAD_REQUEST;
      return -1;
    }
  
  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentRequestPtr->status = BAD_RESPONSE;
    }
#else
  skipTill( ALPHANUM );
#endif

  status = parseDate( &presentRequestPtr->if_unmod_since_value );

  if( status == -1 )
    {
      fprintf( stderr, "Cannot parse date\n" );
      return -1;
    }

  return 0;
}

int parseResponseLocation( httpResponse *presentResPtr )
{
  int status;

#ifdef USE_TOKENS
  status = eat( COLON, 1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting a colon, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }

  status = eat( SP, -1 );

  if( status == -1 )
    {
      fprintf( stderr, "Expecting space, found different\n" );
      presentResPtr->status = BAD_RESPONSE;
    }

  status = getNextToken();

  if( status == -1 )
    {
      fprintf( stderr, "Cannot get next token\n" );
      presentResPtr->status = BAD_RESPONSE;
      return -1;
    }
#else
  skipTill( SPACE );
#endif

  presentResPtr->redir_location = token;

  return 0;
}

