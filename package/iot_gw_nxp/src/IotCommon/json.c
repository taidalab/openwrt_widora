// ------------------------------------------------------------------
// JSON stream parser
// ------------------------------------------------------------------
// Parses a JSON steam one character at a time and uses callbacks
// to the calling processes
// Can handle simple JSON strings and is very suitable for
// embedded systems with small footprint
// ------------------------------------------------------------------
// Author:    nlv10677
// Copyright: NXP B.V. 2014. All rights reserved
// ------------------------------------------------------------------

/** \file
 * \brief JSON stream parser. Parses a JSON steam one character at a time and uses callbacks to
 * the calling processes
 */
 
/*
JSON(JavaScript Object Notation) 是一种轻量级的数据交换格式。
理想的数据交换语言。 易于人阅读和编写，同时也易于机器解析和生成(一般用于提升网络传输速率)。
JSON 语法规则
JSON 语法是 JavaScript 对象表示语法的子集。
数据在键值对中
数据由逗号分隔
花括号保存对象
方括号保存数组
JSON 名称/值对
JSON 数据的书写格式是：名称/值对。
名称/值对组合中的名称写在前面（在双引号中），值对写在后面(同样在双引号中)，中间用冒号隔开：
1
"firstName":"John"
这很容易理解，等价于这条 JavaScript 语句：
1
firstName="John"
JSON 值
JSON 值可以是：
数字（整数或浮点数）
字符串（在双引号中）
逻辑值（true 或 false）
数组（在方括号中）
对象（在花括号中）
null

名称 / 值对
按照最简单的形式，可以用下面这样的 JSON 表示"名称 / 值对"：
1
{"firstName":"Brett"}
这个示例非常基本，而且实际上比等效的纯文本"名称 / 值对"占用更多的空间：
1
firstName=Brett
但是，当将多个"名称 / 值对"串在一起时，JSON 就会体现出它的价值了。首先，可以创建包含多个"名称 / 值对"的 记录，比如：
1
{"firstName":"Brett","lastName":"McLaughlin","email":"aaaa"}
从语法方面来看，这与"名称 / 值对"相比并没有很大的优势，但是在这种情况下 JSON 更容易使用，而且可读性更好。例如，它明
确地表示以上三个值都是同一记录的一部分；花括号使这些值有了某种联系。

*/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atoi.h"
#include "json.h"

#define MAXPARSERS           2

#define MAXSTATES            10
#define MAXSTACK             5
#define MAXERRORBUFFER       20

#define MAXNAME              30
#define MAXVALUE             160   // was 80

#define STATE_NONE           0
#define STATE_INOBJECT       1
#define STATE_TONAME         2
#define STATE_INNAME         3
#define STATE_TOCOLUMN       4
#define STATE_TOVALUE        5
#define STATE_INSTRING       6
#define STATE_INNUM          7
#define STATE_INARRAY        8
#define STATE_OUTVALUE       9

#define ERR_NONE             0
#define ERR_DISCARD          1
#define ERR_NAMETOOLONG      2
#define ERR_VALUETOOLONG     3
#define ERR_PARSE_OBJECT     4
#define ERR_PARSE_NAME       5
#define ERR_PARSE_ILLNAME    6
#define ERR_PARSE_ASSIGNMENT 7
#define ERR_PARSE_VALUE      8
#define ERR_PARSE_ARRAY      9
#define ERR_INTERNAL         10

// #define GENERATE_ERROR_ON_DISCARD   1

static char * json_errors[] = {
    "none",
    "discard",
    "name too long",
    "value too long",
    "parsing object",
    "parsing name",
    "illegal name char",
    "parsing assignment",
    "parsing value",
    "parsing array",
    "internal error"
};

// -------------------------------------------------------------
// Globals
// -------------------------------------------------------------

typedef struct {
    int state;
    int states[MAXSTATES];
    int stateIndex;

    int allowComma;
    int isSlash;

    int stack;
    char name[MAXSTACK][MAXNAME];
    char value[MAXSTACK][MAXVALUE];

    char errbuf[MAXERRORBUFFER];
    int errbufIndex;
    int numchars;

    OnError          onError;
    OnObjectStart    onObjectStart;
    OnObjectComplete onObjectComplete;
    OnArrayStart     onArrayStart;
    OnArrayComplete  onArrayComplete;
    OnString         onString;
    OnInteger        onInteger;

} parser_data_t;

static parser_data_t parserData[MAXPARSERS];
static parser_data_t * parser = &parserData[0];

// -------------------------------------------------------------
// Error handling
// -------------------------------------------------------------

static void jsonError(int err) {
    char locerrbuf[MAXERRORBUFFER + 2];

    int i;
    int start;
    int len = parser->numchars;
    if (len >= MAXERRORBUFFER) len = MAXERRORBUFFER;
    start = parser->errbufIndex - len;
    if (start < 0) start += MAXERRORBUFFER;
    for (i = 0; i < len; i++) {
        locerrbuf[i] = parser->errbuf[start++];
        if (start >= MAXERRORBUFFER) start = 0;
    }
    locerrbuf[len] = '\0';

    // printf("JSON error %d (%s) @ %s\n", err, json_errors[err], locerrbuf);
    if (parser->onError != NULL) parser->onError(err, json_errors[err], locerrbuf);

    jsonReset();
}

// -------------------------------------------------------------
// Helpers
// -------------------------------------------------------------

static int isWhiteSpace(char c) {//-是空的就返回1
    return ( (c == ' ') || (c == '\r') || (c == '\n') || (c == '\t') );
}

static int isSign(char c) {
    return ( (c == '-') || (c == '+') );
}

static int isNum(char c) {
    return ( (c >= '0') && (c <= '9') );
}

static int isAlpha(char c) {
    return ( ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) );
}

static int isValidNameChar(char c) {
    return ( isAlpha(c) || isNum(c) || (c == '_' ) || (c == '+') );
}

static void appendName(char c) {
    int len = strlen(parser->name[parser->stack]);
    if (len < MAXNAME) {
        parser->name[parser->stack][len] = c;
        parser->name[parser->stack][len + 1] = '\0';
    } else {
        // printf( "Name too long\n" );
        jsonError(ERR_NAMETOOLONG);
    }
}

static void appendValue(char c) {
    int len = strlen(parser->value[parser->stack]);
    if (len < MAXVALUE) {
        parser->value[parser->stack][len] = c;
        parser->value[parser->stack][len + 1] = '\0';
    } else {
        // printf( "Value too long\n" );
        jsonError(ERR_VALUETOOLONG);
    }
}

static void setState(int st) {
    // printf("Set state %d\n", st);
    parser->state = st;	//-主动设置状态

    switch (parser->state) {
        case STATE_INNAME:
            // printf( "Reset name\n" );
            parser->name[parser->stack][0] = '\0';
            break;
        case STATE_TOVALUE:
            // printf( "Reset value\n" );
            parser->value[parser->stack][0] = '\0';
            break;
        case STATE_INOBJECT:
        case STATE_INARRAY:
            // printf("Push name '%s'\n", parser->name[parser->stack]);
            if (++parser->stack < MAXSTACK) {
                parser->name[parser->stack][0] = '\0';
            } else {
                jsonError(ERR_INTERNAL);
            }
            break;
    }
}

static void toState(int st) {
    // printf("To state %d\n", st);
    parser->states[parser->stateIndex] = parser->state;
    if (++parser->stateIndex < MAXSTATES) {
        setState(st);
    } else {
        jsonError(ERR_INTERNAL);
    }
}

static void upState(void) {
    // printf("Up state\n");
    if (parser->stateIndex > 0) {
        parser->state = parser->states[--(parser->stateIndex)];
        if (parser->state == STATE_INOBJECT || parser->state == STATE_INARRAY) {
            if (parser->stack > 0) {
                parser->stack--;
                // printf("Pop name '%s'\n", parser->name[parser->stack]);
                parser->allowComma = 1;
            } else {
                jsonError(ERR_INTERNAL);
            }
        } else if (parser->state == STATE_NONE) {
            // printf( "Reset name - 2\n" );
            parser->name[parser->stack][0] = '\0';
        }
    } else {
        jsonError(ERR_INTERNAL);
    }
}

// -------------------------------------------------------------
// Eat character
// -------------------------------------------------------------

static void jsonEatNoLog(char c) {//-这个里面完整的解析了数据,然后根据不同的定义调用不同的处理函数,这样就完成了命令的接收和应答
    int again = 0;

    // int prevstate = parser->state;

    switch (parser->state) {
        case STATE_NONE:
            if (c == '{') {//-遇到这个字符就进行下面的处理,一套流程就实现了JSON解析
                if (parser->onObjectStart != NULL) parser->onObjectStart(parser->name[parser->stack]);
                toState(STATE_INOBJECT);
                toState(STATE_TONAME);
                parser->allowComma = 0;
            } else if (c == '"') {
                toState(STATE_INNAME);
            } else if (!isWhiteSpace(c)) {
#ifdef GENERATE_ERROR_ON_DISCARD
                jsonError(ERR_DISCARD);
#endif
            }
            break;

        case STATE_INOBJECT:
            if (c == '}') {
                // printf( "-------> onObjectComplete( '%s' )\n", parser->name[parser->stack] );
                if (parser->onObjectComplete != NULL) parser->onObjectComplete(parser->name[parser->stack]);
                upState();
            } else if (c == '"') {
                toState(STATE_INNAME);
            } else if (c == ',' && parser->allowComma) {
                parser->allowComma = 0;
                toState(STATE_TONAME);
            } else if (!isWhiteSpace(c)) {
                jsonError(ERR_PARSE_OBJECT);
            }
            break;

        case STATE_TONAME:
            if (c == '"') {
                setState(STATE_INNAME);
            } else if (!isWhiteSpace(c)) {
                jsonError(ERR_PARSE_NAME);
            }
            break;

        case STATE_INNAME:
            if (c == '"') {
                setState(STATE_TOCOLUMN);
            } else if (isValidNameChar(c)) {
                appendName(c);
            } else {
                jsonError(ERR_PARSE_ILLNAME);
            }
            break;

        case STATE_TOCOLUMN:
            if (c == ':') {
                setState(STATE_TOVALUE);
            } else if (!isWhiteSpace(c)) {
                jsonError(ERR_PARSE_ASSIGNMENT);
            }
            break;

        case STATE_TOVALUE:
            if (c == '"') {
                parser->isSlash = 0;
                setState(STATE_INSTRING);
            } else if (isNum(c) || isSign(c)) {
                appendValue(c);
                setState(STATE_INNUM);
            } else if (c == '[') {
                if (parser->onArrayStart != NULL) parser->onArrayStart(parser->name[parser->stack]);
                setState(STATE_INARRAY);
                toState(STATE_TONAME);
            } else if (c == '{') {
                if (parser->onObjectStart != NULL) parser->onObjectStart(parser->name[parser->stack]);
                setState(STATE_INOBJECT);
                toState(STATE_TONAME);
            } else if (!isWhiteSpace(c)) {
                jsonError(ERR_PARSE_VALUE);
            }
            break;

        case STATE_INSTRING:
            if (!parser->isSlash && c == '\\') {
                parser->isSlash = 1;
            } else if (parser->isSlash) {
                parser->isSlash = 0;
                appendValue('\\');
                appendValue(c);
            } else if (c == '"') {
                // printf( "-------> onString( '%s', '%s' )\n", parser->name[parser->stack], parser->value[parser->stack] );
                if (parser->onString != NULL) parser->onString(parser->name[parser->stack], parser->value[parser->stack]);
                setState(STATE_OUTVALUE);
            } else {
                appendValue(c);
            }
            break;

        case STATE_INNUM:
            if (isNum(c)) {
                appendValue(c);
            } else {
                // printf( "-------> onInteger( Name='%s', value='%s' )\n", parser->name[parser->stack], parser->value[parser->stack] );
                if (parser->onInteger != NULL) {
                     parser->onInteger( parser->name[parser->stack],
                                        Atoi( parser->value[parser->stack] ) );
                }
                setState(STATE_OUTVALUE);
                again = 1;
            }
            break;

        case STATE_INARRAY:
            if (c == ']') {
                // printf( "-------> onArrayComplete( '%s' )\n", parser->name[parser->stack] );
                if (parser->onArrayComplete != NULL) parser->onArrayComplete(parser->name[parser->stack]);
                upState();
                // setState( STATE_OUTVALUE );
            } else if (c == '"') {
                toState(STATE_INNAME);
            } else if (c == ',' && parser->allowComma) {
                parser->allowComma = 0;
                toState(STATE_TONAME);
            } else if (!isWhiteSpace(c)) {
                jsonError(ERR_PARSE_ARRAY);
            }
            break;

        case STATE_OUTVALUE:
            if (!isWhiteSpace(c)) {
                if (c == ',') {
                    toState(STATE_TONAME);
                } else {
                    upState();
                    again = 1;
                }
            }
            break;
    }

    // printf( "Eat %c, state = %d -> %d\n", c, prevstate, parser->state );

//    printf("%c  ->  S[%d]=", c, parser->stateIndex);
//    int i;
//    for (i = 0; i < parser->stateIndex; i++) printf("%d ", parser->states[i]);
//    printf(", state=%d, A=%d - %d\n", parser->state, parser->allowComma, parser->again);

    if (again) jsonEatNoLog(c);	//-从一个字符开始解析,如果需要的话可以自己再读取下一个继续
}

// -------------------------------------------------------------
// Public Interface
// -------------------------------------------------------------

/**
 * \brief Reset the JSON parser
 */
void jsonReset(void) {
    parser->allowComma      = 0;
    parser->isSlash         = 0;
    parser->state           = STATE_NONE;
    parser->stateIndex      = 0;
    parser->stack           = 0;
    parser->name[parser->stack][0]  = '\0';
    parser->value[parser->stack][0] = '\0';
    parser->errbufIndex     = 0;
    parser->numchars        = 0;
}

/**
 * \brief Parse a JSON stream, one character at a time
 * \param c Character to parse
 */
void jsonEat(char c) {//-开始解析的进入点

    parser->numchars++;

    // Log character in error buffer
    if (!isWhiteSpace(c)) {
        parser->errbuf[parser->errbufIndex++] = c;
        if (parser->errbufIndex >= MAXERRORBUFFER) parser->errbufIndex = 0;
    }

    jsonEatNoLog(c);
}
//-下面都是给一个解析结构赋值的,这样就动态的创建了一个解析体
void jsonSetOnError(OnError oe) {
    parser->onError = oe;
}

void jsonSetOnObjectStart(OnObjectStart oos) {
    parser->onObjectStart = oos;
}

void jsonSetOnObjectComplete(OnObjectComplete oo) {
    parser->onObjectComplete = oo;
}

void jsonSetOnArrayStart(OnArrayStart oas) {
    parser->onArrayStart = oas;
}

void jsonSetOnArrayComplete(OnArrayComplete oa) {
    parser->onArrayComplete = oa;
}

void jsonSetOnString(OnString os) {
    parser->onString = os;
}

void jsonSetOnInteger(OnInteger oi) {
    parser->onInteger = oi;
}

// -------------------------------------------------------------
// Parser switching (for nested parsing)
// -------------------------------------------------------------

int jsonGetSelected( void ) {	//-获得当前解析结构在数组中的位置
    int i;
    for ( i=0; i<MAXPARSERS; i++ ) {
        if ( parser == &parserData[i] ) return( i );
    }
    return( 0 );
}

void jsonSelectNext( void ) {
    int p = jsonGetSelected();
    if ( p < ( MAXPARSERS - 1 ) ) {
        // printf( "JSON select %d\n", p+1 );
        parser = &parserData[p+1];
    } else {
        printf( "Error: parser overflow\n" );
    }
}

void jsonSelectPrev( void ) {
    int p = jsonGetSelected();
    if ( p > 0 ) {
        // printf( "JSON select %d\n", p-1 );
        parser = &parserData[p-1];	//-记录了解析结构的数据
    } else {
        printf( "Error: parser underflow\n" );
    }
}

