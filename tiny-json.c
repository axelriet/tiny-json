
/*

<https://github.com/rafagafe/tiny-json>

  Licensed under the MIT License <http://opensource.org/licenses/MIT>.
  SPDX-License-Identifier: MIT
  Copyright (c) 2016-2018 Rafa Garcia <rafagarcia77@gmail.com>.

  Permission is hereby  granted, free of charge, to any  person obtaining a copy
  of this software and associated  documentation files (the "Software"), to deal
  in the Software  without restriction, including without  limitation the rights
  to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
  copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
  IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
  FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
  AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
  LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include <string.h>
#include <ctype.h>
#include "tiny-json.h"

/** Structure to handle a heap of JSON properties. */
typedef struct jsonStaticPool_s {
    json_t* mem;      /**< Pointer to array of json properties.      */
    unsigned int qty; /**< Length of the array of json properties.   */
    unsigned int nextFree;  /**< The index of the next free json property. */
    jsonPool_t pool;
} jsonStaticPool_t;

/* Search a property by its name in a JSON object. */
json_t const* json_getProperty( json_t const* obj, CHAR_T const* property ) {
    json_t const* sibling;
    for( sibling = obj->u.c.child; sibling; sibling = sibling->sibling )
#ifdef TINY_JSON_USE_WCHAR
        if (sibling->name && !wcscmp (sibling->name, property))
#else
        if (sibling->name && !strcmp (sibling->name, property))
#endif
            return sibling;
    return 0;
}

/* Search a property by its name in a JSON object and return its value. */
CHAR_T const* json_getPropertyValue( json_t const* obj, CHAR_T const* property ) {
	json_t const* field = json_getProperty( obj, property );
	if ( !field ) return 0;
        jsonType_t type = json_getType( field );
        if ( JSON_ARRAY >= type ) return 0;
	return json_getValue( field );
}

/* Internal prototypes: */
static CHAR_T* goBlank( CHAR_T* str );
static CHAR_T* goNum( CHAR_T* str );
static json_t* poolInit( jsonPool_t* pool );
static json_t* poolAlloc( jsonPool_t* pool );
static CHAR_T* objValue( CHAR_T* ptr, json_t* obj, jsonPool_t* pool );
static CHAR_T* setToNull( CHAR_T* ch );
static bool isEndOfPrimitive( CHAR_T ch );

/* Parse a string to get a json. */
json_t const* json_createWithPool( CHAR_T *str, jsonPool_t *pool ) {
    CHAR_T* ptr = goBlank( str );
    if ( !ptr || (*ptr != T('{') && *ptr != T('[')) ) return 0;
    json_t* obj = pool->init( pool );
    obj->name    = 0;
    obj->sibling = 0;
    obj->u.c.child = 0;
    ptr = objValue( ptr, obj, pool );
    if ( !ptr ) return 0;
    return obj;
}

/* Parse a string to get a json. */
json_t const* json_create( CHAR_T* str, json_t mem[], unsigned int qty ) {
    jsonStaticPool_t spool;
    spool.mem = mem;
    spool.qty = qty;
    spool.pool.init = poolInit;
    spool.pool.alloc = poolAlloc;
    return json_createWithPool( str, &spool.pool );
}

/** Get a special character with its escape character. Examples:
  * 'b' -> '\\b', 'n' -> '\\n', 't' -> '\\t'
  * @param ch The escape character.
  * @retval  The character code. */
static CHAR_T getEscape( CHAR_T ch ) {
    static struct { CHAR_T ch; CHAR_T code; } const pair[] = {
        { T('\"'), T('\"') }, { T('\\'), T('\\') },
        { T('/'),  T('/')  }, { T('b'),  T('\b') },
        { T('f'),  T('\f') }, { T('n'),  T('\n') },
        { T('r'),  T('\r') }, { T('t'),  T('\t') },
    };
    unsigned int i;
    for( i = 0; i < sizeof pair / sizeof *pair; ++i )
        if ( pair[i].ch == ch )
            return pair[i].code;
    return T('\0');
}

/** Parse 4 characters.
  * @param str Pointer to  first digit.
  * @retval '?' If the four characters are hexadecimal digits.
  * @retval '\0' In other cases. */
static CHAR_T getCharFromUnicode(CHAR_T const* str ) {
    unsigned int i;
    for( i = 0; i < 4; ++i )
        if ( !isxdigit( str[i] ) )
            return T('\0');
    return T('?');
}

/** Parse a string and replace the scape characters by their meaning characters.
  * This parser stops when finds the character '\"'. Then replaces '\"' by '\0'.
  * @param str Pointer to first character.
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* parseString( CHAR_T* str ) {
    CHAR_T* head = str;
    CHAR_T* tail = str;
    for( ; *head; ++head, ++tail ) {
        if ( *head == T('\"') ) {
            *tail = T('\0');
            return ++head;
        }
        if ( *head == T('\\') ) {
            if ( *++head == T('u') ) {
                CHAR_T const ch = getCharFromUnicode( ++head );
                if ( ch == T('\0') ) return 0;
                *tail = ch;
                head += 3;
            }
            else {
                CHAR_T const esc = getEscape( *head );
                if ( esc == T('\0') ) return 0;
                *tail = esc;
            }
        }
        else *tail = *head;
    }
    return 0;
}

/** Parse a string to get the name of a property.
  * @param ptr Pointer to first character.
  * @param property The property to assign the name.
  * @retval Pointer to first of property value. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* propertyName( CHAR_T* ptr, json_t* property ) {
    property->name = ++ptr;
    ptr = parseString( ptr );
    if ( !ptr ) return 0;
    ptr = goBlank( ptr );
    if ( !ptr ) return 0;
    if ( *ptr++ != T(':') ) return 0;
    return goBlank( ptr );
}

/** Parse a string to get the value of a property when its type is JSON_TEXT.
  * @param ptr Pointer to first character ('\"').
  * @param property The property to assign the name.
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* textValue( CHAR_T* ptr, json_t* property ) {
    ++property->u.value;
    ptr = parseString( ++ptr );
    if ( !ptr ) return 0;
    property->type = JSON_TEXT;
    return ptr;
}

/** Compare two strings until get the null character in the second one.
  * @param ptr sub string
  * @param str main string
  * @retval Pointer to next character.
  * @retval Null pointer if any error occur. */
static CHAR_T* checkStr( CHAR_T* ptr, CHAR_T const* str ) {
    while( *str )
        if ( *ptr++ != *str++ )
            return 0;
    return ptr;
}

/** Parser a string to get a primitive value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @param value String with the primitive literal.
  * @param type The code of the type. ( JSON_BOOLEAN or JSON_NULL )
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* primitiveValue( CHAR_T* ptr, json_t* property, CHAR_T const* value, jsonType_t type ) {
    ptr = checkStr( ptr, value );
    if ( !ptr || !isEndOfPrimitive( *ptr ) ) return 0;
    ptr = setToNull( ptr );
    property->type = type;
    return ptr;
}

/** Parser a string to get a true value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* trueValue( CHAR_T* ptr, json_t* property ) {
    return primitiveValue( ptr, property, T("true"), JSON_BOOLEAN );
}

/** Parser a string to get a false value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* falseValue( CHAR_T* ptr, json_t* property ) {
    return primitiveValue( ptr, property, T("false"), JSON_BOOLEAN );
}

/** Parser a string to get a null value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* nullValue( CHAR_T* ptr, json_t* property ) {
    return primitiveValue( ptr, property, T("null"), JSON_NULL );
}

/** Analyze the exponential part of a real number.
  * @param ptr Pointer to first character.
  * @retval Pointer to first non numerical after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* expValue( CHAR_T* ptr ) {
    if ( *ptr == T('-') || *ptr == T('+') ) ++ptr;
    if ( !isdigit( (int)(*ptr) ) ) return 0;
    ptr = goNum( ++ptr );
    return ptr;
}

/** Analyze the decimal part of a real number.
  * @param ptr Pointer to first character.
  * @retval Pointer to first non numerical after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* fraqValue( CHAR_T* ptr ) {
    if ( !isdigit( (int)(*ptr) ) ) return 0;
    ptr = goNum( ++ptr );
    if ( !ptr ) return 0;
    return ptr;
}

/** Parser a string to get a numerical value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type: JSON_REAL or JSON_INTEGER.
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* numValue( CHAR_T* ptr, json_t* property ) {
    if ( *ptr == T('-') ) ++ptr;
    if ( !isdigit( (int)(*ptr) ) ) return 0;
    if ( *ptr != T('0') ) {
        ptr = goNum( ptr );
        if ( !ptr ) return 0;
    }
    else if ( isdigit( (int)(*++ptr) ) ) return 0;
    property->type = JSON_INTEGER;
    if ( *ptr == T('.') ) {
        ptr = fraqValue( ++ptr );
        if ( !ptr ) return 0;
        property->type = JSON_REAL;
    }
    if ( *ptr == T('e') || *ptr == T('E') ) {
        ptr = expValue( ++ptr );
        if ( !ptr ) return 0;
        property->type = JSON_REAL;
    }
    if ( !isEndOfPrimitive( *ptr ) ) return 0;
    if ( JSON_INTEGER == property->type ) {
        CHAR_T const* value = property->u.value;
        bool const negative = *value == T('-');
        static CHAR_T const min[] = T("-9223372036854775808");
        static CHAR_T const max[] = T("9223372036854775807");
        unsigned int const maxdigits = ( negative? sizeof min: sizeof max ) - 1;
        unsigned int const len = ( unsigned int const ) ( ptr - value );
        if ( len > maxdigits ) return 0;
        if ( len == maxdigits ) {
            CHAR_T const tmp = *ptr;
            *ptr = T('\0');
            CHAR_T const* const threshold = negative ? min: max;
#ifdef TINY_JSON_USE_WCHAR
            if (0 > wcscmp (threshold, value))
                return 0;
#else
            if (0 > strcmp (threshold, value))
                return 0;
#endif
            *ptr = tmp;
        }
    }
    ptr = setToNull( ptr );
    return ptr;
}

/** Add a property to a JSON object or array.
  * @param obj The handler of the JSON object or array.
  * @param property The handler of the property to be added. */
static void add( json_t* obj, json_t* property ) {
    property->sibling = 0;
    if ( !obj->u.c.child ){
	    obj->u.c.child = property;
	    obj->u.c.last_child = property;
    } else {
	    obj->u.c.last_child->sibling = property;
	    obj->u.c.last_child = property;
    }
}

/** Parser a string to get a json object value.
  * @param ptr Pointer to first character.
  * @param obj The handler of the JSON root object or array.
  * @param pool The handler of a json pool for creating json instances.
  * @retval Pointer to first character after the value. If success.
  * @retval Null pointer if any error occur. */
static CHAR_T* objValue( CHAR_T* ptr, json_t* obj, jsonPool_t* pool ) {
    obj->type    = *ptr == T('{') ? JSON_OBJ : JSON_ARRAY;
    obj->u.c.child = 0;
    obj->sibling = 0;
    ptr++;
    for(;;) {
        ptr = goBlank( ptr );
        if ( !ptr ) return 0;
        if ( *ptr == T(',') ) {
            ++ptr;
            continue;
        }
        CHAR_T const endchar = ( obj->type == JSON_OBJ )? T('}'): T(']');
        if ( *ptr == endchar ) {
            *ptr = T('\0');
            json_t* parentObj = obj->sibling;
            if ( !parentObj ) return ++ptr;
            obj->sibling = 0;
            obj = parentObj;
            ++ptr;
            continue;
        }
        json_t* property = pool->alloc( pool );
        if ( !property ) return 0;
        if( obj->type != JSON_ARRAY ) {
            if ( *ptr != T('\"') ) return 0;
            ptr = propertyName( ptr, property );
            if ( !ptr ) return 0;
        }
        else property->name = 0;
        add( obj, property );
        property->u.value = ptr;
        switch( *ptr ) {
            case T('{'):
                property->type    = JSON_OBJ;
                property->u.c.child = 0;
                property->sibling = obj;
                obj = property;
                ++ptr;
                break;
            case T('['):
                property->type    = JSON_ARRAY;
                property->u.c.child = 0;
                property->sibling = obj;
                obj = property;
                ++ptr;
                break;
            case T('\"'): ptr = textValue( ptr, property );  break;
            case T('t'):  ptr = trueValue( ptr, property );  break;
            case T('f'):  ptr = falseValue( ptr, property ); break;
            case T('n'):  ptr = nullValue( ptr, property );  break;
            default:   ptr = numValue( ptr, property );   break;
        }
        if ( !ptr ) return 0;
    }
}

/** Initialize a json pool.
  * @param pool The handler of the pool.
  * @return a instance of a json. */
static json_t* poolInit( jsonPool_t* pool ) {
    jsonStaticPool_t *spool = json_containerOf( pool, jsonStaticPool_t, pool );
    spool->nextFree = 1;
    return spool->mem;
}

/** Create an instance of a json from a pool.
  * @param pool The handler of the pool.
  * @retval The handler of the new instance if success.
  * @retval Null pointer if the pool was empty. */
static json_t* poolAlloc( jsonPool_t* pool ) {
    jsonStaticPool_t *spool = json_containerOf( pool, jsonStaticPool_t, pool );
    if ( spool->nextFree >= spool->qty ) return 0;
    return spool->mem + spool->nextFree++;
}

/** Checks whether an character belongs to set.
  * @param ch Character value to be checked.
  * @param set Set of characters. It is just a null-terminated string.
  * @return true or false there is membership or not. */
static bool isOneOfThem( CHAR_T ch, CHAR_T const* set ) {
    while( *set != T('\0') )
        if ( ch == *set++ )
            return true;
    return false;
}

/** Increases a pointer while it points to a character that belongs to a set.
  * @param str The initial pointer value.
  * @param set Set of characters. It is just a null-terminated string.
  * @return The final pointer value or null pointer if the null character was found. */
static CHAR_T* goWhile( CHAR_T* str, CHAR_T const* set ) {
    for(; *str != T('\0'); ++str ) {
        if ( !isOneOfThem( *str, set ) )
            return str;
    }
    return 0;
}

/** Set of characters that defines a blank. */
static CHAR_T const* const blank = T(" \n\r\t\f");

/** Increases a pointer while it points to a white space character.
  * @param str The initial pointer value.
  * @return The final pointer value or null pointer if the null character was found. */
static CHAR_T* goBlank( CHAR_T* str ) {
    return goWhile( str, blank );
}

/** Increases a pointer while it points to a decimal digit character.
  * @param str The initial pointer value.
  * @return The final pointer value or null pointer if the null character was found. */
static CHAR_T* goNum( CHAR_T* str ) {
    for( ; *str != T('\0'); ++str ) {
        if ( !isdigit( (int)(*str) ) )
            return str;
    }
    return 0;
}

/** Set of characters that defines the end of an array or a JSON object. */
static CHAR_T const* const endofblock = T("}]");

/** Set a CHAR_T to '\0' and increase its pointer if the CHAR_T is different to '}' or ']'.
  * @param ch Pointer to character.
  * @return  Final value pointer. */
static CHAR_T* setToNull( CHAR_T* ch ) {
    if ( !isOneOfThem( *ch, endofblock ) ) *ch++ = T('\0');
    return ch;
}

/** Indicate if a character is the end of a primitive value. */
static bool isEndOfPrimitive( CHAR_T ch ) {
    return ch == T(',') || isOneOfThem( ch, blank ) || isOneOfThem( ch, endofblock );
}
